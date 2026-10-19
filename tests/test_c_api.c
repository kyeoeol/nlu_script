/* Exercises the ANSI C interface. */
#include <stdio.h>
#include <string.h>

#include "nlu_script/nlu_script.h"

static int failures = 0;

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      ++failures;                                                    \
      fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                \
  } while (0)

static const char* kScript =
    "card:lost\n"
    "    report a lost <card>[credit{type=CREDIT}|debit{type=DEBIT}] card</card>\n"
    "    <card>[신용{type=CREDIT}|체크{type=DEBIT}]카드</card> 분실 신고\n";

static void test_match_with_slots(void) {
  char* error = NULL;
  nlu_script* script = nlu_script_compile(kScript, &error, NULL, NULL);
  nlu_result* result;
  CHECK(script != NULL);
  CHECK(error == NULL);
  if (!script) return;
  CHECK(nlu_script_rule_count(script) == 2);

  result = nlu_script_match(script, "Report a lost debit card");
  CHECK(nlu_result_count(result) == 1);
  CHECK(strcmp(nlu_result_label(result, 0), "card:lost") == 0);
  CHECK(strcmp(nlu_result_tagged(result, 0),
               "Report a lost <card type=\"DEBIT\">debit card</card>") == 0);
  CHECK(nlu_result_slot_count(result, 0) == 1);
  CHECK(strcmp(nlu_result_slot_name(result, 0, 0), "card") == 0);
  CHECK(strcmp(nlu_result_slot_text(result, 0, 0), "debit card") == 0);
  CHECK(nlu_result_slot_begin(result, 0, 0) == 14);
  CHECK(nlu_result_slot_end(result, 0, 0) == 24);
  CHECK(nlu_result_slot_attribute_count(result, 0, 0) == 1);
  CHECK(strcmp(nlu_result_slot_attribute_key(result, 0, 0, 0), "type") == 0);
  CHECK(strcmp(nlu_result_slot_attribute_value(result, 0, 0, 0), "DEBIT") == 0);
  CHECK(nlu_result_line(result, 0) == 2);
  CHECK(strcmp(nlu_result_file(result, 0), "") == 0);
  CHECK(nlu_result_specificity(result, 0) == 20);
  CHECK(nlu_result_label(result, 1) == NULL);
  CHECK(nlu_result_line(result, 1) == 0);
  CHECK(nlu_result_slot_name(result, 0, 5) == NULL);
  nlu_result_free(result);

  /* Offsets are in bytes: each Hangul syllable takes three. */
  result = nlu_script_match(script, "신용카드 분실 신고");
  CHECK(nlu_result_count(result) == 1);
  CHECK(strcmp(nlu_result_slot_text(result, 0, 0), "신용카드") == 0);
  CHECK(nlu_result_slot_begin(result, 0, 0) == 0);
  CHECK(nlu_result_slot_end(result, 0, 0) == 12);
  CHECK(strcmp(nlu_result_slot_attribute_value(result, 0, 0, 0), "CREDIT") == 0);
  nlu_result_free(result);

  result = nlu_script_match(script, "report a lost wallet");
  CHECK(nlu_result_count(result) == 0);
  nlu_result_free(result);
  nlu_script_free(script);
}

static void test_match_best(void) {
  nlu_script* script = nlu_script_compile(
      "card:lost\n    I lost my debit card\nreport:any\n    I <?>+?\n", NULL, NULL, NULL);
  nlu_result* result;
  CHECK(script != NULL);
  if (!script) return;
  result = nlu_script_match(script, "I lost my debit card");
  CHECK(nlu_result_count(result) == 2);
  nlu_result_free(result);
  result = nlu_script_match_best(script, "I lost my debit card");
  CHECK(nlu_result_count(result) == 1);
  CHECK(strcmp(nlu_result_label(result, 0), "card:lost") == 0);
  nlu_result_free(result);
  nlu_script_free(script);
}

static void test_explain(void) {
  nlu_script* script = nlu_script_compile(kScript, NULL, NULL, NULL);
  nlu_explanation* e;
  CHECK(script != NULL);
  if (!script) return;
  e = nlu_script_explain(script, "report a lost wallet", 3);
  CHECK(nlu_explanation_count(e) == 1);
  CHECK(strcmp(nlu_explanation_label(e, 0), "card:lost") == 0);
  CHECK(nlu_explanation_line(e, 0) == 2);
  CHECK(strcmp(nlu_explanation_file(e, 0), "") == 0);
  CHECK(nlu_explanation_matched(e, 0) == strlen("report a lost "));
  CHECK(nlu_explanation_complete(e, 0) == 0);
  CHECK(nlu_explanation_expected_count(e, 0) == 2);
  CHECK(strcmp(nlu_explanation_expected(e, 0, 0), "c") == 0);
  CHECK(strcmp(nlu_explanation_expected(e, 0, 1), "d") == 0);
  CHECK(nlu_explanation_expected(e, 0, 2) == NULL);
  CHECK(nlu_explanation_label(e, 1) == NULL);
  nlu_explanation_free(e);
  nlu_script_free(script);
}

static void test_lint(void) {
  nlu_script* script = nlu_script_compile(
      "card:lost\n    I lost my <?>+?\n    I lost my card\n@unused: x\n", NULL, NULL, NULL);
  nlu_lint* lint;
  CHECK(script != NULL);
  if (!script) return;
  lint = nlu_script_lint(script, 0);
  CHECK(nlu_lint_count(lint) == 2);
  CHECK(strcmp(nlu_lint_kind(lint, 0), "dead-rule") == 0);
  CHECK(nlu_lint_line(lint, 0) == 3);
  CHECK(strcmp(nlu_lint_file(lint, 0), "") == 0);
  CHECK(strcmp(nlu_lint_message(lint, 1), "@unused is never used") == 0);
  CHECK(nlu_lint_complete(lint) == 1);
  CHECK(nlu_lint_allowed(lint) == 0);
  CHECK(nlu_lint_kind(lint, 2) == NULL);
  nlu_lint_free(lint);
  nlu_script_free(script);
}

static void test_resolve_datetime(void) {
  const char* keys[] = {"day_offset", "part_of_day", "hour"};
  const char* values[] = {"+1", "PM", "15"};
  nlu_datetime now = {2026, 12, 31, 14, 20};
  nlu_datetime start, end;
  const char* grain = NULL;
  CHECK(nlu_resolve_datetime(keys, values, 3, &now, &start, &end, &grain) == 0);
  CHECK(start.year == 2027 && start.month == 1 && start.day == 1);
  CHECK(start.hour == 15 && start.minute == 0);
  CHECK(grain != NULL && strcmp(grain, "hour") == 0);
}

static void test_search(void) {
  nlu_script* script = nlu_script_compile("card\n    debit card\n", NULL, NULL, NULL);
  nlu_result* result;
  const char* text = "I lost my debit card yesterday";
  CHECK(script != NULL);
  if (!script) return;
  result = nlu_script_search(script, text);
  CHECK(nlu_result_count(result) == 1);
  CHECK(nlu_result_begin(result, 0) == 10);
  CHECK(nlu_result_end(result, 0) == 20);
  CHECK(strcmp(nlu_result_tagged(result, 0), "debit card") == 0);
  nlu_result_free(result);
  nlu_script_free(script);
}

static void test_save_and_load(void) {
  nlu_script* script = nlu_script_compile(kScript, NULL, NULL, NULL);
  nlu_script* loaded;
  nlu_result* result;
  char* data;
  char* error = NULL;
  size_t size = 0;
  CHECK(script != NULL);
  if (!script) return;
  data = nlu_script_save(script, &size);
  CHECK(data != NULL && size > 8);
  loaded = nlu_script_load(data, size, &error);
  CHECK(loaded != NULL && error == NULL);
  result = nlu_script_match(loaded, "Report a lost debit card");
  CHECK(nlu_result_count(result) == 1);
  nlu_result_free(result);
  CHECK(nlu_script_load(data, size / 2, &error) == NULL);
  CHECK(error != NULL);
  nlu_string_free(error);
  nlu_string_free(data);
  nlu_script_free(loaded);
  nlu_script_free(script);
}

static void test_compile_error(void) {
  char* error = NULL;
  int line = -1;
  int column = -1;
  nlu_script* script = nlu_script_compile("card:lost\n    [신용|체크카드 분실\n", &error, &line, &column);
  CHECK(script == NULL);
  CHECK(error != NULL);
  CHECK(line == 2);
  CHECK(column > 0);
  nlu_string_free(error);

  /* Every error, one per line; `line` and `column` locate the first. */
  script = nlu_script_compile("card:lost\n    [신용|체크카드 분실\n    I lost my @{card}\n",
                              &error, &line, &column);
  CHECK(script == NULL);
  CHECK(line == 2);
  CHECK(error != NULL && strstr(error, "line 2,") == error);
  CHECK(error != NULL && strstr(error, "\nline 3,") != NULL);
  nlu_string_free(error);
}

static void test_compile_diagnostics(void) {
  /* The included file's name looks like the location part of an error. */
  static const char* kPath = "nlu_script_c_api_diagnostics.nlu";
  static const char* kMissing = "x: line 3, column 4: y.nlu";
  nlu_diagnostics* diagnostics = NULL;
  nlu_script* script;
  FILE* f = fopen(kPath, "wb");
  CHECK(f != NULL);
  if (!f) return;
  fprintf(f, "include %s\ncard:lost\n    [신용|체크카드 분실\n", kMissing);
  fclose(f);
  script = nlu_script_compile_file_diagnose(kPath, &diagnostics);
  remove(kPath);
  CHECK(script == NULL);
  CHECK(nlu_diagnostics_count(diagnostics) == 2);
  CHECK(strcmp(nlu_diagnostics_file(diagnostics, 0), kPath) == 0);
  CHECK(nlu_diagnostics_line(diagnostics, 0) == 1);
  CHECK(nlu_diagnostics_column(diagnostics, 0) == 9);
  CHECK(strstr(nlu_diagnostics_message(diagnostics, 0), kMissing) != NULL);
  CHECK(nlu_diagnostics_line(diagnostics, 1) == 3);
  CHECK(nlu_diagnostics_column(diagnostics, 1) > 0);
  CHECK(nlu_diagnostics_file(diagnostics, 2) == NULL);
  CHECK(nlu_diagnostics_line(diagnostics, 2) == 0);
  /* The text is the message nlu_script_compile_file reports. */
  CHECK(strstr(nlu_diagnostics_text(diagnostics), nlu_diagnostics_message(diagnostics, 1)) !=
        NULL);
  nlu_diagnostics_free(diagnostics);

  script = nlu_script_compile_diagnose("card\n    debit card\n", &diagnostics);
  CHECK(script != NULL);
  CHECK(diagnostics == NULL);
  nlu_script_free(script);

  /* A file that cannot be read is no compile error. */
  script = nlu_script_compile_file_diagnose("/nonexistent/script.nlu", &diagnostics);
  CHECK(script == NULL);
  CHECK(nlu_diagnostics_count(diagnostics) == 0);
  CHECK(strstr(nlu_diagnostics_text(diagnostics), "/nonexistent/script.nlu") != NULL);
  nlu_diagnostics_free(diagnostics);
}

static void test_run_tests(void) {
  nlu_script* script = nlu_script_compile(
      "card:lost\n    I lost my debit card\nreport:any\n    I <?>+?\n", NULL, NULL, NULL);
  nlu_test_report* report;
  char* error = NULL;
  CHECK(script != NULL);
  if (!script) return;
  report = nlu_script_run_tests(script,
                                "I lost my debit card\n"
                                "    card:lost:I lost my debit card\n"
                                "thank you\n"
                                "    card:lost:thank you\n"
                                "I lost my wallet\n"
                                "    -\n",
                                "rules.test", 0, &error);
  CHECK(error == NULL);
  CHECK(nlu_test_report_passed(report) == 0);
  CHECK(nlu_test_report_failed(report) == 3);
  /* Two labels match where one was expected. */
  CHECK(nlu_test_report_line(report, 0) == 1);
  CHECK(strcmp(nlu_test_report_input(report, 0), "I lost my debit card") == 0);
  CHECK(nlu_test_report_expected_count(report, 0) == 1);
  CHECK(nlu_test_report_actual_count(report, 0) == 2);
  CHECK(strcmp(nlu_test_report_actual(report, 0, 1), "report:any:I lost my debit card") == 0);
  CHECK(nlu_test_report_actual_line(report, 0, 1) == 4);
  CHECK(strcmp(nlu_test_report_actual_file(report, 0, 1), "") == 0);
  /* Nothing matches: the actual output is "-", with no rule behind it. */
  CHECK(nlu_test_report_line(report, 1) == 3);
  CHECK(nlu_test_report_actual_count(report, 1) == 1);
  CHECK(strcmp(nlu_test_report_actual(report, 1, 0), "-") == 0);
  CHECK(nlu_test_report_actual_line(report, 1, 0) == 0);
  CHECK(strcmp(nlu_test_report_actual_file(report, 1, 0), "") == 0);
  CHECK(strcmp(nlu_test_report_expected(report, 2, 0), "-") == 0);
  CHECK(nlu_test_report_input(report, 3) == NULL);
  CHECK(nlu_test_report_actual(report, 0, 2) == NULL);
  nlu_test_report_free(report);

  /* With `best`, the most specific label alone passes. */
  report = nlu_script_run_tests(
      script, "I lost my debit card\n    card:lost:I lost my debit card\n", NULL, 1, NULL);
  CHECK(nlu_test_report_passed(report) == 1);
  CHECK(nlu_test_report_failed(report) == 0);
  nlu_test_report_free(report);

  CHECK(nlu_script_run_tests(script, "    -\n", "rules.test", 0, &error) == NULL);
  CHECK(error != NULL && strstr(error, "rules.test:1:") == error);
  nlu_string_free(error);
  CHECK(nlu_script_run_tests_file(script, "/nonexistent/rules.test", 0, &error) == NULL);
  CHECK(error != NULL && strstr(error, "/nonexistent/rules.test") != NULL);
  nlu_string_free(error);
  nlu_script_free(script);
}

static void test_missing_file(void) {
  char* error = NULL;
  int line = -1;
  nlu_script* script = nlu_script_compile_file("/nonexistent/script.nlu", &error, &line, NULL);
  CHECK(script == NULL);
  CHECK(error != NULL);
  CHECK(line == 0);
  nlu_string_free(error);
}

int main(void) {
  test_match_with_slots();
  test_match_best();
  test_explain();
  test_lint();
  test_resolve_datetime();
  test_search();
  test_save_and_load();
  test_compile_error();
  test_compile_diagnostics();
  test_run_tests();
  test_missing_file();
  printf("C API: %d failed checks\n", failures);
  return failures == 0 ? 0 : 1;
}

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
  test_compile_error();
  test_missing_file();
  printf("C API: %d failed checks\n", failures);
  return failures == 0 ? 0 : 1;
}

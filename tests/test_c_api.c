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
  test_compile_error();
  test_missing_file();
  printf("C API: %d failed checks\n", failures);
  return failures == 0 ? 0 : 1;
}

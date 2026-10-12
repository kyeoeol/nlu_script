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
    "alarm\n"
    "    wake me up at <time>[nine{hour=9}|ten{hour=10}] o'clock</time>\n";

static void test_match_with_slots(void) {
  char* error = NULL;
  nlu_script* script = nlu_script_compile(kScript, &error, NULL, NULL);
  nlu_result* result;
  CHECK(script != NULL);
  CHECK(error == NULL);
  if (!script) return;
  CHECK(nlu_script_rule_count(script) == 1);

  result = nlu_script_match(script, "Wake me up at ten o'clock");
  CHECK(nlu_result_count(result) == 1);
  CHECK(strcmp(nlu_result_label(result, 0), "alarm") == 0);
  CHECK(strcmp(nlu_result_tagged(result, 0),
               "Wake me up at <time hour=\"10\">ten o'clock</time>") == 0);
  CHECK(nlu_result_slot_count(result, 0) == 1);
  CHECK(strcmp(nlu_result_slot_name(result, 0, 0), "time") == 0);
  CHECK(strcmp(nlu_result_slot_text(result, 0, 0), "ten o'clock") == 0);
  CHECK(nlu_result_slot_begin(result, 0, 0) == 14);
  CHECK(nlu_result_slot_end(result, 0, 0) == 25);
  CHECK(nlu_result_slot_attribute_count(result, 0, 0) == 1);
  CHECK(strcmp(nlu_result_slot_attribute_key(result, 0, 0, 0), "hour") == 0);
  CHECK(strcmp(nlu_result_slot_attribute_value(result, 0, 0, 0), "10") == 0);
  CHECK(nlu_result_label(result, 1) == NULL);
  CHECK(nlu_result_slot_name(result, 0, 5) == NULL);
  nlu_result_free(result);

  result = nlu_script_match(script, "wake me up at noon");
  CHECK(nlu_result_count(result) == 0);
  nlu_result_free(result);
  nlu_script_free(script);
}

static void test_compile_error(void) {
  char* error = NULL;
  int line = -1;
  int column = -1;
  nlu_script* script = nlu_script_compile("greeting\n    hello [world\n", &error, &line, &column);
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

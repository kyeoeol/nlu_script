/* nlu_script: ANSI C interface.
 *
 * Strings are UTF-8 and NUL-terminated. Strings returned by accessor
 * functions are owned by the object they were read from and stay valid until
 * that object is freed. Byte offsets refer to the matched input text.
 */
#ifndef NLU_SCRIPT_NLU_SCRIPT_H
#define NLU_SCRIPT_NLU_SCRIPT_H

#include <stddef.h>

#if defined(_WIN32)
#  if defined(NLU_SCRIPT_BUILDING_DLL)
#    define NLU_SCRIPT_API __declspec(dllexport)
#  else
#    define NLU_SCRIPT_API
#  endif
#else
#  define NLU_SCRIPT_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nlu_script nlu_script;
typedef struct nlu_result nlu_result;
typedef struct nlu_explanation nlu_explanation;
typedef struct nlu_lint nlu_lint;
typedef struct nlu_diagnostics nlu_diagnostics;
typedef struct nlu_test_report nlu_test_report;

/* A local date and time; hour and minute are -1 when not given. */
typedef struct nlu_datetime {
  int year, month, day, hour, minute;
} nlu_datetime;

/* Compiles a script. On failure returns NULL and, if `error` is not NULL,
 * stores a message to release with nlu_string_free. For compile errors the
 * message lists every error, one per line ("line 3, column 5: ...", after
 * "path: " for errors in a file), and
 * `line` and `column` (if not NULL) receive the first one's location;
 * otherwise they receive 0. */
NLU_SCRIPT_API nlu_script* nlu_script_compile(const char* source, char** error, int* line,
                                              int* column);
NLU_SCRIPT_API nlu_script* nlu_script_compile_file(const char* path, char** error, int* line,
                                                   int* column);
/* Like nlu_script_compile and nlu_script_compile_file, with every error
 * as a structured diagnostic. On failure returns NULL and, if `diagnostics` is
 * not NULL, stores an object to release with nlu_diagnostics_free (NULL on
 * allocation failure); on success stores NULL. */
NLU_SCRIPT_API nlu_script* nlu_script_compile_diagnose(const char* source,
                                                       nlu_diagnostics** diagnostics);
NLU_SCRIPT_API nlu_script* nlu_script_compile_file_diagnose(const char* path,
                                                            nlu_diagnostics** diagnostics);
NLU_SCRIPT_API void nlu_script_free(nlu_script* script);

/* Why compiling failed. `text` is the whole message, as nlu_script_compile
 * reports it. The diagnostics are the compile errors, ordered by file (the
 * main script, then included files as they are read) and position; there are
 * none when compiling failed for another reason (a script file that cannot be
 * read). `line` is 1-based (0 if unknown), `column` 1-based in code points,
 * and `file` as in nlu_result_file. Accessors return NULL or 0 for
 * out-of-range indices. */
NLU_SCRIPT_API void nlu_diagnostics_free(nlu_diagnostics* diagnostics);
NLU_SCRIPT_API const char* nlu_diagnostics_text(const nlu_diagnostics* diagnostics);
NLU_SCRIPT_API size_t nlu_diagnostics_count(const nlu_diagnostics* diagnostics);
NLU_SCRIPT_API int nlu_diagnostics_line(const nlu_diagnostics* diagnostics, size_t i);
NLU_SCRIPT_API int nlu_diagnostics_column(const nlu_diagnostics* diagnostics, size_t i);
NLU_SCRIPT_API const char* nlu_diagnostics_message(const nlu_diagnostics* diagnostics, size_t i);
NLU_SCRIPT_API const char* nlu_diagnostics_file(const nlu_diagnostics* diagnostics, size_t i);

/* Saves a compiled script as bytes for nlu_script_load (as Script::save in
 * C++). Returns a buffer to release with nlu_string_free and stores its
 * length in `size`, or NULL on failure. */
NLU_SCRIPT_API char* nlu_script_save(const nlu_script* script, size_t* size);
/* Loads bytes from nlu_script_save. On failure returns NULL and, if `error`
 * is not NULL, stores a message to release with nlu_string_free. */
NLU_SCRIPT_API nlu_script* nlu_script_load(const char* data, size_t size, char** error);
NLU_SCRIPT_API size_t nlu_script_rule_count(const nlu_script* script);

/* Matches `text` against every rule. Returns NULL if `script` is NULL or on
 * allocation failure. Accessors return NULL or 0 for out-of-range indices. */
NLU_SCRIPT_API nlu_result* nlu_script_match(const nlu_script* script, const char* text);
/* Keeps only the labels whose most specific rule has the highest
 * specificity (see nlu_result_specificity). */
NLU_SCRIPT_API nlu_result* nlu_script_match_best(const nlu_script* script, const char* text);
/* Finds rules inside free text, as Script::search in C++: each result is a
 * span of `text` (nlu_result_begin, nlu_result_end), tagged on its own. */
NLU_SCRIPT_API nlu_result* nlu_script_search(const nlu_script* script, const char* text);
NLU_SCRIPT_API void nlu_result_free(nlu_result* result);

/* One entry per matched label. */
NLU_SCRIPT_API size_t nlu_result_count(const nlu_result* result);
NLU_SCRIPT_API const char* nlu_result_label(const nlu_result* result, size_t match);
NLU_SCRIPT_API const char* nlu_result_tagged(const nlu_result* result, size_t match);
/* 1-based script line of the rule that matched, and its script file: the
 * path given to nlu_script_compile_file or an included file's path; empty for
 * a script compiled from a string. */
NLU_SCRIPT_API int nlu_result_line(const nlu_result* result, size_t match);
NLU_SCRIPT_API const char* nlu_result_file(const nlu_result* result, size_t match);
/* Byte offsets of the matched text in the input. */
NLU_SCRIPT_API size_t nlu_result_begin(const nlu_result* result, size_t match);
NLU_SCRIPT_API size_t nlu_result_end(const nlu_result* result, size_t match);
/* Input characters, not counting spaces, that the rule matched with pattern
 * text rather than wildcards. */
NLU_SCRIPT_API int nlu_result_specificity(const nlu_result* result, size_t match);

NLU_SCRIPT_API size_t nlu_result_slot_count(const nlu_result* result, size_t match);
NLU_SCRIPT_API const char* nlu_result_slot_name(const nlu_result* result, size_t match,
                                                size_t slot);
NLU_SCRIPT_API const char* nlu_result_slot_text(const nlu_result* result, size_t match,
                                                size_t slot);
NLU_SCRIPT_API size_t nlu_result_slot_begin(const nlu_result* result, size_t match, size_t slot);
NLU_SCRIPT_API size_t nlu_result_slot_end(const nlu_result* result, size_t match, size_t slot);
NLU_SCRIPT_API size_t nlu_result_slot_attribute_count(const nlu_result* result, size_t match,
                                                      size_t slot);
NLU_SCRIPT_API const char* nlu_result_slot_attribute_key(const nlu_result* result, size_t match,
                                                         size_t slot, size_t attribute);
NLU_SCRIPT_API const char* nlu_result_slot_attribute_value(const nlu_result* result,
                                                           size_t match, size_t slot,
                                                           size_t attribute);

/* For debugging rules: the `limit` rules that got furthest into `text`, as
 * Script::explain in C++. Returns NULL if `script` is NULL or on allocation
 * failure. `matched` is a byte offset into `text`; `expected` lists what the
 * rule could have taken next ("c", "space", "any character"). Accessors
 * return NULL or 0 for out-of-range indices. */
NLU_SCRIPT_API nlu_explanation* nlu_script_explain(const nlu_script* script, const char* text,
                                                   size_t limit);
NLU_SCRIPT_API void nlu_explanation_free(nlu_explanation* explanation);
NLU_SCRIPT_API size_t nlu_explanation_count(const nlu_explanation* explanation);
NLU_SCRIPT_API const char* nlu_explanation_label(const nlu_explanation* explanation, size_t i);
NLU_SCRIPT_API int nlu_explanation_line(const nlu_explanation* explanation, size_t i);
NLU_SCRIPT_API const char* nlu_explanation_file(const nlu_explanation* explanation, size_t i);
NLU_SCRIPT_API size_t nlu_explanation_matched(const nlu_explanation* explanation, size_t i);
NLU_SCRIPT_API int nlu_explanation_complete(const nlu_explanation* explanation, size_t i);
NLU_SCRIPT_API size_t nlu_explanation_expected_count(const nlu_explanation* explanation,
                                                     size_t i);
NLU_SCRIPT_API const char* nlu_explanation_expected(const nlu_explanation* explanation, size_t i,
                                                    size_t j);

/* Lints the script: its findings as `nlu_script_cli --lint` prints them, with
 * `kind` "overlap", "dead-rule", "unused" or "directive". With `best`
 * nonzero, overlaps that match_best resolves are left out. `allowed` counts
 * overlaps that `# lint: allow-overlap` comments mark as intended, and
 * `complete` is 0 if the search stopped at its state limit. Returns NULL if
 * `script` is NULL or on allocation failure. */
NLU_SCRIPT_API nlu_lint* nlu_script_lint(const nlu_script* script, int best);
NLU_SCRIPT_API void nlu_lint_free(nlu_lint* lint);
NLU_SCRIPT_API size_t nlu_lint_count(const nlu_lint* lint);
NLU_SCRIPT_API const char* nlu_lint_kind(const nlu_lint* lint, size_t i);
NLU_SCRIPT_API const char* nlu_lint_file(const nlu_lint* lint, size_t i);
NLU_SCRIPT_API int nlu_lint_line(const nlu_lint* lint, size_t i);
NLU_SCRIPT_API const char* nlu_lint_message(const nlu_lint* lint, size_t i);
NLU_SCRIPT_API size_t nlu_lint_allowed(const nlu_lint* lint);
NLU_SCRIPT_API int nlu_lint_complete(const nlu_lint* lint);

/* Runs the cases of a script test file (see the README), as
 * Script::run_tests in C++: `tests` is the file's contents and `name` (if not
 * NULL) locates errors. With `best` nonzero, cases expect match_best output.
 * Returns NULL if `script` is NULL; on a malformed (or, for
 * nlu_script_run_tests_file, unreadable) file returns NULL and, if `error` is
 * not NULL, stores a message ("name:line: ...") to release with
 * nlu_string_free. */
NLU_SCRIPT_API nlu_test_report* nlu_script_run_tests(const nlu_script* script, const char* tests,
                                                     const char* name, int best, char** error);
NLU_SCRIPT_API nlu_test_report* nlu_script_run_tests_file(const nlu_script* script,
                                                          const char* path, int best,
                                                          char** error);
NLU_SCRIPT_API void nlu_test_report_free(nlu_test_report* report);
/* Cases that passed, and the failures, each indexed by `i` in file order:
 * the line and text of its input sentence, its expected output lines, and
 * its actual output lines with the line and file of the rule behind each (0
 * and "" for "-"). Accessors return NULL or 0 for out-of-range indices. */
NLU_SCRIPT_API size_t nlu_test_report_passed(const nlu_test_report* report);
NLU_SCRIPT_API size_t nlu_test_report_failed(const nlu_test_report* report);
NLU_SCRIPT_API int nlu_test_report_line(const nlu_test_report* report, size_t i);
NLU_SCRIPT_API const char* nlu_test_report_input(const nlu_test_report* report, size_t i);
NLU_SCRIPT_API size_t nlu_test_report_expected_count(const nlu_test_report* report, size_t i);
NLU_SCRIPT_API const char* nlu_test_report_expected(const nlu_test_report* report, size_t i,
                                                    size_t j);
NLU_SCRIPT_API size_t nlu_test_report_actual_count(const nlu_test_report* report, size_t i);
NLU_SCRIPT_API const char* nlu_test_report_actual(const nlu_test_report* report, size_t i,
                                                  size_t j);
NLU_SCRIPT_API int nlu_test_report_actual_line(const nlu_test_report* report, size_t i, size_t j);
NLU_SCRIPT_API const char* nlu_test_report_actual_file(const nlu_test_report* report, size_t i,
                                                       size_t j);

/* Resolves the attributes of a DATETIME slot (`count` key/value pairs)
 * against `now`, as nlu::resolve_datetime in C++. Fills `start`, and `end`
 * when the slot is a range, and returns 1 if it is, 0 otherwise. `grain` (if
 * not NULL) receives "year", "month", "week", "day", "hour" or "minute",
 * a static string. */
NLU_SCRIPT_API int nlu_resolve_datetime(const char* const* keys, const char* const* values,
                                        size_t count, const nlu_datetime* now,
                                        nlu_datetime* start, nlu_datetime* end,
                                        const char** grain);

NLU_SCRIPT_API void nlu_string_free(char* string);

#ifdef __cplusplus
}
#endif

#endif /* NLU_SCRIPT_NLU_SCRIPT_H */

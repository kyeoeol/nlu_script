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
NLU_SCRIPT_API void nlu_script_free(nlu_script* script);
NLU_SCRIPT_API size_t nlu_script_rule_count(const nlu_script* script);

/* Matches `text` against every rule. Returns NULL if `script` is NULL or on
 * allocation failure. Accessors return NULL or 0 for out-of-range indices. */
NLU_SCRIPT_API nlu_result* nlu_script_match(const nlu_script* script, const char* text);
/* Keeps only the labels whose most specific rule has the highest
 * specificity (see nlu_result_specificity). */
NLU_SCRIPT_API nlu_result* nlu_script_match_best(const nlu_script* script, const char* text);
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

NLU_SCRIPT_API void nlu_string_free(char* string);

#ifdef __cplusplus
}
#endif

#endif /* NLU_SCRIPT_NLU_SCRIPT_H */

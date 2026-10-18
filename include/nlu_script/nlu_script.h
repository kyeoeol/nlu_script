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

NLU_SCRIPT_API void nlu_string_free(char* string);

#ifdef __cplusplus
}
#endif

#endif /* NLU_SCRIPT_NLU_SCRIPT_H */

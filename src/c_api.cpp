#include "nlu_script/nlu_script.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "nlu_script/nlu_script.hpp"

struct nlu_script {
  nlu::Script script;
};

struct nlu_result {
  std::vector<nlu::Match> matches;
};

namespace {

char* copy_string(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (out) std::memcpy(out, s.c_str(), s.size() + 1);
  return out;
}

void report(char** error, int* line, int* column, const std::string& message, int l, int c) {
  if (error) *error = copy_string(message);
  if (line) *line = l;
  if (column) *column = c;
}

template <typename Compile>
nlu_script* compile_with(Compile compile, char** error, int* line, int* column) {
  if (error) *error = nullptr;
  if (line) *line = 0;
  if (column) *column = 0;
  try {
    return new nlu_script{compile()};
  } catch (const nlu::CompileError& e) {
    report(error, line, column, e.what(), e.line(), e.column());
  } catch (const std::exception& e) {
    report(error, line, column, e.what(), 0, 0);
  }
  return nullptr;
}

const nlu::Match* match_at(const nlu_result* r, size_t match) {
  return r && match < r->matches.size() ? &r->matches[match] : nullptr;
}

const nlu::Slot* slot_at(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Match* m = match_at(r, match);
  return m && slot < m->slots.size() ? &m->slots[slot] : nullptr;
}

const std::pair<std::string, std::string>* attribute_at(const nlu_result* r, size_t match,
                                                        size_t slot, size_t attribute) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s && attribute < s->attributes.size() ? &s->attributes[attribute] : nullptr;
}

}  // namespace

extern "C" {

nlu_script* nlu_script_compile(const char* source, char** error, int* line, int* column) {
  return compile_with([&] { return nlu::Script::compile(source ? source : ""); }, error, line,
                      column);
}

nlu_script* nlu_script_compile_file(const char* path, char** error, int* line, int* column) {
  return compile_with([&] { return nlu::Script::compile_file(path ? path : ""); }, error, line,
                      column);
}

void nlu_script_free(nlu_script* script) { delete script; }

size_t nlu_script_rule_count(const nlu_script* script) {
  return script ? script->script.rule_count() : 0;
}

nlu_result* nlu_script_match(const nlu_script* script, const char* text) {
  if (!script) return nullptr;
  try {
    return new nlu_result{script->script.match(text ? text : "")};
  } catch (const std::exception&) {
    return nullptr;
  }
}

nlu_result* nlu_script_match_best(const nlu_script* script, const char* text) {
  if (!script) return nullptr;
  try {
    return new nlu_result{script->script.match_best(text ? text : "")};
  } catch (const std::exception&) {
    return nullptr;
  }
}

void nlu_result_free(nlu_result* result) { delete result; }

size_t nlu_result_count(const nlu_result* r) { return r ? r->matches.size() : 0; }

const char* nlu_result_label(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->label.c_str() : nullptr;
}

const char* nlu_result_tagged(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->tagged.c_str() : nullptr;
}

int nlu_result_line(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->line : 0;
}

int nlu_result_specificity(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->specificity : 0;
}

size_t nlu_result_slot_count(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->slots.size() : 0;
}

const char* nlu_result_slot_name(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s ? s->name.c_str() : nullptr;
}

const char* nlu_result_slot_text(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s ? s->text.c_str() : nullptr;
}

size_t nlu_result_slot_begin(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s ? s->begin : 0;
}

size_t nlu_result_slot_end(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s ? s->end : 0;
}

size_t nlu_result_slot_attribute_count(const nlu_result* r, size_t match, size_t slot) {
  const nlu::Slot* s = slot_at(r, match, slot);
  return s ? s->attributes.size() : 0;
}

const char* nlu_result_slot_attribute_key(const nlu_result* r, size_t match, size_t slot,
                                          size_t attribute) {
  const auto* kv = attribute_at(r, match, slot, attribute);
  return kv ? kv->first.c_str() : nullptr;
}

const char* nlu_result_slot_attribute_value(const nlu_result* r, size_t match, size_t slot,
                                            size_t attribute) {
  const auto* kv = attribute_at(r, match, slot, attribute);
  return kv ? kv->second.c_str() : nullptr;
}

void nlu_string_free(char* string) { std::free(string); }

}  // extern "C"

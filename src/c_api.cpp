#include "nlu_script/nlu_script.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "datetime.hpp"
#include "nlu_script/nlu_script.hpp"

struct nlu_script {
  nlu::Script script;
};

struct nlu_result {
  std::vector<nlu::Match> matches;
};

struct nlu_explanation {
  std::vector<nlu::Explanation> items;
};

struct nlu_lint {
  std::vector<nlu::LintFinding> findings;
  size_t allowed;
  bool complete;
};

struct nlu_diagnostics {
  std::string text;
  std::vector<nlu::Diagnostic> errors;
};

struct nlu_test_report {
  nlu::TestReport report;
};

namespace {

char* copy_string(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (out) std::memcpy(out, s.c_str(), s.size() + 1);
  return out;
}

// Compiles, or records why compiling failed in `failure`.
template <typename Compile>
nlu_script* try_compile(Compile compile, nlu_diagnostics& failure) {
  try {
    try {
      return new nlu_script{compile()};
    } catch (const nlu::CompileError& e) {
      failure = nlu_diagnostics{e.what(), e.errors()};
    } catch (const std::exception& e) {
      failure = nlu_diagnostics{e.what(), {}};
    }
  } catch (const std::exception&) {  // out of memory while recording the failure
  }
  return nullptr;
}

template <typename Compile>
nlu_script* compile_with(Compile compile, char** error, int* line, int* column) {
  nlu_diagnostics failure;
  nlu_script* script = try_compile(compile, failure);
  const nlu::Diagnostic* first = failure.errors.empty() ? nullptr : &failure.errors[0];
  if (error) *error = script ? nullptr : copy_string(failure.text);
  if (line) *line = first ? first->line : 0;
  if (column) *column = first ? first->column : 0;
  return script;
}

template <typename Compile>
nlu_script* compile_diagnosed(Compile compile, nlu_diagnostics** diagnostics) {
  nlu_diagnostics failure;
  nlu_script* script = try_compile(compile, failure);
  if (diagnostics)
    *diagnostics = script ? nullptr : new (std::nothrow) nlu_diagnostics(std::move(failure));
  return script;
}

template <typename Run>
nlu_test_report* run_tests_with(const nlu_script* script, Run run, char** error) {
  if (error) *error = nullptr;
  if (!script) return nullptr;
  try {
    return new nlu_test_report{run(script->script)};
  } catch (const std::exception& e) {
    if (error) *error = copy_string(e.what());
  }
  return nullptr;
}

nlu_result* run_match(const nlu_script* script, const char* text,
                      std::vector<nlu::Match> (nlu::Script::*method)(const std::string&) const) {
  if (!script) return nullptr;
  try {
    return new nlu_result{(script->script.*method)(text ? text : "")};
  } catch (const std::exception&) {
    return nullptr;
  }
}

const nlu::Match* match_at(const nlu_result* r, size_t match) {
  return r && match < r->matches.size() ? &r->matches[match] : nullptr;
}

const nlu::Explanation* explanation_at(const nlu_explanation* e, size_t i) {
  return e && i < e->items.size() ? &e->items[i] : nullptr;
}

const nlu::LintFinding* finding_at(const nlu_lint* l, size_t i) {
  return l && i < l->findings.size() ? &l->findings[i] : nullptr;
}

const nlu::Diagnostic* diagnostic_at(const nlu_diagnostics* d, size_t i) {
  return d && i < d->errors.size() ? &d->errors[i] : nullptr;
}

const nlu::TestFailure* failure_at(const nlu_test_report* r, size_t i) {
  return r && i < r->report.failures.size() ? &r->report.failures[i] : nullptr;
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

nlu_script* nlu_script_compile_diagnose(const char* source, nlu_diagnostics** diagnostics) {
  return compile_diagnosed([&] { return nlu::Script::compile(source ? source : ""); },
                           diagnostics);
}

nlu_script* nlu_script_compile_file_diagnose(const char* path, nlu_diagnostics** diagnostics) {
  return compile_diagnosed([&] { return nlu::Script::compile_file(path ? path : ""); },
                           diagnostics);
}

void nlu_script_free(nlu_script* script) { delete script; }

void nlu_diagnostics_free(nlu_diagnostics* d) { delete d; }

const char* nlu_diagnostics_text(const nlu_diagnostics* d) { return d ? d->text.c_str() : nullptr; }

size_t nlu_diagnostics_count(const nlu_diagnostics* d) { return d ? d->errors.size() : 0; }

int nlu_diagnostics_line(const nlu_diagnostics* d, size_t i) {
  const nlu::Diagnostic* x = diagnostic_at(d, i);
  return x ? x->line : 0;
}

int nlu_diagnostics_column(const nlu_diagnostics* d, size_t i) {
  const nlu::Diagnostic* x = diagnostic_at(d, i);
  return x ? x->column : 0;
}

const char* nlu_diagnostics_message(const nlu_diagnostics* d, size_t i) {
  const nlu::Diagnostic* x = diagnostic_at(d, i);
  return x ? x->message.c_str() : nullptr;
}

const char* nlu_diagnostics_file(const nlu_diagnostics* d, size_t i) {
  const nlu::Diagnostic* x = diagnostic_at(d, i);
  return x ? x->file.c_str() : nullptr;
}

char* nlu_script_save(const nlu_script* script, size_t* size) {
  if (!script) return nullptr;
  try {
    std::string bytes = script->script.save();
    char* out = static_cast<char*>(std::malloc(bytes.size() ? bytes.size() : 1));
    if (!out) return nullptr;
    std::memcpy(out, bytes.data(), bytes.size());
    if (size) *size = bytes.size();
    return out;
  } catch (const std::exception&) {
    return nullptr;
  }
}

nlu_script* nlu_script_load(const char* data, size_t size, char** error) {
  return compile_with([&] { return nlu::Script::load(std::string(data ? data : "", data ? size : 0)); },
                      error, nullptr, nullptr);
}

size_t nlu_script_rule_count(const nlu_script* script) {
  return script ? script->script.rule_count() : 0;
}

nlu_result* nlu_script_match(const nlu_script* script, const char* text) {
  return run_match(script, text, &nlu::Script::match);
}

nlu_result* nlu_script_search(const nlu_script* script, const char* text) {
  return run_match(script, text, &nlu::Script::search);
}

nlu_result* nlu_script_match_best(const nlu_script* script, const char* text) {
  return run_match(script, text, &nlu::Script::match_best);
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

const char* nlu_result_file(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->file.c_str() : nullptr;
}

size_t nlu_result_begin(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->begin : 0;
}

size_t nlu_result_end(const nlu_result* r, size_t match) {
  const nlu::Match* m = match_at(r, match);
  return m ? m->end : 0;
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

nlu_explanation* nlu_script_explain(const nlu_script* script, const char* text, size_t limit) {
  if (!script) return nullptr;
  try {
    return new nlu_explanation{script->script.explain(text ? text : "", limit)};
  } catch (const std::exception&) {
    return nullptr;
  }
}

void nlu_explanation_free(nlu_explanation* e) { delete e; }

size_t nlu_explanation_count(const nlu_explanation* e) { return e ? e->items.size() : 0; }

const char* nlu_explanation_label(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x ? x->label.c_str() : nullptr;
}

int nlu_explanation_line(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x ? x->line : 0;
}

const char* nlu_explanation_file(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x ? x->file.c_str() : nullptr;
}

size_t nlu_explanation_matched(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x ? x->matched : 0;
}

int nlu_explanation_complete(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x && x->complete ? 1 : 0;
}

size_t nlu_explanation_expected_count(const nlu_explanation* e, size_t i) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x ? x->expected.size() : 0;
}

const char* nlu_explanation_expected(const nlu_explanation* e, size_t i, size_t j) {
  const nlu::Explanation* x = explanation_at(e, i);
  return x && j < x->expected.size() ? x->expected[j].c_str() : nullptr;
}

nlu_lint* nlu_script_lint(const nlu_script* script, int best) {
  if (!script) return nullptr;
  try {
    nlu::LintReport report = script->script.lint();
    return new nlu_lint{report.findings(best != 0), report.allowed_overlaps.size(),
                        report.complete};
  } catch (const std::exception&) {
    return nullptr;
  }
}

void nlu_lint_free(nlu_lint* l) { delete l; }

size_t nlu_lint_count(const nlu_lint* l) { return l ? l->findings.size() : 0; }

const char* nlu_lint_kind(const nlu_lint* l, size_t i) {
  const nlu::LintFinding* f = finding_at(l, i);
  return f ? f->kind.c_str() : nullptr;
}

const char* nlu_lint_file(const nlu_lint* l, size_t i) {
  const nlu::LintFinding* f = finding_at(l, i);
  return f ? f->file.c_str() : nullptr;
}

int nlu_lint_line(const nlu_lint* l, size_t i) {
  const nlu::LintFinding* f = finding_at(l, i);
  return f ? f->line : 0;
}

const char* nlu_lint_message(const nlu_lint* l, size_t i) {
  const nlu::LintFinding* f = finding_at(l, i);
  return f ? f->message.c_str() : nullptr;
}

size_t nlu_lint_allowed(const nlu_lint* l) { return l ? l->allowed : 0; }

int nlu_lint_complete(const nlu_lint* l) { return l && l->complete ? 1 : 0; }

nlu_test_report* nlu_script_run_tests(const nlu_script* script, const char* tests,
                                      const char* name, int best, char** error) {
  return run_tests_with(
      script,
      [&](const nlu::Script& s) {
        return s.run_tests(tests ? tests : "", name ? name : "tests", best != 0);
      },
      error);
}

nlu_test_report* nlu_script_run_tests_file(const nlu_script* script, const char* path, int best,
                                           char** error) {
  return run_tests_with(
      script, [&](const nlu::Script& s) { return s.run_tests_file(path ? path : "", best != 0); },
      error);
}

void nlu_test_report_free(nlu_test_report* r) { delete r; }

size_t nlu_test_report_passed(const nlu_test_report* r) {
  return r ? static_cast<size_t>(r->report.passed) : 0;
}

size_t nlu_test_report_failed(const nlu_test_report* r) {
  return r ? r->report.failures.size() : 0;
}

int nlu_test_report_line(const nlu_test_report* r, size_t i) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f ? f->line : 0;
}

const char* nlu_test_report_input(const nlu_test_report* r, size_t i) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f ? f->input.c_str() : nullptr;
}

size_t nlu_test_report_expected_count(const nlu_test_report* r, size_t i) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f ? f->expected.size() : 0;
}

const char* nlu_test_report_expected(const nlu_test_report* r, size_t i, size_t j) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f && j < f->expected.size() ? f->expected[j].c_str() : nullptr;
}

size_t nlu_test_report_actual_count(const nlu_test_report* r, size_t i) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f ? f->actual.size() : 0;
}

const char* nlu_test_report_actual(const nlu_test_report* r, size_t i, size_t j) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f && j < f->actual.size() ? f->actual[j].c_str() : nullptr;
}

int nlu_test_report_actual_line(const nlu_test_report* r, size_t i, size_t j) {
  const nlu::TestFailure* f = failure_at(r, i);
  return f && j < f->matches.size() ? f->matches[j].line : 0;
}

const char* nlu_test_report_actual_file(const nlu_test_report* r, size_t i, size_t j) {
  const nlu::TestFailure* f = failure_at(r, i);
  if (!f || j >= f->actual.size()) return nullptr;
  return j < f->matches.size() ? f->matches[j].file.c_str() : "";
}

int nlu_resolve_datetime(const char* const* keys, const char* const* values, size_t count,
                         const nlu_datetime* now, nlu_datetime* start, nlu_datetime* end,
                         const char** grain) {
  if (!now) return 0;
  std::vector<std::pair<std::string, std::string>> attributes;
  for (size_t i = 0; i < count; ++i)
    if (keys[i] && values[i]) attributes.emplace_back(keys[i], values[i]);
  nlu::DateTimeRange r = nlu::resolve_datetime(
      attributes, nlu::DateTimeValue{now->year, now->month, now->day, now->hour, now->minute});
  auto copy = [](const nlu::DateTimeValue& v, nlu_datetime* out) {
    if (out) *out = nlu_datetime{v.year, v.month, v.day, v.hour, v.minute};
  };
  copy(r.start, start);
  if (r.has_end) copy(r.end, end);
  if (grain) {
    *grain = nlu::detail::kGrains[0];
    for (const char* g : nlu::detail::kGrains)
      if (r.grain == g) *grain = g;
  }
  return r.has_end ? 1 : 0;
}

void nlu_string_free(char* string) { std::free(string); }

}  // extern "C"

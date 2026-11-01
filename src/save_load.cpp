// Saving compiled scripts as bytes and loading them again.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

#include "nlu_script/nlu_script.hpp"
#include "replay.hpp"
#include "script_impl.hpp"
#include "script_source.hpp"

namespace nlu {

using namespace detail;

namespace {

// "\x89NLU" then a line ending and a format version, so text never looks like
// a saved script and a saved script is never read by another format version.
const char kMagic[] = "\x89NLU\r\n\x1a\n";
constexpr std::uint32_t kFormatVersion = 2;  // 2: Edge::wildcard
static_assert(static_cast<unsigned>(Edge::kLastKind) < 8, "edge kinds fit in 3 bits");
static_assert(static_cast<unsigned>(kLastGuard) < 8, "guards fit in 3 bits");

// Numbers are LEB128 varints; signed ones are zigzag-encoded first, so small
// magnitudes (and -1) take one byte.
class Writer {
 public:
  void u8(unsigned v) { out_ += static_cast<char>(v & 0xff); }
  void u32(std::uint32_t v) {
    while (v >= 0x80) {
      u8((v & 0x7f) | 0x80);
      v >>= 7;
    }
    u8(v);
  }
  void i32(int v) {
    const std::uint32_t u = static_cast<std::uint32_t>(v);
    u32((u << 1) ^ (v < 0 ? 0xffffffffu : 0u));
  }
  void size(std::size_t v) { u32(static_cast<std::uint32_t>(v)); }
  void str(const std::string& s) {
    size(s.size());
    out_ += s;
  }
  std::string& out() { return out_; }

 private:
  std::string out_;
};

class Reader {
 public:
  explicit Reader(const std::string& in) : in_(in) {}
  [[noreturn]] static void fail(const std::string& what) {
    throw std::runtime_error("not a saved nlu_script script: " + what);
  }
  unsigned u8() {
    if (at_ >= in_.size()) fail("unexpected end");
    return static_cast<unsigned char>(in_[at_++]);
  }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int shift = 0;; shift += 7) {
      if (shift > 28) fail("bad number");
      const unsigned b = u8();
      v |= static_cast<std::uint32_t>(b & 0x7f) << shift;
      if (!(b & 0x80)) return v;
    }
  }
  int i32() {
    const std::uint32_t u = u32();
    return static_cast<int>((u >> 1) ^ (0u - (u & 1)));
  }
  // A count of items taking at least `item_bytes` each, checked against the
  // bytes left so corrupt counts can't ask for huge allocations.
  std::size_t count(std::size_t item_bytes) {
    std::size_t n = u32();
    if (n > (in_.size() - at_) / std::max<std::size_t>(item_bytes, 1)) fail("bad count");
    return n;
  }
  std::string str() {
    std::size_t n = count(1);
    std::string s = in_.substr(at_, n);
    at_ += n;
    return s;
  }
  bool done() const { return at_ == in_.size(); }

 private:
  const std::string& in_;
  std::size_t at_ = 0;
};

}  // namespace

std::string Script::save() const {
  const Impl& impl = *impl_;
  Writer w;
  w.out() = std::string(kMagic, sizeof kMagic - 1);
  w.u32(kFormatVersion);
  w.u8(impl.fold_width ? 1 : 0);
  w.u8(impl.nfa.has_guards ? 1 : 0);
  w.i32(impl.start);
  w.size(impl.nfa.states.size());
  for (std::size_t s = 0; s < impl.nfa.states.size(); ++s) {
    const State& st = impl.nfa.states[s];
    w.i32(st.accept_rule);
    w.size(st.edges.size());
    for (const Edge& e : st.edges) {
      w.u8(static_cast<unsigned>(e.kind) | static_cast<unsigned>(e.guard) << 3 |
           (e.wildcard ? 1u : 0u) << 6);
      w.u8(static_cast<unsigned>(e.char_class));
      w.u32(static_cast<std::uint32_t>(e.ch));
      w.i32(e.set);
      w.i32(e.action);
      w.i32(e.to - static_cast<int>(s));  // usually the next few states
    }
  }
  w.size(impl.nfa.sets.size());
  for (const auto& set : impl.nfa.sets) {
    w.size(set.size());
    for (char32_t c : set) w.u32(static_cast<std::uint32_t>(c));
  }
  w.size(impl.nfa.actions.size());
  for (const Action& a : impl.nfa.actions) {
    w.u8(static_cast<unsigned>(a.kind));
    w.i32(a.slot);
    w.str(a.key);
    w.str(a.value);
    w.u8(static_cast<unsigned>(a.normalizer));
  }
  w.size(impl.nfa.slot_names.size());
  for (const std::string& name : impl.nfa.slot_names) w.str(name);
  w.size(impl.rules.size());
  for (const CompiledRule& r : impl.rules) {
    w.str(r.label);
    w.i32(r.line);
    w.str(r.file);
    w.i32(r.start);
    w.i32(r.first);
    w.i32(r.last);
  }
  w.size(impl.unused.size());
  for (const UnusedDefinition& d : impl.unused) {
    w.str(d.name);
    w.i32(d.line);
    w.str(d.file);
  }
  w.size(impl.files.size());
  for (const std::string& f : impl.files) w.str(f);
  w.size(impl.lint_directives.size());
  for (const LintDirective& d : impl.lint_directives) {
    w.size(d.file);
    w.i32(d.line);
    w.i32(d.column);
    w.str(d.command);
    w.size(d.args.size());
    for (const std::string& a : d.args) w.str(a);
  }
  return std::move(w.out());
}

Script Script::load(const std::string& bytes) {
  if (bytes.compare(0, sizeof kMagic - 1, kMagic) != 0) Reader::fail("no header");
  const std::string body = bytes.substr(sizeof kMagic - 1);
  Reader r(body);
  if (r.u32() != kFormatVersion) Reader::fail("saved by another format version");
  auto impl = std::make_unique<Impl>();
  impl->fold_width = r.u8() != 0;
  impl->nfa.has_guards = r.u8() != 0;
  impl->start = r.i32();
  const std::size_t states = r.count(2);
  impl->nfa.states.resize(states);
  for (std::size_t s = 0; s < states; ++s) {
    State& st = impl->nfa.states[s];
    st.accept_rule = r.i32();
    const std::size_t edges = r.count(6);
    st.edges.reserve(edges);
    for (std::size_t k = 0; k < edges; ++k) {
      Edge e;
      const unsigned head = r.u8(), cls = r.u8();
      const unsigned kind = head & 7, guard = head >> 3 & 7, wildcard = head >> 6;
      if (kind > static_cast<unsigned>(Edge::kLastKind) || wildcard > 1 ||
          guard > static_cast<unsigned>(kLastGuard) || cls > static_cast<unsigned>(kLastCharClass))
        Reader::fail("bad edge");
      e.kind = static_cast<Edge::Kind>(kind);
      e.guard = static_cast<Guard>(guard);
      e.char_class = static_cast<CharClass>(cls);
      e.wildcard = wildcard != 0;
      e.ch = static_cast<char32_t>(r.u32());
      e.set = r.i32();
      e.action = r.i32();
      e.to = static_cast<int>(s) + r.i32();
      st.edges.push_back(e);
    }
  }
  impl->nfa.sets.resize(r.count(1));
  for (auto& set : impl->nfa.sets) {
    set.resize(r.count(1));
    for (char32_t& c : set) c = static_cast<char32_t>(r.u32());
    if (!std::is_sorted(set.begin(), set.end())) Reader::fail("bad set");
  }
  impl->nfa.actions.resize(r.count(5));
  for (Action& a : impl->nfa.actions) {
    const unsigned kind = r.u8();
    if (kind > static_cast<unsigned>(Action::kLastKind)) Reader::fail("bad action");
    a.kind = static_cast<Action::Kind>(kind);
    a.slot = r.i32();
    a.key = r.str();
    a.value = r.str();
    const unsigned normalizer = r.u8();
    if (normalizer > static_cast<unsigned>(kLastNormalizer)) Reader::fail("bad action");
    a.normalizer = static_cast<Normalizer>(normalizer);
  }
  impl->nfa.slot_names.resize(r.count(1));
  for (std::string& name : impl->nfa.slot_names) name = r.str();
  impl->rules.resize(r.count(6));
  for (CompiledRule& rule : impl->rules) {
    rule.label = r.str();
    rule.line = r.i32();
    rule.file = r.str();
    rule.start = r.i32();
    rule.first = r.i32();
    rule.last = r.i32();
  }
  impl->unused.resize(r.count(3));
  for (UnusedDefinition& d : impl->unused) {
    d.name = r.str();
    d.line = r.i32();
    d.file = r.str();
  }
  const std::size_t files = r.count(1);
  for (std::size_t k = 0; k < files; ++k) impl->files.push_back(r.str());
  impl->lint_directives.resize(r.count(5));
  for (LintDirective& d : impl->lint_directives) {
    d.file = r.u32();
    if (d.file >= impl->files.size()) Reader::fail("bad lint comment");
    d.line = r.i32();
    d.column = r.i32();
    d.command = r.str();
    d.args.resize(r.count(1));
    for (std::string& a : d.args) a = r.str();
  }
  if (!r.done()) Reader::fail("trailing bytes");

  // Matching relies on these: the start state comes first, rules own
  // contiguous ranges of states after it in order, and edges stay within
  // their rule; every set, action and slot exists.
  const Nfa& nfa = impl->nfa;
  const int n = static_cast<int>(nfa.states.size());
  if (impl->start != 0 || n == 0) Reader::fail("bad start");
  int next_first = 1;
  for (std::size_t id = 0; id < impl->rules.size(); ++id) {
    CompiledRule& rule = impl->rules[id];
    if (rule.first != next_first || rule.last <= rule.first || rule.last > n ||
        rule.start < rule.first || rule.start >= rule.last)
      Reader::fail("bad rule");
    for (int s = rule.first; s < rule.last; ++s) {
      const State& st = nfa.states[s];
      if (st.accept_rule != -1 && st.accept_rule != static_cast<int>(id)) Reader::fail("bad state");
      for (const Edge& e : st.edges) {
        if (e.to < rule.first || e.to >= rule.last) Reader::fail("bad edge");
        if (e.kind == Edge::Kind::Any && e.set >= static_cast<int>(nfa.sets.size()))
          Reader::fail("bad edge");
        if (e.kind == Edge::Kind::Action &&
            (e.action < 0 || e.action >= static_cast<int>(nfa.actions.size())))
          Reader::fail("bad edge");
      }
    }
    rule.needs_vm = needs_pike_vm(nfa, rule.first, rule.last);
    next_first = rule.last;
  }
  if (next_first != n) Reader::fail("bad rule");
  for (const Edge& e : nfa.states[0].edges) {
    if (e.kind != Edge::Kind::Epsilon || e.to < 1 || e.to >= n) Reader::fail("bad start");
  }
  if (nfa.states[0].accept_rule != -1) Reader::fail("bad start");
  for (const Action& a : nfa.actions) {
    const bool unqualified = a.kind == Action::Kind::Assign && a.slot == -1;
    if (!unqualified && (a.slot < 0 || a.slot >= static_cast<int>(nfa.slot_names.size())))
      Reader::fail("bad action");
  }
  impl->index_rules();
  impl->dfa = impl->make_dfa(impl->cache_options);
  return Script(std::move(impl));
}

void Script::save_file(const std::string& path) const {
  std::ofstream out(path, std::ios::binary);
  out << save();
  if (!out) throw std::runtime_error("cannot write " + path);
}

Script Script::load_file(const std::string& path) {
  std::string bytes;
  if (!read_file(path, bytes)) throw std::runtime_error("cannot open " + path);
  return load(bytes);
}

}  // namespace nlu

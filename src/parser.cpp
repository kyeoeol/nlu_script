#include "parser.hpp"

#include <memory>

#include "nlu_script/nlu_script.hpp"
#include "unicode.hpp"

namespace nlu {
namespace detail {

namespace {

constexpr int kMaxRepeat = 100;
constexpr std::size_t kMaxPermuteTokens = 5;
// Groups nest by recursion, so their depth is bounded to keep the stack safe.
constexpr int kMaxDepth = 100;

std::string utf8(char32_t c) {
  std::string out;
  append_utf8(c, out);
  return out;
}

std::string quote(char32_t c) { return "'" + utf8(c) + "'"; }

// Korean particles whose form depends on the preceding syllable: the first
// form follows a final consonant, the second a vowel.
struct ParticlePair {
  const char32_t* after_consonant;
  const char32_t* after_vowel;
  bool rieul_as_vowel;  // a final ㄹ takes the vowel form (서울로)
};

const ParticlePair kParticlePairs[] = {
    {U"은", U"는", false},     {U"이", U"가", false},     {U"을", U"를", false},
    {U"과", U"와", false},     {U"아", U"야", false},     {U"이나", U"나", false},
    {U"이랑", U"랑", false},   {U"이라고", U"라고", false}, {U"이에요", U"예요", false},
    {U"으로", U"로", true},    {U"으로서", U"로서", true},  {U"으로써", U"로써", true},
};

std::string particle_pair_list() {
  std::string out;
  for (const ParticlePair& p : kParticlePairs) {
    if (!out.empty()) out += ", ";
    out += to_utf8(p.after_consonant) + "/" + to_utf8(p.after_vowel);
  }
  return out;
}

bool ends_with(const std::u32string& s, const std::u32string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class Parser {
 public:
  Parser(const std::u32string& text, const SourcePos& start, bool width_folding)
      : text_(text),
        line_(start.line),
        column_(start.column),
        file_(start.file),
        width_folding_(width_folding) {}

  NodePtr parse() {
    std::shared_ptr<Node> node = parse_alternation(false);
    skip_spaces();
    if (!at_end()) unexpected();
    return node;
  }

 private:
  const std::u32string& text_;
  int line_;
  int column_;
  const std::string* file_;
  bool width_folding_;
  std::size_t i_ = 0;
  int depth_ = 0;  // groups open around the current position

  // --- helpers ---------------------------------------------------------

  bool at_end() const { return i_ >= text_.size(); }
  char32_t peek(std::size_t ahead = 0) const {
    return i_ + ahead < text_.size() ? text_[i_ + ahead] : U'\0';
  }
  bool looking_at(const char32_t* s) const {
    for (std::size_t k = 0; s[k]; ++k)
      if (peek(k) != s[k]) return false;
    return true;
  }
  SourcePos pos_at(std::size_t index) const {
    return SourcePos{line_, column_ + static_cast<int>(index), file_};
  }
  [[noreturn]] void error_at(std::size_t index, const std::string& message) const {
    throw_at(pos_at(index), message);
  }
  [[noreturn]] void error(const std::string& message) const { error_at(i_, message); }
  [[noreturn]] void unexpected() const {
    if (at_end()) error("unexpected end of pattern");
    char32_t c = peek();
    if (c == U']' || c == U')') error("unmatched " + quote(c));
    if (is_special(c))
      error("unexpected special symbol " + quote(c) + "; quote it as \"" + utf8(c) +
            "\" to match it literally");
    error("unexpected " + quote(c));
  }
  void expect(char32_t c, const char* what) {
    if (peek() != c) error(std::string("expected ") + quote(c) + " " + what);
    ++i_;
  }
  bool skip_spaces() {
    std::size_t start = i_;
    while (!at_end() && is_space(peek())) ++i_;
    return i_ > start;
  }
  std::string read_name(const char* what) {
    std::size_t start = i_;
    while (!at_end() && is_name_char(peek())) ++i_;
    if (i_ == start) error(std::string("expected ") + what);
    return to_utf8(text_.substr(start, i_ - start));
  }
  int read_int() {
    skip_spaces();
    std::size_t start = i_;
    long value = 0;
    while (peek() >= U'0' && peek() <= U'9') {
      value = value * 10 + (peek() - U'0');
      if (value > 1000000) error("number too large");
      ++i_;
    }
    if (i_ == start) error("expected a number");
    skip_spaces();
    return static_cast<int>(value);
  }
  std::shared_ptr<Node> make(Node::Kind kind, std::size_t at) const {
    auto node = std::make_shared<Node>();
    node->kind = kind;
    node->pos = pos_at(at);
    return node;
  }

  // A sequence or alternation stops before one of these.
  bool at_stop(bool in_permute) const {
    char32_t c = peek();
    return at_end() || c == U']' || c == U')' || c == U'|' || (in_permute && c == U',') ||
           (c == U'<' && peek(1) == U'/');
  }

  // --- grammar ---------------------------------------------------------

  // alternation := sequence ('|' sequence)*
  std::shared_ptr<Node> parse_alternation(bool in_permute) {
    struct Depth {
      int& depth;
      ~Depth() { --depth; }
    } depth{++depth_};
    if (depth_ > kMaxDepth)
      error("groups are nested too deeply (over " + std::to_string(kMaxDepth) + " levels)");
    std::size_t start = i_;
    std::vector<std::shared_ptr<Node>> options;
    options.push_back(parse_sequence(in_permute));
    while (peek() == U'|') {
      ++i_;
      options.push_back(parse_sequence(in_permute));
    }
    if (options.size() == 1) return options[0];
    auto choice = make(Node::Kind::Choice, start);
    choice->children.assign(options.begin(), options.end());
    return choice;
  }

  // sequence := item ((space | '::' | nothing) item)*
  std::shared_ptr<Node> parse_sequence(bool in_permute) {
    auto seq = make(Node::Kind::Sequence, i_);
    std::vector<std::shared_ptr<Node>> items;
    bool saw_space = false;
    bool hard = false;
    for (;;) {
      saw_space = skip_spaces() || saw_space;
      if (at_stop(in_permute)) break;
      if (looking_at(U"::")) {
        if (items.empty()) error("'::' must join two tokens");
        if (saw_space) error("'::' must not be preceded by a space");
        i_ += 2;
        if (at_end() || is_space(peek()) || at_stop(in_permute))
          error("'::' must be directly followed by a token");
        hard = true;
        continue;
      }
      if (peek() == U'{') {
        std::size_t at = i_;
        std::vector<Assignment> assignments = parse_assignments();
        if (items.empty()) items.push_back(make(Node::Kind::Literal, at));
        for (Assignment& a : assignments) items.back()->assignments.push_back(std::move(a));
        saw_space = false;
        continue;
      }
      std::shared_ptr<Node> item = parse_item(in_permute);
      if (!items.empty()) seq->hard_joins.push_back(hard);
      items.push_back(item);
      hard = false;
      saw_space = false;
    }
    if (hard) error("'::' must be directly followed by a token");
    if (items.size() == 1) return items[0];
    seq->children.assign(items.begin(), items.end());
    return seq;
  }

  std::shared_ptr<Node> parse_item(bool in_permute) {
    std::size_t start = i_;
    char32_t c = peek();
    switch (c) {
      case U'[': {
        ++i_;
        std::shared_ptr<Node> body = parse_alternation(false);
        expect(U']', "to close '['");
        if (peek() == U'^') return parse_repeat(body, start);
        return body;
      }
      case U'(': {
        ++i_;
        std::shared_ptr<Node> body = parse_alternation(false);
        expect(U')', "to close '('");
        auto opt = make(Node::Kind::Optional, start);
        opt->children.push_back(body);
        return opt;
      }
      case U'~':
        return parse_permute();
      case U'<':
        if (peek(1) == U'?') return parse_wildcard();
        return parse_slot();
      case U'\\':
        return parse_exclusion();
      case U'@':
        return parse_reference();
      case U'`': {
        if (!looking_at(U"`@{")) error("expected '`@{' to refer to a system entity");
        i_ += 3;
        auto ref = make(Node::Kind::SystemRef, start);
        ref->name = read_name("a system entity name");
        expect(U'}', "to close '`@{'");
        return ref;
      }
      default:
        break;
    }
    if (c == U'"' || (!is_special(c) && !is_space(c) && !(in_permute && c == U',')))
      return parse_literal(in_permute);
    unexpected();
  }

  char32_t fold(char32_t c) const { return fold_case(width_folding_ ? fold_width(c) : c); }

  bool literal_char(char32_t c, bool in_permute) const {
    return !is_special(c) && !is_space(c) && !(in_permute && c == U',');
  }

  std::shared_ptr<Node> parse_literal(bool in_permute) {
    const std::size_t start = i_;
    auto lit = make(Node::Kind::Literal, start);
    while (!at_end()) {
      char32_t c = peek();
      if (c == U'/' && !lit->text.empty()) return parse_particle(lit, start, in_permute);
      if (c == U'"') {
        std::size_t open = i_++;
        while (!at_end() && peek() != U'"') {
          char32_t q = peek();
          if (is_space(q)) {
            if (lit->text.empty() || lit->text.back() != U' ') lit->text += U' ';
          } else {
            lit->text += fold(q);
          }
          ++i_;
        }
        if (at_end()) error_at(open, "unterminated quote");
        ++i_;
      } else if (literal_char(c, in_permute)) {
        lit->text += fold(c);
        ++i_;
      } else {
        break;
      }
    }
    return lit;
  }

  // A particle pair such as 을/를 ends the literal `lit`, which holds the text
  // up to the '/' (e.g. 적금을). Returns the particle, after the rest of the
  // literal if there is any, joined without a space.
  std::shared_ptr<Node> parse_particle(std::shared_ptr<Node> lit, std::size_t start,
                                       bool in_permute) {
    const std::size_t slash = i_++;
    std::u32string after;
    while (!at_end() && literal_char(peek(), in_permute)) after += text_[i_++];
    const ParticlePair* pair = nullptr;
    bool written_vowel_first = false;
    for (const ParticlePair& p : kParticlePairs) {
      if (after == p.after_vowel && ends_with(lit->text, p.after_consonant)) {
        pair = &p;
      } else if (after == p.after_consonant && ends_with(lit->text, p.after_vowel)) {
        pair = &p;
        written_vowel_first = true;
      }
      if (pair) break;
    }
    if (!pair)
      error_at(slash, "unknown particle pair ending in '/" + to_utf8(after) +
                          "'; available: " + particle_pair_list());
    const std::u32string before =
        written_vowel_first ? pair->after_vowel : pair->after_consonant;
    auto particle = make(Node::Kind::Particle, slash - before.size());
    particle->text = pair->after_consonant;
    particle->vowel_text = pair->after_vowel;
    particle->rieul_as_vowel = pair->rieul_as_vowel;
    lit->text.erase(lit->text.size() - before.size());
    if (lit->text.empty()) return particle;
    auto seq = make(Node::Kind::Sequence, start);
    seq->children = {lit, particle};
    seq->hard_joins = {true};
    return seq;
  }

  // [body]^(min,max) | [body]^<n | [body]^>n
  std::shared_ptr<Node> parse_repeat(std::shared_ptr<Node> body, std::size_t start) {
    ++i_;  // '^'
    auto rep = make(Node::Kind::Repeat, start);
    rep->children.push_back(body);
    std::size_t spec = i_;
    if (peek() == U'(') {
      ++i_;
      rep->min = read_int();
      expect(U',', "between repetition bounds");
      rep->max = read_int();
      expect(U')', "to close the repetition range");
    } else if (peek() == U'<') {
      ++i_;
      int n = read_int();
      rep->min = 1;
      rep->max = n - 1;
    } else if (peek() == U'>') {
      ++i_;
      int n = read_int();
      rep->min = n + 1;
      rep->max = -1;
    } else {
      error("expected '(min,max)', '<n' or '>n' after '^'");
    }
    if (rep->max != -1 && (rep->max < 1 || rep->max < rep->min))
      error_at(spec, "repetition range matches nothing");
    if (rep->min > kMaxRepeat || rep->max > kMaxRepeat)
      error_at(spec, "repetition count above " + std::to_string(kMaxRepeat));
    return rep;
  }

  // ~[a, b, c]~
  std::shared_ptr<Node> parse_permute() {
    std::size_t start = i_;
    if (!looking_at(U"~[")) unexpected();
    i_ += 2;
    auto perm = make(Node::Kind::Permute, start);
    for (;;) {
      skip_spaces();
      std::size_t token = i_;
      std::shared_ptr<Node> body = parse_alternation(true);
      if (body->kind == Node::Kind::Sequence && body->children.empty())
        error_at(token, "empty token in '~[ ]~'");
      perm->children.push_back(body);
      if (peek() == U',') {
        ++i_;
        continue;
      }
      if (looking_at(U"]~")) {
        i_ += 2;
        break;
      }
      error("expected ',' or ']~' in '~[ ]~'");
    }
    if (perm->children.size() > kMaxPermuteTokens)
      error_at(start, "'~[ ]~' takes at most " + std::to_string(kMaxPermuteTokens) + " tokens");
    return perm;
  }

  // <?>, <?>*?, <?>+?
  std::shared_ptr<Node> parse_wildcard() {
    std::size_t start = i_;
    if (!looking_at(U"<?>")) error("expected '<?>'");
    i_ += 3;
    auto wild = make(Node::Kind::Wildcard, start);
    if (peek() == U'*' || peek() == U'+') {
      wild->wild = peek() == U'*' ? Node::WildKind::Star : Node::WildKind::Plus;
      ++i_;
      if (peek() != U'?') error("expected '?' after '<?>*' or '<?>+'");
      ++i_;
    }
    return wild;
  }

  // <name> body </name>
  std::shared_ptr<Node> parse_slot() {
    std::size_t start = i_;
    ++i_;  // '<'
    auto slot = make(Node::Kind::Slot, start);
    slot->name = read_name("a slot name after '<'");
    expect(U'>', "to close the slot tag");
    slot->children.push_back(parse_alternation(false));
    if (!looking_at(U"</")) error_at(start, "slot <" + slot->name + "> is not closed");
    i_ += 2;
    std::size_t close = i_;
    std::string name = read_name("a slot name after '</'");
    if (name != slot->name)
      error_at(close, "closing tag </" + name + "> does not match <" + slot->name + ">");
    expect(U'>', "to close the slot tag");
    return slot;
  }

  // \X <?>*? : the wildcard must not match X in full. Several exclusions may
  // precede one wildcard.
  std::shared_ptr<Node> parse_exclusion() {
    std::size_t start = i_;
    auto excluded = make(Node::Kind::Choice, start);
    while (peek() == U'\\') {
      ++i_;
      if (at_end() || is_space(peek())) error("expected a token after '\\'");
      excluded->children.push_back(parse_item(false));
      skip_spaces();
    }
    if (!looking_at(U"<?>")) {
      if (peek() == U'<' && peek(1) != U'/')
        error_at(start, "an exclusion '\\' must be followed directly by a wildcard '<?>'; "
                        "to capture the wildcard, put the exclusion inside the slot: "
                        "<slot>\\[...] <?>*?</slot>");
      error_at(start, "an exclusion '\\' must be followed by a wildcard '<?>'");
    }
    std::shared_ptr<Node> wild = parse_wildcard();
    wild->exclusion = excluded;
    return wild;
  }

  // @{entity} or @var.{variable}
  std::shared_ptr<Node> parse_reference() {
    std::size_t start = i_;
    if (looking_at(U"@{")) {
      i_ += 2;
      auto ref = make(Node::Kind::EntityRef, start);
      ref->name = read_name("an entity name");
      expect(U'}', "to close '@{'");
      return ref;
    }
    if (looking_at(U"@var.{")) {
      i_ += 6;
      auto ref = make(Node::Kind::VarRef, start);
      ref->name = read_name("a variable name");
      expect(U'}', "to close '@var.{'");
      return ref;
    }
    error("expected '@{entity}' or '@var.{variable}'");
  }

  // {key=value, slot.key=value}
  std::vector<Assignment> parse_assignments() {
    ++i_;  // '{'
    std::vector<Assignment> out;
    for (;;) {
      skip_spaces();
      Assignment a;
      a.pos = pos_at(i_);
      a.key = read_name("an attribute name");
      if (peek() == U'.') {
        ++i_;
        a.slot = a.key;
        a.key = read_name("an attribute name after '.'");
      }
      skip_spaces();
      expect(U'=', "after the attribute name");
      skip_spaces();
      if (peek() == U'"') {
        std::size_t open = i_++;
        std::size_t begin = i_;
        while (!at_end() && peek() != U'"') ++i_;
        if (at_end()) error_at(open, "unterminated quote");
        a.value = to_utf8(text_.substr(begin, i_ - begin));
        ++i_;
      } else {
        std::size_t begin = i_;
        while (!at_end() && !is_space(peek()) && peek() != U',' && peek() != U'}') ++i_;
        if (i_ == begin) error("expected an attribute value");
        a.value = to_utf8(text_.substr(begin, i_ - begin));
      }
      out.push_back(std::move(a));
      skip_spaces();
      if (peek() == U',') {
        ++i_;
        continue;
      }
      expect(U'}', "to close the attribute assignment");
      return out;
    }
  }
};

}  // namespace

bool is_special(char32_t c) {
  switch (c) {
    case U'(': case U')': case U'{': case U'}': case U'[': case U']':
    case U'<': case U'>': case U'\\': case U':': case U'+': case U'*':
    case U'^': case U'~': case U'=': case U'.': case U'|': case U'&':
    case U'-': case U'/': case U'`':
      return true;
    default:
      return false;
  }
}

bool is_name_char(char32_t c) {
  return !is_special(c) && !is_space(c) && c != U'"' && c != U',' && c != U'?';
}

void throw_at(const SourcePos& pos, const std::string& message) {
  throw CompileError({{pos.line, pos.column, message, pos.file ? *pos.file : std::string()}});
}

NodePtr parse_pattern(const std::u32string& text, const SourcePos& start, bool width_folding) {
  return Parser(text, start, width_folding).parse();
}

std::vector<std::pair<std::size_t, std::u32string>> split_top_level_commas(
    const std::u32string& text) {
  std::vector<std::pair<std::size_t, std::u32string>> parts;
  int depth = 0;
  bool quoted = false;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    char32_t c = i < text.size() ? text[i] : U',';
    if (quoted) {
      if (c == U'"') quoted = false;
      if (i < text.size()) continue;
    }
    if (c == U'"') {
      quoted = true;
    } else if (c == U'[' || c == U'(' || c == U'{') {
      ++depth;
    } else if (c == U']' || c == U')' || c == U'}') {
      --depth;
    } else if (c == U',' && (depth <= 0 || i == text.size())) {
      parts.emplace_back(start, text.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

}  // namespace detail
}  // namespace nlu

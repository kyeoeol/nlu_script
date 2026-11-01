// Pike VM replays and building matches from them.
#include "replay.hpp"

#include "datetime.hpp"
#include "numbers.hpp"
#include "script_impl.hpp"

namespace nlu {
namespace detail {

bool needs_pike_vm(const Nfa& nfa, int first, int last) {
  for (int s = first; s < last; ++s)
    for (const Edge& e : nfa.states[s].edges)
      if (e.kind == Edge::Kind::Action || e.wildcard) return true;
  return false;
}

namespace {

struct Instance {
  int slot;
  int open_pos;
  int close_pos;
  std::size_t open_event;
  Attributes attributes;
  Normalizer normalizer;
  int parent;  // the instance this one is nested in, or -1
};

std::string escape_attribute(const std::string& value) {
  std::string out;
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

Match build_match(const Nfa& nfa, const CompiledRule& rule, int specificity,
                  const std::vector<Event>& events, const std::string& original,
                  const NormalizedText& norm, std::size_t begin, std::size_t end) {
  std::vector<Instance> instances;
  std::vector<int> event_instance(events.size(), -1);
  std::vector<int> open;
  std::vector<std::size_t> deferred;  // qualified assignments
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Action& a = nfa.actions[events[i].action];
    switch (a.kind) {
      case Action::Kind::Open:
        instances.push_back({a.slot, events[i].pos, events[i].pos, i, {}, a.normalizer,
                             open.empty() ? -1 : open.back()});
        open.push_back(static_cast<int>(instances.size()) - 1);
        event_instance[i] = open.back();
        break;
      case Action::Kind::Close:
        if (open.empty()) break;  // only in a corrupt loaded script
        instances[open.back()].close_pos = events[i].pos;
        event_instance[i] = open.back();
        open.pop_back();
        break;
      case Action::Kind::Assign:
        if (a.slot < 0) {
          if (!open.empty()) set_attribute(instances[open.back()].attributes, a.key, a.value);
        } else {
          deferred.push_back(i);
        }
        break;
    }
  }
  // {slot.key=value} targets the latest such slot opened before it, or else
  // the first one opened after it.
  for (std::size_t i : deferred) {
    const Action& a = nfa.actions[events[i].action];
    int target = -1;
    for (std::size_t k = 0; k < instances.size(); ++k) {
      if (instances[k].slot != a.slot) continue;
      if (instances[k].open_event < i || target < 0) target = static_cast<int>(k);
      if (instances[k].open_event > i) break;
    }
    if (target >= 0) set_attribute(instances[target].attributes, a.key, a.value);
  }

  // A slot leaves out a space at either end, which a skipped optional group
  // leaves there (`<s>a (b)</s> c` takes `a` in `a c`), and stays within the
  // slot it is nested in (listed before it).
  for (Instance& inst : instances) {
    while (inst.open_pos < inst.close_pos && norm.text[inst.open_pos] == U' ') ++inst.open_pos;
    while (inst.close_pos > inst.open_pos && norm.text[inst.close_pos - 1] == U' ') --inst.close_pos;
    if (inst.parent >= 0) {
      const Instance& outer = instances[inst.parent];
      inst.open_pos = std::clamp(inst.open_pos, outer.open_pos, outer.close_pos);
      inst.close_pos = std::clamp(inst.close_pos, outer.open_pos, outer.close_pos);
    }
  }

  const std::size_t n = norm.text.size();
  auto byte_at = [&](int pos) -> std::size_t {
    if (static_cast<std::size_t>(pos) < n) return norm.begin[pos];
    return n ? norm.end[n - 1] : 0;
  };
  auto open_byte = [&](const Instance& inst) { return byte_at(inst.open_pos); };
  auto close_byte = [&](const Instance& inst) {
    return inst.close_pos > inst.open_pos ? norm.end[inst.close_pos - 1] : open_byte(inst);
  };

  // System entity values are read from the text as normalized for matching
  // (composed, case-folded, and width-folded with `option fold-width`), the
  // text their grammars matched.
  std::string value;
  for (Instance& inst : instances) {
    if (inst.normalizer == Normalizer::None) continue;
    const std::string text = to_utf8(
        norm.text.substr(static_cast<std::size_t>(inst.open_pos),
                         static_cast<std::size_t>(inst.close_pos - inst.open_pos)));
    std::string currency;
    if ((inst.normalizer == Normalizer::KoreanNumber && korean_number_value(text, value)) ||
        (inst.normalizer == Normalizer::EnglishNumber && english_number_value(text, value)) ||
        (inst.normalizer == Normalizer::Ordinal && ordinal_value(text, value)))
      set_attribute(inst.attributes, "value", value);
    if (inst.normalizer == Normalizer::Money && money_value(text, value, currency)) {
      set_attribute(inst.attributes, "value", value);
      set_attribute(inst.attributes, "currency", currency);
    }
    if (inst.normalizer == Normalizer::DateTime)
      for (const auto& kv : datetime_attributes(text))
        set_attribute(inst.attributes, kv.first, kv.second);
  }

  Match m;
  m.label = rule.label;
  m.line = rule.line;
  m.file = rule.file;
  m.begin = begin;
  m.end = end;
  m.specificity = specificity;
  for (const Instance& inst : instances) {
    Slot s;
    s.name = nfa.slot_names[inst.slot];
    s.begin = open_byte(inst);
    s.end = close_byte(inst);
    s.text = original.substr(s.begin, s.end - s.begin);
    s.attributes = inst.attributes;
    m.slots.push_back(std::move(s));
  }

  std::size_t cursor = begin;
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (event_instance[i] < 0) continue;
    const Instance& inst = instances[event_instance[i]];
    const bool is_open = nfa.actions[events[i].action].kind == Action::Kind::Open;
    std::size_t at = is_open ? open_byte(inst) : close_byte(inst);
    m.tagged.append(original, cursor, at - cursor);
    cursor = at;
    const std::string& name = nfa.slot_names[inst.slot];
    if (is_open) {
      m.tagged += "<" + name;
      for (const auto& kv : inst.attributes)
        m.tagged += " " + kv.first + "=\"" + escape_attribute(kv.second) + "\"";
      m.tagged += ">";
    } else {
      m.tagged += "</" + name + ">";
    }
  }
  m.tagged.append(original, cursor, end - cursor);
  return m;
}

}  // namespace detail
}  // namespace nlu

#include "system_entities.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace nlu {
namespace detail {

namespace {

// --- building blocks --------------------------------------------------------

std::shared_ptr<Node> node(Node::Kind kind) {
  auto n = std::make_shared<Node>();
  n->kind = kind;
  return n;
}

NodePtr literal(const std::u32string& text) {
  auto n = node(Node::Kind::Literal);
  n->text = text;
  return n;
}

NodePtr choice(std::vector<NodePtr> options) {
  auto n = node(Node::Kind::Choice);
  n->children = std::move(options);
  return n;
}

NodePtr optional(NodePtr body) {
  auto n = node(Node::Kind::Optional);
  n->children.push_back(std::move(body));
  return n;
}

NodePtr digit() {
  std::vector<NodePtr> digits;
  for (char32_t c = U'0'; c <= U'9'; ++c) digits.push_back(literal(std::u32string(1, c)));
  return choice(std::move(digits));
}

// `body` repeated `min` to `max` (-1: unbounded) times, without spaces.
NodePtr repeated(NodePtr body, int min, int max) {
  auto n = node(Node::Kind::Repeat);
  n->children.push_back(std::move(body));
  n->min = min;
  n->max = max;
  n->soft_repeat = false;
  return n;
}

NodePtr digits(int min, int max) { return repeated(digit(), min, max); }

// Tokens joined without spaces.
NodePtr glued(std::vector<NodePtr> items) {
  auto n = node(Node::Kind::Sequence);
  n->hard_joins.assign(items.size() - 1, true);
  n->children = std::move(items);
  return n;
}

// Tokens that may be separated by a space.
NodePtr spaced(std::vector<NodePtr> items) {
  auto n = node(Node::Kind::Sequence);
  n->hard_joins.assign(items.size() - 1, false);
  n->children = std::move(items);
  return n;
}

NodePtr literals(const char32_t* const* words) {
  std::vector<NodePtr> options;
  for (; *words; ++words) options.push_back(literal(*words));
  return choice(std::move(options));
}

// Numbers written with digits and Korean numerals: 35000, 35,000, 3만 5천,
// 삼만오천, 2,500만, 1억 2천만. Below 만 a number is either digits or
// numerals (with single digits allowed for numerals: 3천5백); 만, 억 and 조
// groups may be separated by spaces. A space only ever separates two parts,
// so the entity never ends in one.
NodePtr korean_number() {
  static const char32_t* const kSinoDigits[] = {U"일", U"이", U"삼", U"사", U"오",
                                                U"육", U"칠", U"팔", U"구", nullptr};
  static const char32_t* const kDigits[] = {U"1", U"2", U"3", U"4", U"5", U"6", U"7",
                                            U"8", U"9", nullptr};
  NodePtr sino = literals(kSinoDigits);
  NodePtr d = choice({sino, literals(kDigits)});
  NodePtr comma_groups =
      glued({digits(1, 3), repeated(glued({literal(U","), digits(3, 3)}), 1, -1)});
  NodePtr arabic = choice({comma_groups, digits(1, -1)});

  // Below 10,000 in numerals: [d]천 [d]백 [d]십 [d], highest unit first.
  NodePtr ten = glued({optional(d), literal(U"십"), optional(d)});
  NodePtr hundred = glued({optional(d), literal(U"백"), optional(choice({ten, d}))});
  NodePtr thousand = glued({optional(d), literal(U"천"), optional(choice({hundred, ten, d}))});
  NodePtr small = choice({arabic, thousand, hundred, ten, sino});

  // [small]만 [small], [small]억 [만 part | small], [small]조 [억 part | ...].
  auto group = [&](const char32_t* unit) { return glued({optional(small), literal(unit)}); };
  auto part = [&](const char32_t* unit, std::vector<NodePtr> lower) {
    std::vector<NodePtr> options;
    for (NodePtr& l : lower) options.push_back(spaced({group(unit), l}));
    options.push_back(group(unit));
    return choice(std::move(options));
  };
  NodePtr man = part(U"만", {small});
  NodePtr eok = part(U"억", {man, small});
  NodePtr jo = part(U"조", {eok, man, small});
  return choice({jo, eok, man, small});
}

// --- DATETIME ---------------------------------------------------------------

// One of the characters from `first` to `last`.
NodePtr range(char32_t first, char32_t last) {
  std::vector<NodePtr> options;
  for (char32_t c = first; c <= last; ++c) options.push_back(literal(std::u32string(1, c)));
  return choice(std::move(options));
}

// One or more of `items`, in their order, separated by optional spaces.
NodePtr in_order(const std::vector<NodePtr>& items, std::size_t from = 0) {
  if (from + 1 == items.size()) return items[from];
  NodePtr rest = in_order(items, from + 1);
  return choice({spaced({items[from], rest}), items[from], rest});
}

// Two-digit fields within their ranges, with an optional leading zero.
NodePtr month_digits() {
  return choice({glued({literal(U"1"), range(U'0', U'2')}),
                 glued({optional(literal(U"0")), range(U'1', U'9')})});
}
NodePtr day_digits() {
  return choice({glued({range(U'1', U'2'), range(U'0', U'9')}),
                 glued({literal(U"3"), range(U'0', U'1')}),
                 glued({optional(literal(U"0")), range(U'1', U'9')})});
}
NodePtr hour24_digits() {
  return choice({glued({literal(U"1"), range(U'0', U'9')}),
                 glued({literal(U"2"), range(U'0', U'4')}),
                 glued({optional(literal(U"0")), range(U'0', U'9')})});
}
NodePtr hour12_digits() {
  return choice({glued({literal(U"1"), range(U'0', U'2')}),
                 glued({optional(literal(U"0")), range(U'1', U'9')})});
}
NodePtr minute_digits() {
  return choice({glued({range(U'0', U'5'), range(U'0', U'9')}), range(U'0', U'9')});
}

// Dates and times in Korean and English, such as 내일 오후 3시, 다음 주 월요일,
// 2026년 3월 5일, 세 시 반, tomorrow 3pm, next monday, march 5th, 15:30. The
// words and numbers must match the attributes datetime_attributes() reads.
NodePtr datetime() {
  static const char32_t* const kSinoDigits[] = {U"일", U"이", U"삼", U"사", U"오",
                                                U"육", U"칠", U"팔", U"구", nullptr};
  NodePtr sino_digit = literals(kSinoDigits);
  NodePtr sino_upto_99 = choice(
      {glued({optional(sino_digit), literal(U"십"), optional(sino_digit)}), sino_digit});

  // Korean dates.
  static const char32_t* const kRelativeDays[] = {U"오늘", U"내일", U"모레", U"글피",
                                                  U"어제", U"그제", U"그저께", nullptr};
  static const char32_t* const kWeekdaysKo[] = {U"월", U"화", U"수", U"목",
                                                U"금", U"토", U"일", nullptr};
  static const char32_t* const kWhichKo[] = {U"이번", U"다음", U"지난", U"저번", nullptr};
  static const char32_t* const kRelativeYears[] = {U"올해", U"내년", U"작년", U"재작년",
                                                   nullptr};
  NodePtr which_ko = literals(kWhichKo);
  NodePtr week_ko = choice({spaced({which_ko, literal(U"주")}), literal(U"담주")});
  NodePtr weekday_ko = glued({literals(kWeekdaysKo), literal(U"요일")});
  NodePtr week_part_ko = in_order({week_ko, weekday_ko});
  NodePtr year_ko = choice({glued({choice({digits(4, 4), digits(2, 2)}), literal(U"년")}),
                            literals(kRelativeYears)});
  NodePtr month_ko = choice({glued({choice({month_digits(), sino_upto_99}), literal(U"월")}),
                             spaced({which_ko, literal(U"달")})});
  NodePtr day_ko = glued({choice({day_digits(), sino_upto_99}), literal(U"일")});
  NodePtr date_ko =
      choice({literals(kRelativeDays), week_part_ko, in_order({year_ko, month_ko, day_ko})});

  // Korean times.
  static const char32_t* const kNativeHours[] = {U"한", U"두", U"세", U"네", U"다섯",
                                                 U"여섯", U"일곱", U"여덟", U"아홉", U"열",
                                                 U"열한", U"열두", nullptr};
  static const char32_t* const kPartsKo[] = {U"새벽", U"아침", U"오전", U"낮", U"점심",
                                             U"오후", U"저녁", U"밤", nullptr};
  NodePtr hour_ko =
      spaced({choice({hour24_digits(), literals(kNativeHours), sino_upto_99}), literal(U"시")});
  NodePtr minute_ko =
      choice({spaced({choice({minute_digits(), sino_upto_99}), literal(U"분")}), literal(U"반")});
  NodePtr clock_ko = choice({spaced({hour_ko, minute_ko}), hour_ko});
  NodePtr part_ko = literals(kPartsKo);
  NodePtr time_ko = choice({spaced({part_ko, clock_ko}), clock_ko, part_ko, literal(U"정오"),
                            literal(U"자정")});

  // English dates.
  static const char32_t* const kRelativeDaysEn[] = {
      U"today", U"tomorrow", U"yesterday", U"the day after tomorrow",
      U"the day before yesterday", nullptr};
  static const char32_t* const kWeekdaysEn[] = {U"monday", U"tuesday", U"wednesday",
                                                U"thursday", U"friday", U"saturday",
                                                U"sunday", nullptr};
  static const char32_t* const kMonthsEn[] = {
      U"january", U"february", U"march", U"april", U"may", U"june", U"july", U"august",
      U"september", U"october", U"november", U"december", U"jan", U"feb", U"mar", U"apr",
      U"jun", U"jul", U"aug", U"sep", U"sept", U"oct", U"nov", U"dec", nullptr};
  static const char32_t* const kWhichEn[] = {U"this", U"next", U"last", nullptr};
  static const char32_t* const kOrdinals[] = {U"st", U"nd", U"rd", U"th", nullptr};
  NodePtr which_en = literals(kWhichEn);
  NodePtr weekday_en = literals(kWeekdaysEn);
  NodePtr week_en = choice({spaced({which_en, weekday_en}), weekday_en,
                            spaced({which_en, literal(U"week")})});
  NodePtr month_en = literals(kMonthsEn);
  NodePtr day_en = glued({day_digits(), optional(literals(kOrdinals))});
  NodePtr date_en = choice({literals(kRelativeDaysEn), week_en, spaced({month_en, day_en}),
                            spaced({day_en, literal(U"of"), month_en})});

  // English times.
  static const char32_t* const kAmPm[] = {U"am", U"pm", U"a.m.", U"p.m.", nullptr};
  static const char32_t* const kPartsEn[] = {
      U"morning", U"afternoon", U"evening", U"night", U"in the morning",
      U"in the afternoon", U"in the evening", U"at night", U"this morning",
      U"this afternoon", U"this evening", U"tonight", nullptr};
  NodePtr minutes_en = glued({literal(U":"), range(U'0', U'5'), range(U'0', U'9')});
  NodePtr clock_en =
      choice({spaced({glued({hour12_digits(), optional(minutes_en)}), literals(kAmPm)}),
              glued({hour24_digits(), minutes_en}),
              spaced({hour12_digits(), literal(U"o'clock")}), literal(U"noon"),
              literal(U"midnight")});
  NodePtr part_en = literals(kPartsEn);
  NodePtr time_en = choice({spaced({clock_en, part_en}), clock_en, part_en});

  // "tomorrow at 3pm": `at` may join a date and a time. A leading `at` is
  // left to the pattern (`(at) `@{DATETIME}`), like other prepositions.
  NodePtr en = choice({spaced({date_en, choice({spaced({literal(U"at"), time_en}), time_en})}),
                       date_en, time_en});
  return choice({in_order({date_ko, time_ko}), en});
}

}  // namespace

const std::map<std::string, SystemEntity>& system_entities() {
  static const std::map<std::string, SystemEntity> entities = [] {
    std::map<std::string, SystemEntity> m;
    m["NUMBER"] = {digits(1, -1), Normalizer::None};
    NodePtr separator = optional(choice({literal(U"-"), literal(U" ")}));
    m["TELNUM"] = {glued({literal(U"0"), digits(1, 2), separator, digits(3, 4), separator,
                          digits(4, 4)}),
                   Normalizer::None};
    m["KO_NUMBER"] = {korean_number(), Normalizer::KoreanNumber};
    m["DATETIME"] = {datetime(), Normalizer::DateTime};
    return m;
  }();
  return entities;
}

}  // namespace detail
}  // namespace nlu

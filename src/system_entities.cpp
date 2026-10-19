#include "system_entities.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace nlu {
namespace detail {

namespace {

// Null-terminated word lists, shared by the entities that read them.
const char32_t* const kSinoDigits[] = {U"일", U"이", U"삼", U"사", U"오",
                                       U"육", U"칠", U"팔", U"구", nullptr};
const char32_t* const kNativeTens[] = {U"열", U"스물", U"서른", U"마흔", U"쉰",
                                       U"예순", U"일흔", U"여든", U"아흔", nullptr};

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

// One of the characters from `first` to `last`.
NodePtr range(char32_t first, char32_t last) {
  std::vector<NodePtr> options;
  for (char32_t c = first; c <= last; ++c) options.push_back(literal(std::u32string(1, c)));
  return choice(std::move(options));
}

NodePtr digit() { return range(U'0', U'9'); }

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

// One or more of `items`, in their order, separated by optional spaces.
NodePtr in_order(std::vector<NodePtr> items) {
  auto n = node(Node::Kind::Chain);
  n->children = std::move(items);
  return n;
}

// Digits, with or without thousands separators: 35000, 35,000.
NodePtr arabic_number() {
  return choice({glued({digits(1, 3), repeated(glued({literal(U","), digits(3, 3)}), 1, -1)}),
                 digits(1, -1)});
}

// Native Korean numbers from 1 to 99: 하나, 열, 스물셋, 아흔아홉, and the
// forms used before a counter (한, 두, 세, 네, 스무).
NodePtr native_korean_number() {
  static const char32_t* const kOnes[] = {U"하나", U"둘", U"셋", U"넷", U"다섯",
                                          U"여섯", U"일곱", U"여덟", U"아홉", nullptr};
  static const char32_t* const kCountingOnes[] = {U"한", U"두", U"세", U"네", nullptr};
  NodePtr ones = choice({literals(kOnes), literals(kCountingOnes)});
  return choice({glued({literals(kNativeTens), optional(ones)}), ones, literal(U"스무")});
}

// Numbers written with digits and Korean numerals: 35000, 35,000, 3만 5천,
// 삼만오천, 2,500만, 1억 2천만. Below 만 a number is either digits or
// numerals (with single digits allowed for numerals: 3천5백); 만, 억 and 조
// groups may be separated by spaces. A space only ever separates two parts,
// so the entity never ends in one.
NodePtr korean_number() {
  static const char32_t* const kDigits[] = {U"1", U"2", U"3", U"4", U"5", U"6", U"7",
                                            U"8", U"9", nullptr};
  NodePtr sino = literals(kSinoDigits);
  NodePtr d = choice({sino, literals(kDigits)});
  NodePtr arabic = arabic_number();

  // Below 10,000 in numerals: [d]천 [d]백 [d]십 [d], highest unit first.
  NodePtr ten = glued({optional(d), literal(U"십"), optional(d)});
  NodePtr hundred = glued({optional(d), literal(U"백"), optional(choice({ten, d}))});
  NodePtr thousand = glued({optional(d), literal(U"천"), optional(choice({hundred, ten, d}))});
  NodePtr small = choice({arabic, thousand, hundred, ten, sino});

  // [small]조 [small]억 [small]만 [small], highest unit first, any of them.
  auto group = [&](const char32_t* unit) { return glued({optional(small), literal(unit)}); };
  return choice(
      {in_order({group(U"조"), group(U"억"), group(U"만"), small}), native_korean_number()});
}

// --- ORDINAL, EN_NUMBER, MONEY ---------------------------------------------------

const char32_t* const kEnglishUnits[] = {U"one", U"two", U"three", U"four", U"five", U"six",
                                         U"seven", U"eight", U"nine", nullptr};
const char32_t* const kEnglishTeens[] = {U"ten",      U"eleven",  U"twelve",   U"thirteen",
                                         U"fourteen", U"fifteen", U"sixteen",  U"seventeen",
                                         U"eighteen", U"nineteen", nullptr};
const char32_t* const kEnglishTens[] = {U"twenty", U"thirty",  U"forty",  U"fifty",
                                        U"sixty",  U"seventy", U"eighty", U"ninety", nullptr};

// One to 999,999,999,999 in English words, or in digits: twenty-one, two
// hundred and five, three thousand five hundred, 3 million, 1,250.
NodePtr english_number() {
  NodePtr units = literals(kEnglishUnits);
  NodePtr tens = literals(kEnglishTens);
  NodePtr below_100 = choice({glued({tens, literal(U"-"), units}), spaced({tens, units}), tens,
                              literals(kEnglishTeens), units, literal(U"zero")});
  NodePtr hundreds_of = spaced({choice({units, literal(U"a")}), literal(U"hundred")});
  NodePtr below_1000 = choice({spaced({hundreds_of, literal(U"and"), below_100}),
                               spaced({hundreds_of, below_100}), hundreds_of, below_100});
  auto scale = [&](const char32_t* word) {
    return spaced({choice({below_1000, arabic_number(), literal(U"a")}), literal(word)});
  };
  return choice({in_order({scale(U"billion"), scale(U"million"), scale(U"thousand"), below_1000}),
                 arabic_number()});
}

// 첫 번째, 두번째, 3번째, 셋째; first, twelfth, twenty-first, 21st.
NodePtr ordinal() {
  static const char32_t* const kCountingOnes[] = {U"한", U"두", U"세", U"네", U"다섯",
                                                  U"여섯", U"일곱", U"여덟", U"아홉", nullptr};
  static const char32_t* const kTh[] = {U"첫", U"둘", U"셋", U"넷", U"다섯", U"여섯",
                                        U"일곱", U"여덟", U"아홉", U"열", nullptr};
  NodePtr counting = choice({glued({literals(kNativeTens), optional(literals(kCountingOnes))}),
                             literals(kCountingOnes), literal(U"첫"), literal(U"스무")});
  NodePtr korean = choice({spaced({choice({counting, digits(1, 3)}), literal(U"번째")}),
                           glued({literals(kTh), literal(U"째")})});

  // Digits with the suffix their last digits call for: 1st, 11th, 21st, 3rd.
  NodePtr not_one = choice({literal(U"0"), range(U'2', U'9')});
  NodePtr lead = optional(glued({digits(0, -1), not_one}));
  NodePtr suffixed = choice({glued({digits(0, -1), literal(U"1"), range(U'0', U'9'), literal(U"th")}),
                             glued({lead, literal(U"1st")}), glued({lead, literal(U"2nd")}),
                             glued({lead, literal(U"3rd")}),
                             glued({lead, choice({literal(U"0"), range(U'4', U'9')}), literal(U"th")})});
  static const char32_t* const kOnesOrdinal[] = {U"first", U"second", U"third", U"fourth",
                                                 U"fifth", U"sixth",  U"seventh", U"eighth",
                                                 U"ninth", nullptr};
  static const char32_t* const kOtherOrdinals[] = {
      U"tenth",       U"eleventh",   U"twelfth",     U"thirteenth", U"fourteenth",
      U"fifteenth",   U"sixteenth",  U"seventeenth", U"eighteenth", U"nineteenth",
      U"twentieth",   U"thirtieth",  U"fortieth",    U"fiftieth",   U"sixtieth",
      U"seventieth",  U"eightieth",  U"ninetieth",   nullptr};
  NodePtr ones = literals(kOnesOrdinal);
  NodePtr tens = literals(kEnglishTens);
  NodePtr english = choice({glued({tens, literal(U"-"), ones}), spaced({tens, ones}), ones,
                            literals(kOtherOrdinals), suffixed});
  return choice({korean, english});
}

// An amount with its currency: 3만 원, 50달러, $12.50, €10, fifty dollars,
// 20 bucks, USD 50, 50 GBP.
NodePtr money() {
  NodePtr decimal =
      glued({arabic_number(), optional(glued({literal(U"."), digits(1, 2)}))});
  static const char32_t* const kCurrenciesKo[] = {U"원", U"달러", U"불", U"엔", U"유로",
                                                  U"위안", U"파운드", nullptr};
  static const char32_t* const kSymbols[] = {U"$", U"₩", U"€", U"£", U"¥", nullptr};
  static const char32_t* const kCurrenciesEn[] = {
      U"dollar", U"dollars", U"buck", U"bucks", U"euro",  U"euros", U"yen",
      U"won",    U"pound",   U"pounds", U"yuan", nullptr};
  static const char32_t* const kCodes[] = {U"usd", U"krw", U"eur", U"jpy", U"gbp", U"cny",
                                           nullptr};
  NodePtr codes = literals(kCodes);
  return choice({spaced({choice({korean_number(), decimal}), literals(kCurrenciesKo)}),
                 spaced({literals(kSymbols), decimal}),
                 spaced({choice({decimal, english_number()}), literals(kCurrenciesEn)}),
                 spaced({codes, decimal}), spaced({decimal, codes})});
}

// Phone numbers: Korean (010-9876-5432, 031-765-4321, 01098765432),
// international (+1 555-123-4567, +44 20 7946 0958, +82 10-9876-5432) and
// North American ((555) 123-4567, 555.123.4567).
NodePtr phone_number() {
  NodePtr korean_separator = optional(choice({literal(U"-"), literal(U" ")}));
  NodePtr korean = glued({literal(U"0"), digits(1, 2), korean_separator, digits(3, 4),
                          korean_separator, digits(4, 4)});
  NodePtr separator = optional(choice({literal(U"-"), literal(U" "), literal(U".")}));
  NodePtr area = choice({glued({literal(U"("), digits(1, 4), literal(U")")}), digits(1, 4)});
  NodePtr international =
      glued({literal(U"+"), digits(1, 3), separator, area, separator, digits(2, 4), separator,
             digits(2, 4), optional(glued({separator, digits(2, 4)}))});
  NodePtr north_american =
      glued({choice({glued({literal(U"("), digits(3, 3), literal(U")")}), digits(3, 3)}),
             separator, digits(3, 3), separator, digits(4, 4)});
  return choice({korean, international, north_american});
}

// --- DATETIME ---------------------------------------------------------------

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
  NodePtr date_ko = choice({literals(kRelativeDays), week_part_ko,
                            in_order({year_ko, month_ko, day_ko, weekday_ko})});

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
  NodePtr single_ko = in_order({date_ko, time_ko});

  // Korean durations: 2시간 뒤, 1시간 30분 후, 이틀 전, 한 달 후.
  static const char32_t* const kDayCounts[] = {U"하루", U"이틀", U"사흘", U"나흘", nullptr};
  NodePtr count_ko = choice({digits(1, 3), sino_upto_99, literals(kNativeHours)});
  auto amount_ko = [&](std::vector<NodePtr> units) {
    return spaced({count_ko, choice(std::move(units))});
  };
  NodePtr span_ko = in_order(
      {amount_ko({literal(U"년")}), amount_ko({literal(U"개월"), literal(U"달")}),
       amount_ko({literal(U"주"), literal(U"주일")}),
       choice({amount_ko({literal(U"일")}), literals(kDayCounts)}), amount_ko({literal(U"시간")}),
       amount_ko({literal(U"분")})});
  NodePtr duration_ko = spaced({span_ko, choice({literal(U"뒤"), literal(U"후"), literal(U"전")})});

  // Korean ranges: 3시부터 5시까지, 3시에서 5시 사이, 3시~5시, 3~5시.
  NodePtr dash = choice({literal(U"~"), literal(U"-")});
  NodePtr range_ko = choice(
      {spaced({glued({single_ko, literal(U"부터")}), glued({single_ko, literal(U"까지")})}),
       spaced({glued({single_ko, literal(U"부터")}), single_ko}),
       spaced({glued({single_ko, literal(U"에서")}), single_ko, literal(U"사이")}),
       spaced({single_ko, dash, single_ko}), spaced({hour24_digits(), dash, hour_ko})});

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

  // "tomorrow at 3pm": `at` may join a date and a time; "3pm tomorrow" and
  // "noon on friday" put the time first. A leading `at` or `on` is left to the
  // pattern (`(at) `@{DATETIME}`), like other prepositions.
  NodePtr single_en =
      choice({spaced({date_en, choice({spaced({literal(U"at"), time_en}), time_en})}),
              spaced({time_en, choice({spaced({literal(U"on"), date_en}), date_en})}), date_en,
              time_en});

  // English durations: in 2 hours, in an hour, 3 days ago, 2 weeks from now.
  static const char32_t* const kCountsEn[] = {
      U"a",   U"an",    U"one",   U"two",  U"three",  U"four",   U"five", U"six",
      U"seven", U"eight", U"nine", U"ten", U"eleven", U"twelve", nullptr};
  NodePtr count_en = choice({digits(1, 3), literals(kCountsEn)});
  auto amount_en = [&](const char32_t* unit) {
    std::u32string plural = std::u32string(unit) + U"s";
    return spaced({count_en, choice({literal(unit), literal(plural)})});
  };
  NodePtr span_en = in_order({amount_en(U"year"), amount_en(U"month"), amount_en(U"week"),
                              amount_en(U"day"), amount_en(U"hour"), amount_en(U"minute")});
  NodePtr duration_en =
      choice({spaced({literal(U"in"), span_en}),
              spaced({span_en, choice({literal(U"later"), literal(U"ago"),
                                       literal(U"from now")})})});

  // English ranges: from 3 to 5pm, between monday and friday, 3-5pm. The
  // start may be a bare hour, which takes the end's am or pm.
  NodePtr start_en = choice({single_en, hour12_digits()});
  NodePtr to_en = choice({literal(U"to"), literal(U"until"), literal(U"till"), literal(U"-")});
  NodePtr range_en = choice({spaced({literal(U"from"), start_en, to_en, single_en}),
                             spaced({literal(U"between"), start_en, literal(U"and"), single_en}),
                             spaced({start_en, to_en, single_en})});

  return choice({range_ko, duration_ko, single_ko, range_en, duration_en, single_en});
}

}  // namespace

const std::map<std::string, SystemEntity>& system_entities() {
  static const std::map<std::string, SystemEntity> entities = [] {
    std::map<std::string, SystemEntity> m;
    m["NUMBER"] = {digits(1, -1), Normalizer::None};
    m["TELNUM"] = {phone_number(), Normalizer::None};
    m["KO_NUMBER"] = {korean_number(), Normalizer::KoreanNumber};
    m["EN_NUMBER"] = {english_number(), Normalizer::EnglishNumber};
    m["ORDINAL"] = {ordinal(), Normalizer::Ordinal};
    m["MONEY"] = {money(), Normalizer::Money};
    m["DATETIME"] = {datetime(), Normalizer::DateTime};
    return m;
  }();
  return entities;
}

}  // namespace detail
}  // namespace nlu

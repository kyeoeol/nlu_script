// Words the system entities are made of, with what each one means. The
// grammar in system_entities.cpp matches them and the readers in numbers.cpp
// and datetime.cpp compute attributes from them, so a word is added in one
// place. Texts are as matched (lowercase, single spaces); the readers of
// dates and times compare them with the spaces removed. The grammar tries
// the words of a table longest first, so an entity takes the whole word
// (dollars, not dollar).
#ifndef NLU_SCRIPT_VOCABULARY_HPP
#define NLU_SCRIPT_VOCABULARY_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace nlu {
namespace detail {

struct NumberWord {
  const char32_t* text;
  int value;
};

struct NamedWord {
  const char32_t* text;
  const char* name;
};

// The value of `text` in `table`, or -1.
template <std::size_t N>
int value_of(const NumberWord (&table)[N], std::u32string_view text) {
  for (const NumberWord& w : table)
    if (text == w.text) return w.value;
  return -1;
}

// The first word in `table` with `value`, or nullptr.
template <std::size_t N>
const char32_t* word_for(const NumberWord (&table)[N], int value) {
  for (const NumberWord& w : table)
    if (w.value == value) return w.text;
  return nullptr;
}

// --- numbers ------------------------------------------------------------------

// Sino-Korean digits, 일 (1) to 구 (9).
inline constexpr std::u32string_view kSinoDigits = U"일이삼사오육칠팔구";

// Native Korean numbers: 1 to 9 as said alone, the forms 1 to 4 take before
// a counter (한 개, 두 시; 5 to 9 keep theirs), and the tens. 스무 is 20
// before a counter and takes no ones; 첫 is "first".
inline constexpr NumberWord kNativeOnes[] = {{U"하나", 1}, {U"둘", 2},   {U"셋", 3},
                                             {U"넷", 4},   {U"다섯", 5}, {U"여섯", 6},
                                             {U"일곱", 7}, {U"여덟", 8}, {U"아홉", 9}};
inline constexpr NumberWord kNativeCountingOnes[] = {
    {U"한", 1}, {U"두", 2}, {U"세", 3}, {U"네", 4}};
inline constexpr NumberWord kNativeTens[] = {{U"열", 10},   {U"스물", 20}, {U"서른", 30},
                                             {U"마흔", 40}, {U"쉰", 50},   {U"예순", 60},
                                             {U"일흔", 70}, {U"여든", 80}, {U"아흔", 90}};
inline constexpr NumberWord kNativeCountingTwenty = {U"스무", 20};
inline constexpr NumberWord kNativeFirst = {U"첫", 1};

// 1 to 99 in the form used before a counter: 한, 다섯, 열두, 스무, 스물한.
inline std::u32string native_counting_form(int n) {
  if (n == kNativeCountingTwenty.value) return kNativeCountingTwenty.text;
  std::u32string out = n >= 10 ? word_for(kNativeTens, n / 10 * 10) : U"";
  if (n % 10 != 0) {
    const char32_t* one = word_for(kNativeCountingOnes, n % 10);
    out += one ? one : word_for(kNativeOnes, n % 10);
  }
  return out;
}

// English numbers from 0 to 19, and the tens.
inline constexpr NumberWord kEnglishSmall[] = {
    {U"zero", 0},      {U"one", 1},        {U"two", 2},        {U"three", 3},
    {U"four", 4},      {U"five", 5},       {U"six", 6},        {U"seven", 7},
    {U"eight", 8},     {U"nine", 9},       {U"ten", 10},       {U"eleven", 11},
    {U"twelve", 12},   {U"thirteen", 13},  {U"fourteen", 14},  {U"fifteen", 15},
    {U"sixteen", 16},  {U"seventeen", 17}, {U"eighteen", 18},  {U"nineteen", 19}};
inline constexpr NumberWord kEnglishTens[] = {{U"twenty", 20}, {U"thirty", 30},
                                              {U"forty", 40},  {U"fifty", 50},
                                              {U"sixty", 60},  {U"seventy", 70},
                                              {U"eighty", 80}, {U"ninety", 90}};

// English ordinal words; the ones below 10 also follow a ten (twenty-first).
inline constexpr NumberWord kEnglishOrdinals[] = {
    {U"first", 1},        {U"second", 2},       {U"third", 3},       {U"fourth", 4},
    {U"fifth", 5},        {U"sixth", 6},        {U"seventh", 7},     {U"eighth", 8},
    {U"ninth", 9},        {U"tenth", 10},       {U"eleventh", 11},   {U"twelfth", 12},
    {U"thirteenth", 13},  {U"fourteenth", 14},  {U"fifteenth", 15},  {U"sixteenth", 16},
    {U"seventeenth", 17}, {U"eighteenth", 18},  {U"nineteenth", 19}, {U"twentieth", 20},
    {U"thirtieth", 30},   {U"fortieth", 40},    {U"fiftieth", 50},   {U"sixtieth", 60},
    {U"seventieth", 70},  {U"eightieth", 80},   {U"ninetieth", 90}};

// --- money --------------------------------------------------------------------

// Currencies and their ISO 4217 codes: Korean words after an amount (3만 원,
// 50달러), symbols before one ($12.50), English words after one (fifty
// dollars), and codes before or after one (USD 50, 50 GBP).
inline constexpr NamedWord kCurrencyWordsKo[] = {
    {U"원", "KRW"},   {U"달러", "USD"}, {U"불", "USD"},    {U"엔", "JPY"},
    {U"유로", "EUR"}, {U"위안", "CNY"}, {U"파운드", "GBP"}};
inline constexpr NamedWord kCurrencySymbols[] = {
    {U"$", "USD"}, {U"₩", "KRW"}, {U"€", "EUR"}, {U"£", "GBP"}, {U"¥", "JPY"}};
inline constexpr NamedWord kCurrencyWordsEn[] = {
    {U"dollar", "USD"}, {U"dollars", "USD"}, {U"buck", "USD"},  {U"bucks", "USD"},
    {U"euro", "EUR"},   {U"euros", "EUR"},   {U"yen", "JPY"},   {U"won", "KRW"},
    {U"pound", "GBP"},  {U"pounds", "GBP"},  {U"yuan", "CNY"}};
inline constexpr NamedWord kCurrencyCodes[] = {{U"usd", "USD"}, {U"krw", "KRW"},
                                               {U"eur", "EUR"}, {U"jpy", "JPY"},
                                               {U"gbp", "GBP"}, {U"cny", "CNY"}};
// Scales after an amount in digits: 30k, 2.8 million.
inline constexpr NumberWord kAmountScales[] = {
    {U"k", 1000}, {U"thousand", 1000}, {U"million", 1000000}, {U"billion", 1000000000}};

// --- dates and times ----------------------------------------------------------

// Weekdays from Monday, as the weekday attribute names them. Korean ones are
// the first syllable of 월요일 ... 일요일.
inline constexpr const char* kWeekdayNames[7] = {"MON", "TUE", "WED", "THU",
                                                 "FRI", "SAT", "SUN"};
inline constexpr std::u32string_view kWeekdaysKo = U"월화수목금토일";
inline constexpr const char32_t* kWeekdaysEn[7] = {U"monday",   U"tuesday", U"wednesday",
                                                   U"thursday", U"friday",  U"saturday",
                                                   U"sunday"};

// Days, weeks, months and years relative to now, by their offset. The
// Korean "which" words come before 주 (week) and 달 (month); the English
// ones before a weekday, "week" or "month".
inline constexpr NumberWord kRelativeDaysKo[] = {{U"오늘", 0},  {U"내일", 1},  {U"모레", 2},
                                                 {U"글피", 3},  {U"어제", -1}, {U"그제", -2},
                                                 {U"그저께", -2}};
inline constexpr NumberWord kWhichKo[] = {{U"이번", 0}, {U"다음", 1}, {U"지난", -1}, {U"저번", -1}};
inline constexpr NumberWord kRelativeYearsKo[] = {
    {U"올해", 0}, {U"내년", 1}, {U"작년", -1}, {U"재작년", -2}};
inline constexpr NumberWord kRelativeDaysEn[] = {{U"today", 0},
                                                 {U"tomorrow", 1},
                                                 {U"yesterday", -1},
                                                 {U"the day after tomorrow", 2},
                                                 {U"the day before yesterday", -2}};
inline constexpr NumberWord kWhichEn[] = {{U"this", 0}, {U"next", 1}, {U"last", -1}};

// English months and their abbreviations, by number.
inline constexpr NumberWord kMonthsEn[] = {
    {U"january", 1}, {U"february", 2}, {U"march", 3},     {U"april", 4},
    {U"may", 5},     {U"june", 6},     {U"july", 7},      {U"august", 8},
    {U"september", 9}, {U"october", 10}, {U"november", 11}, {U"december", 12},
    {U"jan", 1},     {U"feb", 2},      {U"mar", 3},       {U"apr", 4},
    {U"jun", 6},     {U"jul", 7},      {U"aug", 8},       {U"sep", 9},
    {U"sept", 9},    {U"oct", 10},     {U"nov", 11},      {U"dec", 12}};

// Parts of the day, as the part_of_day attribute names them. Some English
// ones also mean today (this morning, tonight).
inline constexpr NamedWord kPartsOfDayKo[] = {
    {U"새벽", "DAWN"}, {U"아침", "MORNING"}, {U"오전", "AM"},      {U"낮", "DAY"},
    {U"점심", "NOON"}, {U"오후", "PM"},      {U"저녁", "EVENING"}, {U"밤", "NIGHT"}};
struct PartOfDayEn {
  const char32_t* text;
  const char* name;
  bool today;
};
inline constexpr PartOfDayEn kPartsOfDayEn[] = {
    {U"morning", "MORNING", false},          {U"afternoon", "PM", false},
    {U"evening", "EVENING", false},          {U"night", "NIGHT", false},
    {U"in the morning", "MORNING", false},   {U"in the afternoon", "PM", false},
    {U"in the evening", "EVENING", false},   {U"at night", "NIGHT", false},
    {U"this morning", "MORNING", true},      {U"this afternoon", "PM", true},
    {U"this evening", "EVENING", true},      {U"tonight", "NIGHT", true}};
// After an English clock time: 3pm, 10:30 a.m.
inline constexpr NamedWord kAmPm[] = {
    {U"am", "AM"}, {U"pm", "PM"}, {U"a.m.", "AM"}, {U"p.m.", "PM"}};

// Times of day said as a word, by their hour (on the hour).
inline constexpr NumberWord kClockWordsKo[] = {{U"정오", 12}, {U"자정", 0}};
inline constexpr NumberWord kClockWordsEn[] = {{U"noon", 12}, {U"midnight", 0}};

// The largest hour, and duration count, said as a word: 열두 시, twelve
// hours.
inline constexpr int kMaxWordHour = 12;

// Units of a duration by the grain they count (an index into kGrains:
// year, month, week, day, hour, minute). English ones also take an "s".
struct DurationUnit {
  const char32_t* text;
  int grain;
};
inline constexpr DurationUnit kDurationUnitsKo[] = {
    {U"년", 0}, {U"개월", 1}, {U"달", 1}, {U"주", 2}, {U"주일", 2},
    {U"일", 3}, {U"시간", 4}, {U"분", 5}};
inline constexpr DurationUnit kDurationUnitsEn[] = {
    {U"year", 0}, {U"month", 1}, {U"week", 2}, {U"day", 3}, {U"hour", 4}, {U"minute", 5}};
// Durations of days said as one word: 하루 (a day), 이틀 (two days).
inline constexpr NumberWord kDayCountsKo[] = {{U"하루", 1}, {U"이틀", 2}, {U"사흘", 3}, {U"나흘", 4}};
// English counts of a duration besides the numbers: an hour, a week.
inline constexpr NumberWord kCountArticlesEn[] = {{U"a", 1}, {U"an", 1}};

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_VOCABULARY_HPP

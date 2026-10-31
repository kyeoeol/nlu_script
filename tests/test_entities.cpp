// Behavior of the number, ordinal, money and phone number system entities.
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "harness.hpp"
#include "nlu_script/nlu_script.hpp"

namespace {

// The attributes of system entity `entity` when it is the whole input, as
// "key=value ...", or "<no match>".
std::string attributes(const std::string& entity, const std::string& text) {
  static std::map<std::string, nlu::Script> scripts;
  auto it = scripts.find(entity);
  if (it == scripts.end())
    it = scripts.emplace(entity, nlu::Script::compile("t\n    `@{" + entity + "}\n")).first;
  std::vector<nlu::Match> found = it->second.match(text);
  if (found.size() != 1 || found[0].slots.size() != 1) return "<no match>";
  std::string out;
  for (const auto& kv : found[0].slots[0].attributes)
    out += (out.empty() ? "" : " ") + kv.first + "=" + kv.second;
  return out;
}

}  // namespace

TEST(entity_attributes_are_read_from_normalized_text) {
  nlu::Script s = nlu::Script::compile("option fold-width\nt\n    `@{KO_NUMBER}\n");
  std::vector<nlu::Match> found = s.match("３만");
  CHECK_EQ(found.size(), 1u);
  if (found.size() == 1) {
    CHECK_EQ(found[0].slots[0].text, std::string("３만"));
    std::string attributes;
    for (const auto& kv : found[0].slots[0].attributes) attributes += kv.first + "=" + kv.second;
    CHECK_EQ(attributes, std::string("value=30000"));
  }
}

TEST(korean_numbers_include_native_numbers) {
  CHECK_EQ(attributes("KO_NUMBER", "하나"), std::string("value=1"));
  CHECK_EQ(attributes("KO_NUMBER", "열"), std::string("value=10"));
  CHECK_EQ(attributes("KO_NUMBER", "스물셋"), std::string("value=23"));
  CHECK_EQ(attributes("KO_NUMBER", "아흔아홉"), std::string("value=99"));
  CHECK_EQ(attributes("KO_NUMBER", "일곱"), std::string("value=7"));
  CHECK_EQ(attributes("KO_NUMBER", "스무"), std::string("value=20"));
  CHECK_EQ(attributes("KO_NUMBER", "세"), std::string("value=3"));
  CHECK_EQ(attributes("KO_NUMBER", "하나둘"), std::string("<no match>"));
}

TEST(english_number_words) {
  CHECK_EQ(attributes("EN_NUMBER", "seven"), std::string("value=7"));
  CHECK_EQ(attributes("EN_NUMBER", "Nineteen"), std::string("value=19"));
  CHECK_EQ(attributes("EN_NUMBER", "twenty-one"), std::string("value=21"));
  CHECK_EQ(attributes("EN_NUMBER", "forty two"), std::string("value=42"));
  CHECK_EQ(attributes("EN_NUMBER", "two hundred and five"), std::string("value=205"));
  CHECK_EQ(attributes("EN_NUMBER", "a hundred"), std::string("value=100"));
  CHECK_EQ(attributes("EN_NUMBER", "three thousand five hundred"), std::string("value=3500"));
  CHECK_EQ(attributes("EN_NUMBER", "1,250"), std::string("value=1250"));
  CHECK_EQ(attributes("EN_NUMBER", "3 million"), std::string("value=3000000"));
  CHECK_EQ(attributes("EN_NUMBER", "twenty twenty"), std::string("<no match>"));
}

TEST(ordinals) {
  CHECK_EQ(attributes("ORDINAL", "첫 번째"), std::string("value=1"));
  CHECK_EQ(attributes("ORDINAL", "두번째"), std::string("value=2"));
  CHECK_EQ(attributes("ORDINAL", "열한 번째"), std::string("value=11"));
  CHECK_EQ(attributes("ORDINAL", "스무 번째"), std::string("value=20"));
  CHECK_EQ(attributes("ORDINAL", "3번째"), std::string("value=3"));
  CHECK_EQ(attributes("ORDINAL", "셋째"), std::string("value=3"));
  CHECK_EQ(attributes("ORDINAL", "first"), std::string("value=1"));
  CHECK_EQ(attributes("ORDINAL", "twelfth"), std::string("value=12"));
  CHECK_EQ(attributes("ORDINAL", "twenty-first"), std::string("value=21"));
  CHECK_EQ(attributes("ORDINAL", "21st"), std::string("value=21"));
  CHECK_EQ(attributes("ORDINAL", "3th"), std::string("<no match>"));
}

TEST(money_amounts_and_currencies) {
  CHECK_EQ(attributes("MONEY", "3만 원"), std::string("value=30000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "삼만 오천원"), std::string("value=35000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "5,000원"), std::string("value=5000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "50달러"), std::string("value=50 currency=USD"));
  CHECK_EQ(attributes("MONEY", "100엔"), std::string("value=100 currency=JPY"));
  CHECK_EQ(attributes("MONEY", "$12.50"), std::string("value=12.50 currency=USD"));
  CHECK_EQ(attributes("MONEY", "€10"), std::string("value=10 currency=EUR"));
  CHECK_EQ(attributes("MONEY", "₩5,000"), std::string("value=5000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "50 dollars"), std::string("value=50 currency=USD"));
  CHECK_EQ(attributes("MONEY", "fifty dollars"), std::string("value=50 currency=USD"));
  CHECK_EQ(attributes("MONEY", "20 bucks"), std::string("value=20 currency=USD"));
  CHECK_EQ(attributes("MONEY", "10 euros"), std::string("value=10 currency=EUR"));
  CHECK_EQ(attributes("MONEY", "USD 50"), std::string("value=50 currency=USD"));
  CHECK_EQ(attributes("MONEY", "50 GBP"), std::string("value=50 currency=GBP"));
  CHECK_EQ(attributes("MONEY", "50"), std::string("<no match>"));
}

TEST(money_amounts_with_a_scale) {
  CHECK_EQ(attributes("MONEY", "30k won"), std::string("value=30000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "30K won"), std::string("value=30000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "2.8 million won"), std::string("value=2800000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "1.25 billion won"), std::string("value=1250000000 currency=KRW"));
  CHECK_EQ(attributes("MONEY", "$1.5k"), std::string("value=1500 currency=USD"));
  CHECK_EQ(attributes("MONEY", "$2.5 million"), std::string("value=2500000 currency=USD"));
  CHECK_EQ(attributes("MONEY", "USD 30k"), std::string("value=30000 currency=USD"));
  CHECK_EQ(attributes("MONEY", "1,500k dollars"), std::string("value=1500000 currency=USD"));
  CHECK_EQ(attributes("MONEY", "30k"), std::string("<no match>"));
}

TEST(system_entities_take_their_longest_form_before_a_wildcard) {
  // A wildcard after the entity could take the rest of a word (the `s` of
  // `dollars`); the entity keeps it.
  const std::pair<const char*, const char*> cases[] = {
      {"MONEY", "50 dollars"},   {"MONEY", "fifty dollars"},       {"MONEY", "20 bucks"},
      {"MONEY", "10 euros"},     {"MONEY", "3 pounds"},            {"MONEY", "$30k"},
      {"DATETIME", "in 2 hours"}, {"DATETIME", "in 1 hour 30 minutes"},
      {"DATETIME", "3 days ago"}, {"KO_NUMBER", "일곱"},             {"KO_NUMBER", "일흔일곱"},
      {"KO_NUMBER", "3만 5천"},
      {"EN_NUMBER", "seventeen"}, {"ORDINAL", "twenty-first"}};
  for (const auto& c : cases) {
    nlu::Script s = nlu::Script::compile(std::string("t\n    `@{") + c.first + "} <?>*?\n");
    std::vector<nlu::Match> found = s.match(c.second);
    CHECK_EQ(found.size(), 1u);
    if (found.size() == 1 && found[0].slots.size() == 1)
      CHECK_EQ(found[0].slots[0].text, std::string(c.second));
  }
}

TEST(phone_numbers_in_other_formats) {
  for (const char* text : {"+1 555-123-4567", "+1 (555) 123-4567", "(555) 123-4567",
                           "555-123-4567", "555.123.4567", "+82 10-9876-5432",
                           "+44 20 7946 0958", "010-9876-5432", "01098765432"})
    CHECK_EQ(attributes("TELNUM", text), std::string());
  CHECK_EQ(attributes("TELNUM", "911"), std::string("<no match>"));
  CHECK_EQ(attributes("TELNUM", "123-45"), std::string("<no match>"));
}

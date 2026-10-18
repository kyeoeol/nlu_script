// Values of numbers written with digits and Korean numerals.
#ifndef NLU_SCRIPT_NUMBERS_HPP
#define NLU_SCRIPT_NUMBERS_HPP

#include <string>

namespace nlu {
namespace detail {

// Reads a number such as "35,000", "3만 5천", "삼만오천", "1억 2천만" or a
// native one ("스물셋") and stores its value in decimal digits. Returns false
// if `text` is not such a number or its value is 10^18 or more.
bool korean_number_value(const std::string& text, std::string& value);

// Reads a number in English words or digits ("two hundred and five",
// "3 million", "1,250"). Returns false as above.
bool english_number_value(const std::string& text, std::string& value);

// Reads an ordinal ("첫 번째", "셋째", "twenty-first", "21st") and stores its
// position. Returns false if `text` is not one.
bool ordinal_value(const std::string& text, std::string& value);

// Reads an amount of money ("3만 원", "$12.50", "fifty dollars") and stores
// its amount (digits, with the decimals as written) and currency code.
bool money_value(const std::string& text, std::string& value, std::string& currency);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_NUMBERS_HPP

// Values of numbers written with digits and Korean numerals.
#ifndef NLU_SCRIPT_NUMBERS_HPP
#define NLU_SCRIPT_NUMBERS_HPP

#include <string>

namespace nlu {
namespace detail {

// Reads a number such as "35,000", "3만 5천", "삼만오천" or "1억 2천만" and
// stores its value in decimal digits. Returns false if `text` is not such a
// number or its value is 10^18 or more.
bool korean_number_value(const std::string& text, std::string& value);

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_NUMBERS_HPP

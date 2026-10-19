// Attributes of dates and times matched by the DATETIME system entity.
#ifndef NLU_SCRIPT_DATETIME_HPP
#define NLU_SCRIPT_DATETIME_HPP

#include <string>
#include <utility>
#include <vector>

namespace nlu {
namespace detail {

using Attributes = std::vector<std::pair<std::string, std::string>>;

// Sets `key` to `value`, in place if `attrs` has it already.
void set_attribute(Attributes& attrs, const std::string& key, const std::string& value);

// Reads a date or time such as "내일 오후 3시" or "next monday" and returns its
// fields as attributes, in the order the text gives them:
//   day_offset, week_offset, month_offset, year_offset   "+1", "-1", "0"
//   weekday                                               "MON" ... "SUN"
//   year, month, day                                      as written
//   part_of_day   DAWN, MORNING, AM, NOON, DAY, PM, EVENING, NIGHT
//   hour, minute  hour on a 24-hour clock when the part of day tells
// Fields out of range (e.g. 십삼월) are left out.
Attributes datetime_attributes(const std::string& text);

// The values of DateTimeRange::grain, coarsest first.
extern const char* const kGrains[6];

}  // namespace detail
}  // namespace nlu

#endif  // NLU_SCRIPT_DATETIME_HPP

#include "fdr/timearg.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace fdr {

std::uint64_t parse_u64(const std::string& text, const std::string& what) {
  std::uint64_t value = 0;
  const char* first = text.data();
  const char* last = first + text.size();
  const auto result = std::from_chars(first, last, value, 10);
  if (text.empty() || result.ec != std::errc() || result.ptr != last) {
    throw std::invalid_argument(what + ": expected a non-negative integer, got '" + text + "'");
  }
  return value;
}

std::uint64_t parse_time_us(const std::string& text, std::uint64_t base_us,
                            const std::string& what) {
  if (text.empty() || text[0] != '+') {
    return parse_u64(text, what);
  }
  std::size_t split = 1;
  while (split < text.size() && ((text[split] >= '0' && text[split] <= '9') || text[split] == '.')) {
    ++split;
  }
  const std::string number = text.substr(1, split - 1);
  const std::string unit = text.substr(split);
  double scale = 0;
  if (unit == "us") {
    scale = 1.0;
  } else if (unit == "ms") {
    scale = 1e3;
  } else if (unit == "s") {
    scale = 1e6;
  } else {
    throw std::invalid_argument(what + ": expected +<number>us|ms|s, got '" + text + "'");
  }
  char* end = nullptr;
  const double value = std::strtod(number.c_str(), &end);
  if (number.empty() || end != number.c_str() + number.size()) {
    throw std::invalid_argument(what + ": expected +<number>us|ms|s, got '" + text + "'");
  }
  const double offset = std::round(value * scale);
  if (offset >= static_cast<double>(std::numeric_limits<std::uint64_t>::max() - base_us)) {
    throw std::invalid_argument(what + ": time offset out of range");
  }
  return base_us + static_cast<std::uint64_t>(offset);
}

}  // namespace fdr

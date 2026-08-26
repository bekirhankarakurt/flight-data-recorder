#pragma once

#include <cstdint>
#include <string>

namespace fdr {

// Parses an unsigned decimal integer. Throws std::invalid_argument on junk,
// signs, or overflow. `what` names the option in the error message.
[[nodiscard]] std::uint64_t parse_u64(const std::string& text, const std::string& what);

// Parses a time bound. "1700000000000000" is an absolute timestamp in
// microseconds; "+2.5s", "+300ms" and "+40us" are offsets from `base_us`.
[[nodiscard]] std::uint64_t parse_time_us(const std::string& text, std::uint64_t base_us,
                                          const std::string& what);

}  // namespace fdr

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fdr {

// Little-endian integer (de)serialisation independent of host byte order.
template <typename T>
void put_le(std::uint8_t* p, T v) noexcept {
  static_assert(std::is_unsigned<T>::value, "unsigned integers only");
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    p[i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
  }
}

template <typename T>
[[nodiscard]] T get_le(const std::uint8_t* p) noexcept {
  static_assert(std::is_unsigned<T>::value, "unsigned integers only");
  T v = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    v = static_cast<T>(v | (static_cast<T>(p[i]) << (8U * i)));
  }
  return v;
}

}  // namespace fdr

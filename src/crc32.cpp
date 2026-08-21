#include "fdr/crc32.hpp"

#include <array>

namespace fdr {
namespace {

constexpr std::array<std::uint32_t, 256> make_table() noexcept {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int bit = 0; bit < 8; ++bit) {
      c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
    }
    table[i] = c;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                    std::uint32_t crc) noexcept {
  std::uint32_t c = ~crc;
  for (std::size_t i = 0; i < size; ++i) {
    c = kTable[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
  }
  return ~c;
}

}  // namespace fdr

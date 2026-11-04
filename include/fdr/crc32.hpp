#pragma once

#include <cstddef>
#include <cstdint>

namespace fdr {

// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320) as used by zlib.
// Pass the previous result as `crc` to checksum data incrementally.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                                  std::uint32_t crc = 0) noexcept;

}  // namespace fdr

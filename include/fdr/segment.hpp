#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fdr {

// Segment file layout: a 32-byte header followed by back-to-back record frames.
//
//   offset  size  field
//   0       8     magic "FDRSEG01"
//   8       4     format version (1)
//   12      4     segment sequence number
//   16      8     timestamp of the first record (us)
//   24      4     CRC-32 over bytes [0, 24)
//   28      4     reserved (0)
constexpr std::size_t kSegmentHeaderSize = 32;
constexpr std::uint32_t kSegmentVersion = 1;

struct SegmentHeader {
  std::uint32_t sequence = 0;
  std::uint64_t base_timestamp_us = 0;
};

void encode_segment_header(const SegmentHeader& header,
                           std::uint8_t out[kSegmentHeaderSize]) noexcept;

[[nodiscard]] bool decode_segment_header(const std::uint8_t* data, std::size_t size,
                                         SegmentHeader& out) noexcept;

// "fdr-000042.seg"
[[nodiscard]] std::string segment_file_name(std::uint32_t sequence);

struct SegmentFile {
  std::uint32_t sequence = 0;
  std::filesystem::path path;
};

// Segment files found in `dir`, ordered by sequence number. A missing
// directory yields an empty list.
[[nodiscard]] std::vector<SegmentFile> list_segments(const std::filesystem::path& dir);

}  // namespace fdr

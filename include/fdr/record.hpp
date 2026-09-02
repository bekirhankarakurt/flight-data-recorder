#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fdr {

// One recorded sample: a timestamp (microseconds), a channel id and an opaque
// payload.
struct Record {
  std::uint64_t timestamp_us = 0;
  std::uint16_t channel = 0;
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] inline bool operator==(const Record& a, const Record& b) noexcept {
  return a.timestamp_us == b.timestamp_us && a.channel == b.channel &&
         a.payload == b.payload;
}
[[nodiscard]] inline bool operator!=(const Record& a, const Record& b) noexcept {
  return !(a == b);
}

// On-disk frame layout (all integers little-endian):
//
//   offset  size  field
//   0       4     sync marker 0xA55AF00D
//   4       4     payload length
//   8       8     timestamp (us)
//   16      2     channel
//   18      2     reserved (0)
//   20      n     payload
//   20+n    4     CRC-32 over bytes [4, 20+n)
constexpr std::uint32_t kSyncMarker = 0xA55AF00DU;
constexpr std::size_t kFrameHeaderSize = 20;
constexpr std::size_t kFrameTrailerSize = 4;
constexpr std::size_t kFrameOverhead = kFrameHeaderSize + kFrameTrailerSize;
constexpr std::size_t kMaxPayloadSize = 65536;

enum class FrameStatus : std::uint8_t {
  kOk,         // a complete, CRC-valid frame
  kTruncated,  // the buffer ends before the frame does
  kBadSync,    // the sync marker is missing
  kBadLength,  // the length field exceeds kMaxPayloadSize
  kBadCrc,     // the checksum does not match
};

struct FrameResult {
  FrameStatus status = FrameStatus::kBadSync;
  std::size_t size = 0;  // total frame size; valid when status == kOk
};

// Appends the encoded frame to `out`. Throws std::length_error when the
// payload exceeds kMaxPayloadSize.
void encode_frame(const Record& record, std::vector<std::uint8_t>& out);

// Validates the frame at the start of `data`. When `out` is non-null and the
// frame is valid, the decoded record is stored there.
[[nodiscard]] FrameResult parse_frame(const std::uint8_t* data, std::size_t avail,
                                      Record* out);

}  // namespace fdr

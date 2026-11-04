#include "fdr/record.hpp"

#include <cstring>
#include <stdexcept>

#include "fdr/crc32.hpp"
#include "fdr/endian.hpp"

namespace fdr {

void encode_frame(const Record& record, std::vector<std::uint8_t>& out) {
  const std::size_t n = record.payload.size();
  if (n > kMaxPayloadSize) {
    throw std::length_error("payload exceeds maximum frame payload size");
  }
  const std::size_t base = out.size();
  out.resize(base + kFrameOverhead + n);
  std::uint8_t* p = out.data() + base;
  put_le<std::uint32_t>(p, kSyncMarker);
  put_le<std::uint32_t>(p + 4, static_cast<std::uint32_t>(n));
  put_le<std::uint64_t>(p + 8, record.timestamp_us);
  put_le<std::uint16_t>(p + 16, record.channel);
  put_le<std::uint16_t>(p + 18, std::uint16_t{0});
  if (n != 0) {
    std::memcpy(p + kFrameHeaderSize, record.payload.data(), n);
  }
  put_le<std::uint32_t>(p + kFrameHeaderSize + n,
                        crc32(p + 4, kFrameHeaderSize - 4 + n));
}

FrameResult parse_frame(const std::uint8_t* data, std::size_t avail, Record* out) {
  if (avail < 4) {
    return {FrameStatus::kTruncated, 0};
  }
  if (get_le<std::uint32_t>(data) != kSyncMarker) {
    return {FrameStatus::kBadSync, 0};
  }
  if (avail < kFrameHeaderSize) {
    return {FrameStatus::kTruncated, 0};
  }
  const std::size_t n = get_le<std::uint32_t>(data + 4);
  if (n > kMaxPayloadSize) {
    return {FrameStatus::kBadLength, 0};
  }
  const std::size_t total = kFrameOverhead + n;
  if (avail < total) {
    return {FrameStatus::kTruncated, 0};
  }
  const std::uint32_t stored = get_le<std::uint32_t>(data + kFrameHeaderSize + n);
  if (crc32(data + 4, kFrameHeaderSize - 4 + n) != stored) {
    return {FrameStatus::kBadCrc, 0};
  }
  if (out != nullptr) {
    out->timestamp_us = get_le<std::uint64_t>(data + 8);
    out->channel = get_le<std::uint16_t>(data + 16);
    out->payload.assign(data + kFrameHeaderSize, data + kFrameHeaderSize + n);
  }
  return {FrameStatus::kOk, total};
}

}  // namespace fdr

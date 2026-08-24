#include "fdr/scan.hpp"

#include <algorithm>
#include <cstring>

namespace fdr {
namespace {

constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
constexpr int kSyncFirstByte = static_cast<int>(kSyncMarker & 0xFFU);

std::size_t find_next_frame(const std::uint8_t* data, std::size_t size,
                            std::size_t from) {
  while (from < size) {
    const void* hit = std::memchr(data + from, kSyncFirstByte, size - from);
    if (hit == nullptr) {
      return kNotFound;
    }
    const auto pos = static_cast<std::size_t>(static_cast<const std::uint8_t*>(hit) - data);
    if (parse_frame(data + pos, size - pos, nullptr).status == FrameStatus::kOk) {
      return pos;
    }
    from = pos + 1;
  }
  return kNotFound;
}

}  // namespace

ScanResult scan_frames(const std::uint8_t* data, std::size_t size, std::size_t start,
                       const RecordVisitor& visit) {
  ScanResult result;
  result.valid_end = start;
  Record record;
  std::size_t pos = start;
  while (pos < size) {
    const FrameResult frame = parse_frame(data + pos, size - pos, &record);
    if (frame.status == FrameStatus::kOk) {
      ++result.records;
      pos += frame.size;
      result.valid_end = pos;
      if (!visit(record, pos - frame.size)) {
        result.stopped = true;
        return result;
      }
      continue;
    }
    const std::size_t next = find_next_frame(data, size, pos + 1);
    if (next == kNotFound) {
      break;
    }
    ++result.corrupt_regions;
    result.skipped_bytes += next - pos;
    pos = next;
  }
  result.torn_tail = std::any_of(data + result.valid_end, data + size,
                                 [](std::uint8_t b) { return b != 0; });
  return result;
}

}  // namespace fdr

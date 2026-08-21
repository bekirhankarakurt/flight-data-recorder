#include "fdr/segment.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "fdr/crc32.hpp"
#include "fdr/endian.hpp"

namespace fdr {
namespace {

constexpr char kMagic[8] = {'F', 'D', 'R', 'S', 'E', 'G', '0', '1'};
const std::string kPrefix = "fdr-";
const std::string kSuffix = ".seg";

bool parse_segment_name(const std::string& name, std::uint32_t& sequence) {
  if (name.size() <= kPrefix.size() + kSuffix.size() ||
      name.compare(0, kPrefix.size(), kPrefix) != 0 ||
      name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
    return false;
  }
  const std::string digits =
      name.substr(kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
  if (digits.size() > 9 ||
      !std::all_of(digits.begin(), digits.end(),
                   [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  std::uint32_t value = 0;
  for (const char c : digits) {
    value = value * 10U + static_cast<std::uint32_t>(c - '0');
  }
  sequence = value;
  return true;
}

}  // namespace

void encode_segment_header(const SegmentHeader& header,
                           std::uint8_t out[kSegmentHeaderSize]) noexcept {
  std::memcpy(out, kMagic, sizeof(kMagic));
  put_le<std::uint32_t>(out + 8, kSegmentVersion);
  put_le<std::uint32_t>(out + 12, header.sequence);
  put_le<std::uint64_t>(out + 16, header.base_timestamp_us);
  put_le<std::uint32_t>(out + 24, crc32(out, 24));
  put_le<std::uint32_t>(out + 28, std::uint32_t{0});
}

bool decode_segment_header(const std::uint8_t* data, std::size_t size,
                           SegmentHeader& out) noexcept {
  if (size < kSegmentHeaderSize || std::memcmp(data, kMagic, sizeof(kMagic)) != 0 ||
      get_le<std::uint32_t>(data + 8) != kSegmentVersion ||
      get_le<std::uint32_t>(data + 24) != crc32(data, 24)) {
    return false;
  }
  out.sequence = get_le<std::uint32_t>(data + 12);
  out.base_timestamp_us = get_le<std::uint64_t>(data + 16);
  return true;
}

std::string segment_file_name(std::uint32_t sequence) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "fdr-%06u.seg", static_cast<unsigned>(sequence));
  return buf;
}

std::vector<SegmentFile> list_segments(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  std::vector<SegmentFile> files;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return files;
  }
  for (const auto& entry : fs::directory_iterator(dir)) {
    SegmentFile file;
    if (entry.is_regular_file() &&
        parse_segment_name(entry.path().filename().string(), file.sequence)) {
      file.path = entry.path();
      files.push_back(std::move(file));
    }
  }
  std::sort(files.begin(), files.end(), [](const SegmentFile& a, const SegmentFile& b) {
    return a.sequence < b.sequence;
  });
  return files;
}

}  // namespace fdr

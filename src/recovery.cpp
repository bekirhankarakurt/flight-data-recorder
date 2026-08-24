#include "fdr/recovery.hpp"

#include <system_error>

#include "fdr/fd.hpp"
#include "fdr/scan.hpp"
#include "fdr/segment.hpp"

namespace fdr {

bool RecoveryReport::damaged() const noexcept {
  if (torn_segments != 0 || corrupt_regions != 0 || removed_segments != 0) {
    return true;
  }
  for (const auto& s : segments) {
    if (!s.header_ok) {
      return true;
    }
  }
  return false;
}

RecoveryReport recover(const std::filesystem::path& dir, bool repair) {
  RecoveryReport report;
  const std::vector<SegmentFile> files = list_segments(dir);
  for (std::size_t i = 0; i < files.size(); ++i) {
    const bool is_last = i + 1 == files.size();
    const std::vector<std::uint8_t> data = read_file(files[i].path);

    SegmentReport seg;
    seg.sequence = files[i].sequence;
    seg.path = files[i].path;
    seg.file_size = data.size();
    SegmentHeader header;
    seg.header_ok = decode_segment_header(data.data(), data.size(), header) &&
                    header.sequence == seg.sequence;

    if (data.size() >= kSegmentHeaderSize) {
      bool first = true;
      const ScanResult scan = scan_frames(
          data.data(), data.size(), kSegmentHeaderSize, [&](const Record& r, std::uint64_t) {
            if (first) {
              seg.first_timestamp_us = r.timestamp_us;
              first = false;
            }
            seg.last_timestamp_us = r.timestamp_us;
            return true;
          });
      seg.valid_end = scan.valid_end;
      seg.records = scan.records;
      seg.corrupt_regions = scan.corrupt_regions;
      seg.skipped_bytes = scan.skipped_bytes;
      seg.torn_tail = scan.torn_tail;
    }

    if (is_last && !seg.header_ok && seg.records == 0) {
      ++report.removed_segments;
      if (repair) {
        std::filesystem::remove(seg.path);
        sync_directory(dir);
      }
      continue;
    }

    if (data.size() >= kSegmentHeaderSize && seg.valid_end < seg.file_size) {
      report.truncated_bytes += seg.file_size - seg.valid_end;
      if (repair) {
        std::filesystem::resize_file(seg.path, seg.valid_end);
      }
    }
    report.records += seg.records;
    report.corrupt_regions += seg.corrupt_regions;
    report.skipped_bytes += seg.skipped_bytes;
    report.torn_segments += seg.torn_tail ? 1U : 0U;
    report.segments.push_back(std::move(seg));
  }
  return report;
}

}  // namespace fdr

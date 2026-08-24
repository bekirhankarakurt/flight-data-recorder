#include "fdr/reader.hpp"

#include <algorithm>
#include <stdexcept>

#include "fdr/fd.hpp"
#include "fdr/scan.hpp"

namespace fdr {

Reader::Reader(std::filesystem::path dir, std::size_t index_stride)
    : files_(list_segments(dir)) {
  if (index_stride == 0) {
    throw std::invalid_argument("index_stride must be at least 1");
  }
  for (std::size_t s = 0; s < files_.size(); ++s) {
    const std::vector<std::uint8_t> data = read_file(files_[s].path);
    SegmentSummary seg;
    seg.sequence = files_[s].sequence;
    if (data.size() < kSegmentHeaderSize) {
      summary_.segments.push_back(seg);
      continue;
    }
    const ScanResult scan = scan_frames(
        data.data(), data.size(), kSegmentHeaderSize,
        [&](const Record& r, std::uint64_t offset) {
          if (seg.records % index_stride == 0) {
            index_.push_back({r.timestamp_us, s, offset});
          }
          if (summary_.records == 0) {
            summary_.first_timestamp_us = r.timestamp_us;
          }
          if (seg.records == 0) {
            seg.first_timestamp_us = r.timestamp_us;
          }
          seg.last_timestamp_us = r.timestamp_us;
          summary_.last_timestamp_us = r.timestamp_us;
          ++seg.records;
          ++summary_.records;
          ++summary_.channel_counts[r.channel];
          return true;
        });
    seg.valid_bytes = scan.valid_end;
    summary_.corrupt_regions += scan.corrupt_regions;
    summary_.skipped_bytes += scan.skipped_bytes;
    summary_.segments.push_back(seg);
  }
}

std::size_t Reader::query(std::uint64_t from_us, std::uint64_t to_us,
                          const std::function<bool(const Record&)>& visit) const {
  if (index_.empty() || from_us > to_us) {
    return 0;
  }
  // Start at the entry before the first one at or after `from_us`: records
  // equal to `from_us` may precede that entry.
  auto it = std::lower_bound(index_.begin(), index_.end(), from_us,
                             [](const IndexEntry& e, std::uint64_t t) {
                               return e.timestamp_us < t;
                             });
  if (it != index_.begin()) {
    --it;
  }
  std::size_t visited = 0;
  bool done = false;
  for (std::size_t s = it->segment; s < files_.size() && !done; ++s) {
    const std::uint64_t start = s == it->segment ? it->offset : kSegmentHeaderSize;
    const std::vector<std::uint8_t> data = read_file(files_[s].path, start);
    scan_frames(data.data(), data.size(), 0, [&](const Record& r, std::uint64_t) {
      if (r.timestamp_us > to_us) {
        done = true;
        return false;
      }
      if (r.timestamp_us < from_us) {
        return true;
      }
      ++visited;
      if (!visit(r)) {
        done = true;
        return false;
      }
      return true;
    });
  }
  return visited;
}

}  // namespace fdr

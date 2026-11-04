#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>

#include "fdr/record.hpp"
#include "fdr/segment.hpp"

namespace fdr {

// Sparse time index entry: the record at `offset` in segment number `segment`
// (a position in the sorted segment list) has the given timestamp.
struct IndexEntry {
  std::uint64_t timestamp_us = 0;
  std::size_t segment = 0;
  std::uint64_t offset = 0;
};

struct SegmentSummary {
  std::uint32_t sequence = 0;
  std::uint64_t valid_bytes = 0;
  std::size_t records = 0;
  std::uint64_t first_timestamp_us = 0;
  std::uint64_t last_timestamp_us = 0;
};

struct ReadSummary {
  std::vector<SegmentSummary> segments;
  std::size_t records = 0;
  std::size_t corrupt_regions = 0;
  std::uint64_t skipped_bytes = 0;
  std::uint64_t first_timestamp_us = 0;  // meaningful when records > 0
  std::uint64_t last_timestamp_us = 0;   // meaningful when records > 0
  std::map<std::uint16_t, std::size_t> channel_counts;
};

// Read-only view of a recording. The constructor scans every segment once to
// build an in-memory sparse index (one entry per segment start plus one every
// `index_stride` records); the index is derived data, never stored, so it can
// not go stale or corrupt. Reading tolerates damage by resynchronising.
class Reader {
 public:
  explicit Reader(const std::filesystem::path& dir, std::size_t index_stride = 64);

  [[nodiscard]] const ReadSummary& summary() const noexcept { return summary_; }
  [[nodiscard]] const std::vector<IndexEntry>& index() const noexcept { return index_; }

  // Visits records with from_us <= timestamp <= to_us in order. Stops early
  // when the visitor returns false. Returns the number of records visited.
  std::size_t query(std::uint64_t from_us, std::uint64_t to_us,
                    const std::function<bool(const Record&)>& visit) const;

 private:
  std::vector<SegmentFile> files_;
  std::vector<IndexEntry> index_;
  ReadSummary summary_;
};

}  // namespace fdr

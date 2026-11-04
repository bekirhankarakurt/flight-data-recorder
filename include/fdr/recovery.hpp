#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace fdr {

struct SegmentReport {
  std::uint32_t sequence = 0;
  std::filesystem::path path;
  std::uint64_t file_size = 0;
  std::uint64_t valid_end = 0;  // end of the last valid frame
  std::size_t records = 0;
  std::size_t corrupt_regions = 0;
  std::uint64_t skipped_bytes = 0;
  bool header_ok = false;
  bool torn_tail = false;
  std::uint64_t first_timestamp_us = 0;  // meaningful when records > 0
  std::uint64_t last_timestamp_us = 0;   // meaningful when records > 0
};

struct RecoveryReport {
  std::vector<SegmentReport> segments;  // segments that remain after recovery
  std::size_t records = 0;
  std::size_t corrupt_regions = 0;
  std::uint64_t skipped_bytes = 0;
  std::uint64_t truncated_bytes = 0;  // pre-allocation padding plus torn tails
  std::size_t torn_segments = 0;
  std::size_t removed_segments = 0;  // half-created final segments

  // True when data was lost or a file is malformed. Pre-allocation padding left
  // by a crash is not damage.
  [[nodiscard]] bool damaged() const noexcept;
};

// Scans every segment in `dir`. With `repair`, cuts each segment back to the
// end of its last valid frame (removing torn writes and pre-allocation
// padding) and deletes a final segment whose header was never completed.
// Without it, the directory is left untouched and the report shows what a
// repair would do. Corruption in the middle of a segment is reported but kept:
// later records stay readable because readers resynchronise on the sync marker.
RecoveryReport recover(const std::filesystem::path& dir, bool repair);

}  // namespace fdr

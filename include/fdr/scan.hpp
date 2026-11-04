#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "fdr/record.hpp"

namespace fdr {

struct ScanResult {
  std::uint64_t valid_end = 0;      // offset just past the last valid frame
  std::size_t records = 0;          // valid frames visited
  std::size_t corrupt_regions = 0;  // damaged gaps followed by more valid data
  std::uint64_t skipped_bytes = 0;  // total size of those gaps
  bool torn_tail = false;           // non-zero garbage after the last valid frame
  bool stopped = false;             // the visitor ended the scan early
};

// Called for every valid frame with its record and byte offset in the buffer.
// Return false to stop scanning.
using RecordVisitor = std::function<bool(const Record&, std::uint64_t offset)>;

// Walks the frames in data[start, size). On damage it searches for the next
// sync marker that begins a CRC-valid frame and resumes there, so a corrupt
// record costs only itself. Zero padding left by pre-allocation is not damage.
// With an early stop, `valid_end` and `torn_tail` describe only the part scanned.
ScanResult scan_frames(const std::uint8_t* data, std::size_t size, std::size_t start,
                       const RecordVisitor& visit);

}  // namespace fdr

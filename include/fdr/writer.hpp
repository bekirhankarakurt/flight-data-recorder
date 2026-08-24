#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "fdr/fd.hpp"
#include "fdr/record.hpp"
#include "fdr/recovery.hpp"

namespace fdr {

// When appended data is forced to stable storage with fdatasync(2).
enum class SyncPolicy {
  kNone,         // leave it to the OS (fastest, loses the page cache on power loss)
  kEveryRecord,  // after each record (safest, slowest)
  kBatch,        // after every `batch_records` records
  kInterval,     // on the first append after `interval` has elapsed
};

struct WriterConfig {
  std::filesystem::path dir;
  std::uint64_t segment_bytes = 16ULL * 1024 * 1024;
  SyncPolicy sync = SyncPolicy::kBatch;
  std::uint32_t batch_records = 64;
  std::chrono::milliseconds interval{100};
  bool preallocate = true;
};

constexpr std::uint64_t kMinSegmentBytes = 4096;

struct WriterStats {
  std::uint64_t records = 0;
  std::uint64_t bytes = 0;
  std::uint64_t syncs = 0;
  std::uint64_t segments_created = 0;
};

// Append-only recorder. Opening a directory first runs crash recovery, then
// resumes in the last segment. Timestamps must be non-decreasing across the
// whole recording, including records written by earlier sessions.
class Writer {
 public:
  explicit Writer(WriterConfig config);
  ~Writer();
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  void append(const Record& record);
  // Forces everything appended so far to stable storage.
  void sync();
  // Finalises the current segment (trims padding, syncs). Idempotent; the
  // destructor calls it and swallows errors, so call it to observe them.
  void close();

  [[nodiscard]] const WriterStats& stats() const noexcept { return stats_; }
  // What recovery found when the directory was opened.
  [[nodiscard]] const RecoveryReport& recovery() const noexcept { return recovery_; }

 private:
  void open_new_segment(std::uint64_t base_timestamp_us);
  void resume_segment(const SegmentReport& segment);
  void finalize_segment();
  void apply_sync_policy();

  WriterConfig config_;
  RecoveryReport recovery_;
  WriterStats stats_;
  UniqueFd fd_;
  std::filesystem::path segment_path_;
  std::uint32_t next_sequence_ = 1;
  std::uint64_t offset_ = 0;
  std::uint64_t last_timestamp_us_ = 0;
  bool has_last_timestamp_ = false;
  std::uint32_t unsynced_ = 0;
  std::chrono::steady_clock::time_point last_sync_;
  std::vector<std::uint8_t> frame_;
};

}  // namespace fdr

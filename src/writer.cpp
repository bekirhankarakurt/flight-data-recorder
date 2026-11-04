#include "fdr/writer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include "fdr/segment.hpp"

namespace fdr {
namespace {

// Reserves `bytes` of disk space so later appends cannot fail with ENOSPC and
// do not fragment the file. Falls back to a sparse file where unsupported.
void reserve(const UniqueFd& fd, std::uint64_t bytes, const std::filesystem::path& path) {
  const int rc = ::posix_fallocate(fd.get(), 0, static_cast<off_t>(bytes));
  if (rc == 0) {
    return;
  }
  if (rc != EOPNOTSUPP && rc != EINVAL) {
    throw std::system_error(rc, std::generic_category(), "fallocate " + path.string());
  }
  if (::ftruncate(fd.get(), static_cast<off_t>(bytes)) != 0) {
    throw_errno("truncate", path);
  }
}

}  // namespace

Writer::Writer(WriterConfig config) : config_(std::move(config)) {
  if (config_.segment_bytes < kMinSegmentBytes) {
    throw std::invalid_argument("segment_bytes must be at least 4096");
  }
  if (config_.batch_records == 0) {
    throw std::invalid_argument("batch_records must be at least 1");
  }
  std::filesystem::create_directories(config_.dir);
  recovery_ = recover(config_.dir, true);
  for (auto it = recovery_.segments.rbegin(); it != recovery_.segments.rend(); ++it) {
    if (it->records != 0) {
      last_timestamp_us_ = it->last_timestamp_us;
      has_last_timestamp_ = true;
      break;
    }
  }
  if (!recovery_.segments.empty()) {
    resume_segment(recovery_.segments.back());
  }
  last_sync_ = std::chrono::steady_clock::now();
}

Writer::~Writer() {
  try {
    close();
  } catch (...) {
    // Destructors must not throw; call close() to observe errors.
  }
}

void Writer::resume_segment(const SegmentReport& segment) {
  fd_.reset(::open(segment.path.c_str(), O_WRONLY | O_CLOEXEC));
  if (!fd_.valid()) {
    throw_errno("open", segment.path);
  }
  segment_path_ = segment.path;
  offset_ = segment.valid_end;
  next_sequence_ = segment.sequence + 1;
  if (config_.preallocate && offset_ < config_.segment_bytes) {
    reserve(fd_, config_.segment_bytes, segment_path_);
  }
}

void Writer::open_new_segment(std::uint64_t base_timestamp_us) {
  const std::uint32_t sequence = next_sequence_;
  segment_path_ = config_.dir / segment_file_name(sequence);
  fd_.reset(::open(segment_path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644));
  if (!fd_.valid()) {
    throw_errno("create", segment_path_);
  }
  std::uint8_t header[kSegmentHeaderSize];
  encode_segment_header({sequence, base_timestamp_us}, header);
  pwrite_all(fd_, header, sizeof(header), 0, segment_path_);
  if (config_.preallocate) {
    reserve(fd_, config_.segment_bytes, segment_path_);
  }
  sync_directory(config_.dir);
  offset_ = kSegmentHeaderSize;
  next_sequence_ = sequence + 1;
  ++stats_.segments_created;
}

void Writer::finalize_segment() {
  if (!fd_.valid()) {
    return;
  }
  if (::ftruncate(fd_.get(), static_cast<off_t>(offset_)) != 0) {
    throw_errno("truncate", segment_path_);
  }
  if (config_.sync != SyncPolicy::kNone) {
    if (::fdatasync(fd_.get()) != 0) {
      throw_errno("sync", segment_path_);
    }
    ++stats_.syncs;
    unsynced_ = 0;
  }
  fd_.reset();
}

void Writer::sync() {
  if (!fd_.valid() || unsynced_ == 0) {
    return;
  }
  if (::fdatasync(fd_.get()) != 0) {
    throw_errno("sync", segment_path_);
  }
  ++stats_.syncs;
  unsynced_ = 0;
  last_sync_ = std::chrono::steady_clock::now();
}

void Writer::apply_sync_policy() {
  switch (config_.sync) {
    case SyncPolicy::kNone:
      break;
    case SyncPolicy::kEveryRecord:
      sync();
      break;
    case SyncPolicy::kBatch:
      if (unsynced_ >= config_.batch_records) {
        sync();
      }
      break;
    case SyncPolicy::kInterval:
      if (std::chrono::steady_clock::now() - last_sync_ >= config_.interval) {
        sync();
      }
      break;
  }
}

void Writer::append(const Record& record) {
  if (has_last_timestamp_ && record.timestamp_us < last_timestamp_us_) {
    throw std::invalid_argument("timestamp went backwards");
  }
  frame_.clear();
  encode_frame(record, frame_);
  if (kSegmentHeaderSize + frame_.size() > config_.segment_bytes) {
    throw std::length_error("record does not fit in a segment");
  }
  if (fd_.valid() && offset_ + frame_.size() > config_.segment_bytes) {
    finalize_segment();
  }
  if (!fd_.valid()) {
    open_new_segment(record.timestamp_us);
  }
  pwrite_all(fd_, frame_.data(), frame_.size(), offset_, segment_path_);
  offset_ += frame_.size();
  last_timestamp_us_ = record.timestamp_us;
  has_last_timestamp_ = true;
  ++stats_.records;
  stats_.bytes += frame_.size();
  ++unsynced_;
  apply_sync_policy();
}

void Writer::close() { finalize_segment(); }

}  // namespace fdr

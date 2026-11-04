#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace fdr {

// Owning POSIX file descriptor.
class UniqueFd {
 public:
  UniqueFd() noexcept = default;
  explicit UniqueFd(int fd) noexcept : fd_(fd) {}
  ~UniqueFd() { reset(); }
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  int release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) noexcept;

 private:
  int fd_ = -1;
};

// Throws std::system_error built from the current errno.
[[noreturn]] void throw_errno(const char* what, const std::filesystem::path& path);

// Reads the file from `offset` to its end. Returns an empty vector when the
// offset is at or beyond the end of the file.
[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                                  std::uint64_t offset = 0);

// Writes the whole buffer at `offset`, retrying on EINTR and short writes.
void pwrite_all(const UniqueFd& fd, const std::uint8_t* data, std::size_t size,
                std::uint64_t offset, const std::filesystem::path& path);

// Flushes directory metadata (new or removed entries). Best effort.
void sync_directory(const std::filesystem::path& dir) noexcept;

}  // namespace fdr

#include "fdr/fd.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace fdr {

void UniqueFd::reset(int fd) noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
  }
  fd_ = fd;
}

namespace {

// The single place the variadic open(2) is called.
UniqueFd raw_open(const std::filesystem::path& path, int flags, unsigned mode) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  return UniqueFd(::open(path.c_str(), flags | O_CLOEXEC, mode));
}

}  // namespace

UniqueFd open_file(const std::filesystem::path& path, int flags, unsigned mode) {
  UniqueFd fd = raw_open(path, flags, mode);
  if (!fd.valid()) {
    throw_errno("open", path);
  }
  return fd;
}

void throw_errno(const char* what, const std::filesystem::path& path) {
  const int err = errno;
  throw std::system_error(err, std::generic_category(),
                          std::string(what) + " " + path.string());
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                    std::uint64_t offset) {
  const UniqueFd fd = open_file(path, O_RDONLY);
  struct stat st {};
  if (::fstat(fd.get(), &st) != 0) {
    throw_errno("stat", path);
  }
  const auto size = static_cast<std::uint64_t>(st.st_size);
  if (offset >= size) {
    return {};
  }
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(size - offset));
  std::size_t done = 0;
  while (done < buf.size()) {
    const ssize_t n = ::pread(fd.get(), buf.data() + done, buf.size() - done,
                              static_cast<off_t>(offset + done));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw_errno("read", path);
    }
    if (n == 0) {
      break;  // the file shrank while reading
    }
    done += static_cast<std::size_t>(n);
  }
  buf.resize(done);
  return buf;
}

void pwrite_all(const UniqueFd& fd, const std::uint8_t* data, std::size_t size,
                std::uint64_t offset, const std::filesystem::path& path) {
  std::size_t done = 0;
  while (done < size) {
    const ssize_t n = ::pwrite(fd.get(), data + done, size - done,
                               static_cast<off_t>(offset + done));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw_errno("write", path);
    }
    done += static_cast<std::size_t>(n);
  }
}

void sync_directory(const std::filesystem::path& dir) noexcept {
  const UniqueFd fd = raw_open(dir, O_RDONLY | O_DIRECTORY, 0);
  if (fd.valid()) {
    (void)::fsync(fd.get());
  }
}

}  // namespace fdr

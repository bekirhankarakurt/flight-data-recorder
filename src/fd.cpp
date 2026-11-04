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

void throw_errno(const char* what, const std::filesystem::path& path) {
  const int err = errno;
  throw std::system_error(err, std::generic_category(),
                          std::string(what) + " " + path.string());
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                    std::uint64_t offset) {
  UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.valid()) {
    throw_errno("open", path);
  }
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
  UniqueFd fd(::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (fd.valid()) {
    (void)::fsync(fd.get());
  }
}

}  // namespace fdr

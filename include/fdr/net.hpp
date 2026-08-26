#pragma once

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "fdr/fd.hpp"

namespace fdr {

// UDP socket bound on all IPv4 interfaces. Port 0 picks a free port.
class UdpReceiver {
 public:
  explicit UdpReceiver(std::uint16_t port);

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  // Waits up to `timeout_ms` for one datagram and copies it into `buf`.
  // Returns its size, or nullopt on timeout.
  [[nodiscard]] std::optional<std::size_t> receive(std::uint8_t* buf, std::size_t capacity,
                                                   int timeout_ms);

 private:
  UniqueFd fd_;
  std::uint16_t port_ = 0;
};

// UDP socket that sends datagrams to a fixed IPv4 destination.
class UdpSender {
 public:
  UdpSender(const std::string& host, std::uint16_t port);

  void send(const std::uint8_t* data, std::size_t size);

 private:
  UniqueFd fd_;
  sockaddr_storage addr_{};
  socklen_t addr_len_ = 0;
};

}  // namespace fdr

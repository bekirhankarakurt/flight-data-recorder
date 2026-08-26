#include "fdr/net.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace fdr {
namespace {

[[noreturn]] void throw_net(const std::string& what) {
  const int err = errno;
  throw std::system_error(err, std::generic_category(), what);
}

}  // namespace

UdpReceiver::UdpReceiver(std::uint16_t port) {
  fd_.reset(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd_.valid()) {
    throw_net("socket");
  }
  const int one = 1;
  (void)::setsockopt(fd_.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(fd_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    throw_net("bind udp port " + std::to_string(port));
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(fd_.get(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    throw_net("getsockname");
  }
  port_ = ntohs(addr.sin_port);
}

std::optional<std::size_t> UdpReceiver::receive(std::uint8_t* buf, std::size_t capacity,
                                                int timeout_ms) {
  pollfd pfd{};
  pfd.fd = fd_.get();
  pfd.events = POLLIN;
  const int ready = ::poll(&pfd, 1, timeout_ms);
  if (ready < 0) {
    if (errno == EINTR) {
      return std::nullopt;
    }
    throw_net("poll");
  }
  if (ready == 0) {
    return std::nullopt;
  }
  const ssize_t n = ::recv(fd_.get(), buf, capacity, 0);
  if (n < 0) {
    if (errno == EINTR || errno == EAGAIN) {
      return std::nullopt;
    }
    throw_net("recv");
  }
  return static_cast<std::size_t>(n);
}

UdpSender::UdpSender(const std::string& host, std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* raw = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &raw);
  if (rc != 0) {
    throw std::runtime_error("cannot resolve " + host + ": " + ::gai_strerror(rc));
  }
  const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> info(raw, ::freeaddrinfo);
  std::memcpy(&addr_, info->ai_addr, info->ai_addrlen);
  addr_len_ = info->ai_addrlen;
  fd_.reset(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
  if (!fd_.valid()) {
    throw_net("socket");
  }
}

void UdpSender::send(const std::uint8_t* data, std::size_t size) {
  while (::sendto(fd_.get(), data, size, 0, reinterpret_cast<const sockaddr*>(&addr_),
                  addr_len_) < 0) {
    if (errno != EINTR) {
      throw_net("sendto");
    }
  }
}

}  // namespace fdr

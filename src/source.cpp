#include "fdr/source.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>

#include "fdr/endian.hpp"

namespace fdr {
namespace {

void put_f32(std::uint8_t* p, float v) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  put_le<std::uint32_t>(p, bits);
}

void put_i32(std::uint8_t* p, std::int64_t v) noexcept {
  put_le<std::uint32_t>(p, static_cast<std::uint32_t>(static_cast<std::int32_t>(v)));
}

}  // namespace

SimSource::SimSource(const SimConfig& config)
    : config_(config) {
  if (config_.rate_hz == 0 || config_.rate_hz > 1000000) {
    throw std::invalid_argument("simulation rate must be between 1 and 1000000 Hz");
  }
  state_ = (config.seed * 0x9E3779B97F4A7C15ULL) + 0xD1B54A32D192ED03ULL;
  if (state_ == 0) {
    state_ = kDefaultState;
  }
}

std::uint64_t SimSource::random() noexcept {
  state_ ^= state_ << 13U;
  state_ ^= state_ >> 7U;
  state_ ^= state_ << 17U;
  return state_;
}

float SimSource::noise() noexcept {
  return (static_cast<float>(random() % 2001U) - 1000.0F) / 1000.0F;
}

bool SimSource::next(Record& out) {
  if (config_.count != 0 && produced_ >= config_.count) {
    return false;
  }
  const std::uint64_t period_us = 1000000U / config_.rate_hz;
  const std::uint64_t tick = produced_ / 3;
  out.timestamp_us = config_.start_us + (produced_ * period_us);
  out.channel = static_cast<std::uint16_t>(produced_ % 3);
  switch (out.channel) {
    case 0:
      out.payload.assign(24, 0);
      put_f32(out.payload.data() + 0, noise() * 0.05F);
      put_f32(out.payload.data() + 4, noise() * 0.05F);
      put_f32(out.payload.data() + 8, 9.81F + (noise() * 0.05F));
      put_f32(out.payload.data() + 12, noise() * 0.01F);
      put_f32(out.payload.data() + 16, noise() * 0.01F);
      put_f32(out.payload.data() + 20, noise() * 0.01F);
      break;
    case 1:
      out.payload.assign(12, 0);
      put_i32(out.payload.data() + 0, 399000000 + (static_cast<std::int64_t>(tick) * 12) +
                                          static_cast<std::int64_t>(random() % 5U));
      put_i32(out.payload.data() + 4, 326000000 + (static_cast<std::int64_t>(tick) * 9) +
                                          static_cast<std::int64_t>(random() % 5U));
      put_i32(out.payload.data() + 8, 1000000 + (static_cast<std::int64_t>(tick) * 40));
      break;
    default:
      out.payload.assign(8, 0);
      put_f32(out.payload.data() + 0, 101325.0F - (0.12F * static_cast<float>(tick)) + noise());
      put_f32(out.payload.data() + 4, 15.0F + (noise() * 0.1F));
      break;
  }
  ++produced_;
  return true;
}

UdpSource::UdpSource(std::uint16_t port, std::uint16_t channel, std::uint32_t idle_ms,
                     std::function<bool()> should_stop)
    : receiver_(port),
      channel_(channel),
      idle_ms_(idle_ms),
      should_stop_(std::move(should_stop)),
      buffer_(kMaxPayloadSize + 1) {}

bool UdpSource::next(Record& out) {
  using clock = std::chrono::steady_clock;
  auto last_activity = clock::now();
  while (!should_stop_()) {
    const auto size = receiver_.receive(buffer_.data(), buffer_.size(), 100);
    if (size && *size <= kMaxPayloadSize) {
      out.timestamp_us = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());
      out.channel = channel_;
      out.payload.assign(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(*size));
      return true;
    }
    if (size) {
      last_activity = clock::now();  // oversized datagram: drop it
      continue;
    }
    if (idle_ms_ != 0 && clock::now() - last_activity >= std::chrono::milliseconds(idle_ms_)) {
      return false;
    }
  }
  return false;
}

}  // namespace fdr

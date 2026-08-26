#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "fdr/net.hpp"
#include "fdr/record.hpp"

namespace fdr {

// Where records come from while recording.
class Source {
 public:
  virtual ~Source() = default;
  // Produces the next record; returns false when the source is exhausted.
  [[nodiscard]] virtual bool next(Record& out) = 0;
};

struct SimConfig {
  std::uint64_t seed = 1;
  std::uint64_t start_us = 0;
  std::uint32_t rate_hz = 100;  // records per second, 1..1,000,000
  std::uint64_t count = 1000;   // 0 = unbounded
};

// Deterministic telemetry generator that stands in for real hardware. It cycles
// through three channels:
//   0  IMU   6 x float32  ax ay az (m/s^2), gx gy gz (rad/s)
//   1  GPS   3 x int32    lat, lon (1e-7 deg), altitude (mm)
//   2  baro  2 x float32  pressure (Pa), temperature (C)
// Integers and floats are little-endian. The same seed yields the same bytes.
class SimSource final : public Source {
 public:
  explicit SimSource(const SimConfig& config);
  [[nodiscard]] bool next(Record& out) override;

 private:
  std::uint64_t random() noexcept;
  float noise() noexcept;  // uniform in [-1, 1]

  SimConfig config_;
  std::uint64_t state_;
  std::uint64_t produced_ = 0;
};

// Receives one UDP datagram per record (e.g. a telemetry feed from a flight
// computer), stamping each with the wall clock on arrival.
class UdpSource final : public Source {
 public:
  // `idle_ms` ends the source after that much silence (0 = wait forever).
  // `should_stop` is polled about every 100 ms.
  UdpSource(std::uint16_t port, std::uint16_t channel, std::uint32_t idle_ms,
            std::function<bool()> should_stop);
  [[nodiscard]] bool next(Record& out) override;
  [[nodiscard]] std::uint16_t port() const noexcept { return receiver_.port(); }

 private:
  UdpReceiver receiver_;
  std::uint16_t channel_;
  std::uint32_t idle_ms_;
  std::function<bool()> should_stop_;
  std::vector<std::uint8_t> buffer_;
};

}  // namespace fdr

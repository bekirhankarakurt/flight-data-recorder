#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "fdr/reader.hpp"
#include "fdr/sink.hpp"

namespace fdr {

// Time source used to pace a replay; injectable so pacing is testable
// without sleeping.
class Pacer {
 public:
  virtual ~Pacer() = default;
  [[nodiscard]] virtual std::chrono::microseconds now() = 0;
  virtual void sleep_until(std::chrono::microseconds when) = 0;
};

// Real-time pacer on the monotonic clock.
class SteadyPacer : public Pacer {
 public:
  [[nodiscard]] std::chrono::microseconds now() override;
  void sleep_until(std::chrono::microseconds when) override;
};

struct ReplayOptions {
  std::uint64_t from_us = 0;
  std::uint64_t to_us = UINT64_MAX;
  std::optional<std::uint16_t> channel;
  // 1.0 reproduces the original timing, 2.0 runs twice as fast, 0 sends
  // everything as fast as possible. Must not be negative.
  double speed = 1.0;
};

// Emits the selected records to `sink`, spacing them like the originals
// divided by `speed`. Returns the number of records emitted.
std::size_t replay(const Reader& reader, Sink& sink, const ReplayOptions& options,
                   Pacer& pacer);

}  // namespace fdr

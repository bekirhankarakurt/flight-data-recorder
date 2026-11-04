#include "fdr/replay.hpp"

#include <cmath>
#include <stdexcept>
#include <thread>

namespace fdr {

std::chrono::microseconds SteadyPacer::now() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
}

void SteadyPacer::sleep_until(std::chrono::microseconds when) {
  std::this_thread::sleep_until(std::chrono::steady_clock::time_point(when));
}

std::size_t replay(const Reader& reader, Sink& sink, const ReplayOptions& options,
                   Pacer& pacer) {
  if (!(options.speed >= 0.0) || std::isinf(options.speed)) {
    throw std::invalid_argument("replay speed must be a finite, non-negative number");
  }
  const bool paced = options.speed > 0.0;
  const std::chrono::microseconds start = pacer.now();
  std::uint64_t first_us = 0;
  bool have_first = false;
  std::size_t emitted = 0;
  reader.query(options.from_us, options.to_us, [&](const Record& record) {
    if (options.channel && record.channel != *options.channel) {
      return true;
    }
    if (!have_first) {
      first_us = record.timestamp_us;
      have_first = true;
    }
    if (paced) {
      const double elapsed = static_cast<double>(record.timestamp_us - first_us) / options.speed;
      const auto target = start + std::chrono::microseconds(static_cast<std::int64_t>(elapsed));
      if (pacer.now() < target) {
        pacer.sleep_until(target);
      }
    }
    sink.emit(record);
    ++emitted;
    return true;
  });
  return emitted;
}

}  // namespace fdr

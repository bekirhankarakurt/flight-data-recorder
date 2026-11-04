// fdr - command line front end for the flight data recorder.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fdr/reader.hpp"
#include "fdr/recovery.hpp"
#include "fdr/replay.hpp"
#include "fdr/sink.hpp"
#include "fdr/source.hpp"
#include "fdr/timearg.hpp"
#include "fdr/writer.hpp"

namespace {

constexpr int kExitError = 1;
constexpr int kExitDamaged = 2;
constexpr int kExitUsage = 64;

const char kUsage[] =
    R"(usage: fdr <command> [options] DIR

commands:
  record  DIR   record telemetry into DIR (simulated source or UDP feed)
  recover DIR   repair torn writes and padding left by a crash
  info    DIR   summarise a recording
  export  DIR   write records as CSV
  replay  DIR   re-emit records at original or scaled speed (CSV or UDP)

record options:
  --source sim|udp     data source (default sim)
  --count N            stop after N records (sim default 1000; udp default unlimited)
  --rate HZ            sim records per second (default 100)
  --seed N             sim random seed (default 1)
  --start-us N         sim first timestamp in us (default: now)
  --realtime           sim: pace output at --rate instead of running flat out
  --port N             udp listen port
  --channel N          udp channel id stored with each datagram (default 0)
  --idle-ms N          udp: stop after N ms of silence (default 0 = never)
  --segment-kib N      segment size in KiB (default 16384)
  --sync P             none | every | batch | interval (default batch)
  --batch N            records per sync for 'batch' (default 64)
  --interval-ms N      milliseconds between syncs for 'interval' (default 100)
  --no-prealloc        do not pre-allocate segment files

recover options:
  --dry-run            report only; exit 2 if damage is found

export / replay options:
  --from T --to T      time window: absolute us, or +2.5s / +300ms / +40us
                       relative to the first record
  --channel N          only this channel
  -o FILE              export: write to FILE instead of stdout
  --speed X|max        replay speed factor (default 1; max = no pacing)
  --udp HOST:PORT      replay: send payloads as UDP datagrams instead of CSV
)";

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int /*signal*/) { g_stop = 1; }

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Minimal option parser: --name value, boolean flags, positional arguments.
class Args {
 public:
  Args(const std::vector<std::string>& argv, const std::set<std::string>& value_options,
       const std::set<std::string>& flags) {
    for (std::size_t i = 0; i < argv.size(); ++i) {
      const std::string& a = argv[i];
      if (a.size() < 2 || a[0] != '-') {
        positional_.push_back(a);
      } else if (flags.count(a) != 0) {
        flags_.insert(a);
      } else if (value_options.count(a) != 0) {
        if (i + 1 >= argv.size()) {
          throw UsageError("option " + a + " needs a value");
        }
        values_[a] = argv[++i];
      } else {
        throw UsageError("unknown option " + a);
      }
    }
  }

  [[nodiscard]] bool has(const std::string& flag) const { return flags_.count(flag) != 0; }

  [[nodiscard]] std::optional<std::string> value(const std::string& name) const {
    const auto it = values_.find(name);
    if (it == values_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::string str(const std::string& name, const std::string& fallback) const {
    return value(name).value_or(fallback);
  }

  [[nodiscard]] std::uint64_t u64(const std::string& name, std::uint64_t fallback) const {
    const auto v = value(name);
    return v ? fdr::parse_u64(*v, name) : fallback;
  }

  [[nodiscard]] std::string directory() const {
    if (positional_.size() != 1) {
      throw UsageError("expected exactly one DIR argument");
    }
    return positional_[0];
  }

 private:
  std::vector<std::string> positional_;
  std::set<std::string> flags_;
  std::map<std::string, std::string> values_;
};

std::uint64_t wall_clock_us() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

void install_signal_handlers() {
  struct sigaction action {};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  ::sigaction(SIGINT, &action, nullptr);
  ::sigaction(SIGTERM, &action, nullptr);
}

fdr::SyncPolicy parse_sync(const std::string& name) {
  if (name == "none") return fdr::SyncPolicy::kNone;
  if (name == "every") return fdr::SyncPolicy::kEveryRecord;
  if (name == "batch") return fdr::SyncPolicy::kBatch;
  if (name == "interval") return fdr::SyncPolicy::kInterval;
  throw UsageError("--sync must be none, every, batch or interval");
}

std::uint16_t parse_port(const Args& args, const std::string& name) {
  const std::uint64_t port = args.u64(name, 0);
  if (port > 65535) {
    throw UsageError(name + " must be at most 65535");
  }
  return static_cast<std::uint16_t>(port);
}

int cmd_record(const std::vector<std::string>& argv) {
  const Args args(argv,
                  {"--source", "--count", "--rate", "--seed", "--start-us", "--port", "--channel",
                   "--idle-ms", "--segment-kib", "--sync", "--batch", "--interval-ms"},
                  {"--realtime", "--no-prealloc"});
  fdr::WriterConfig config;
  config.dir = args.directory();
  config.segment_bytes = args.u64("--segment-kib", 16384) * 1024;
  config.sync = parse_sync(args.str("--sync", "batch"));
  config.batch_records = static_cast<std::uint32_t>(args.u64("--batch", 64));
  config.interval = std::chrono::milliseconds(args.u64("--interval-ms", 100));
  config.preallocate = !args.has("--no-prealloc");

  fdr::Writer writer(config);
  const std::string kind = args.str("--source", "sim");
  std::unique_ptr<fdr::Source> source;
  if (kind == "sim") {
    fdr::SimConfig sim;
    sim.seed = args.u64("--seed", 1);
    // Default: now, but never earlier than the newest record already stored.
    const auto newest = writer.last_timestamp_us();
    const std::uint64_t floor_us = newest ? *newest + 1 : 0;
    sim.start_us = args.u64("--start-us", std::max(wall_clock_us(), floor_us));
    sim.rate_hz = static_cast<std::uint32_t>(args.u64("--rate", 100));
    sim.count = args.u64("--count", 1000);
    source = std::make_unique<fdr::SimSource>(sim);
  } else if (kind == "udp") {
    if (!args.value("--port")) {
      throw UsageError("--source udp needs --port");
    }
    source = std::make_unique<fdr::UdpSource>(
        parse_port(args, "--port"), parse_port(args, "--channel"),
        static_cast<std::uint32_t>(args.u64("--idle-ms", 0)), [] { return g_stop != 0; });
  } else {
    throw UsageError("--source must be sim or udp");
  }
  const std::uint64_t limit = kind == "udp" ? args.u64("--count", 0) : 0;
  const bool realtime = args.has("--realtime") && kind == "sim";

  const fdr::RecoveryReport& recovered = writer.recovery();
  if (recovered.truncated_bytes != 0 || recovered.removed_segments != 0 ||
      recovered.corrupt_regions != 0) {
    std::cerr << "recovered " << config.dir.string() << ": " << recovered.records
              << " records kept, " << recovered.truncated_bytes << " bytes trimmed, "
              << recovered.corrupt_regions << " corrupt regions\n";
  }

  install_signal_handlers();
  const auto wall_start = std::chrono::steady_clock::now();
  std::uint64_t first_us = 0;
  std::uint64_t count = 0;
  fdr::Record record;
  while (g_stop == 0 && (limit == 0 || count < limit) && source->next(record)) {
    if (count == 0) {
      first_us = record.timestamp_us;
    }
    if (realtime) {
      std::this_thread::sleep_until(wall_start +
                                    std::chrono::microseconds(record.timestamp_us - first_us));
    }
    writer.append(record);
    ++count;
  }
  writer.close();
  std::cout << "recorded " << count << " records to " << config.dir.string() << " ("
            << writer.stats().segments_created << " new segments, " << writer.stats().bytes
            << " bytes, " << writer.stats().syncs << " syncs)\n";
  return 0;
}

int cmd_recover(const std::vector<std::string>& argv) {
  const Args args(argv, {}, {"--dry-run"});
  const bool dry = args.has("--dry-run");
  const fdr::RecoveryReport r = fdr::recover(args.directory(), !dry);
  std::cout << (dry ? "dry run (no files changed)\n" : "")
            << "segments:         " << r.segments.size() << "\n"
            << "records:          " << r.records << "\n"
            << "torn segments:    " << r.torn_segments << "\n"
            << "corrupt regions:  " << r.corrupt_regions << " (" << r.skipped_bytes
            << " bytes skipped)\n"
            << "removed segments: " << r.removed_segments << "\n"
            << (dry ? "bytes to trim:    " : "bytes trimmed:    ") << r.truncated_bytes << "\n";
  return dry && r.damaged() ? kExitDamaged : 0;
}

int cmd_info(const std::vector<std::string>& argv) {
  const Args args(argv, {}, {});
  const std::string dir = args.directory();
  const fdr::Reader reader(dir);
  const fdr::ReadSummary& s = reader.summary();
  std::cout << "directory:  " << dir << "\n"
            << "segments:   " << s.segments.size() << "\n"
            << "records:    " << s.records << "\n";
  if (s.records != 0) {
    const double seconds = static_cast<double>(s.last_timestamp_us - s.first_timestamp_us) / 1e6;
    std::cout << "time range: " << s.first_timestamp_us << " .. " << s.last_timestamp_us
              << " us (" << std::fixed << std::setprecision(3) << seconds << " s)\n";
  }
  std::cout << "corruption: " << s.corrupt_regions << " regions, " << s.skipped_bytes
            << " bytes skipped\n";
  for (const auto& [channel, n] : s.channel_counts) {
    std::cout << "channel " << channel << ": " << n << " records\n";
  }
  std::cout << "\n" << std::setw(8) << "segment" << std::setw(10) << "records" << std::setw(12)
            << "bytes" << std::setw(18) << "first_us" << std::setw(18) << "last_us" << "\n";
  for (const auto& seg : s.segments) {
    std::cout << std::setw(8) << seg.sequence << std::setw(10) << seg.records << std::setw(12)
              << seg.valid_bytes << std::setw(18) << seg.first_timestamp_us << std::setw(18)
              << seg.last_timestamp_us << "\n";
  }
  return 0;
}

struct Selection {
  fdr::ReplayOptions options;
};

Selection parse_selection(const Args& args, const fdr::Reader& reader) {
  Selection sel;
  const std::uint64_t base = reader.summary().first_timestamp_us;
  if (const auto from = args.value("--from")) {
    sel.options.from_us = fdr::parse_time_us(*from, base, "--from");
  }
  if (const auto to = args.value("--to")) {
    sel.options.to_us = fdr::parse_time_us(*to, base, "--to");
  }
  if (args.value("--channel")) {
    sel.options.channel = parse_port(args, "--channel");
  }
  return sel;
}

int cmd_export(const std::vector<std::string>& argv) {
  const Args args(argv, {"--from", "--to", "--channel", "-o"}, {});
  const fdr::Reader reader(args.directory());
  const Selection sel = parse_selection(args, reader);

  std::ofstream file;
  const auto path = args.value("-o");
  if (path) {
    file.open(*path);
    if (!file) {
      throw std::runtime_error("cannot open " + *path + " for writing");
    }
  }
  fdr::CsvSink sink(path ? static_cast<std::ostream&>(file) : std::cout);
  fdr::SteadyPacer pacer;
  fdr::ReplayOptions options = sel.options;
  options.speed = 0;
  const std::size_t n = fdr::replay(reader, sink, options, pacer);
  if (path) {
    file.flush();
    if (!file) {
      throw std::runtime_error("write to " + *path + " failed");
    }
    std::cout << "exported " << n << " records to " << *path << "\n";
  }
  return 0;
}

int cmd_replay(const std::vector<std::string>& argv) {
  const Args args(argv, {"--from", "--to", "--channel", "--speed", "--udp"}, {});
  const fdr::Reader reader(args.directory());
  Selection sel = parse_selection(args, reader);

  const std::string speed = args.str("--speed", "1");
  if (speed == "max") {
    sel.options.speed = 0;
  } else {
    char* end = nullptr;
    sel.options.speed = std::strtod(speed.c_str(), &end);
    if (speed.empty() || end != speed.c_str() + speed.size() || !(sel.options.speed > 0)) {
      throw UsageError("--speed must be a positive number or 'max'");
    }
  }

  std::unique_ptr<fdr::Sink> sink;
  if (const auto target = args.value("--udp")) {
    const auto colon = target->rfind(':');
    if (colon == std::string::npos || colon == 0) {
      throw UsageError("--udp expects HOST:PORT");
    }
    const std::uint64_t port = fdr::parse_u64(target->substr(colon + 1), "--udp port");
    if (port == 0 || port > 65535) {
      throw UsageError("--udp port must be 1..65535");
    }
    sink = std::make_unique<fdr::UdpSink>(target->substr(0, colon),
                                          static_cast<std::uint16_t>(port));
  } else {
    sink = std::make_unique<fdr::CsvSink>(std::cout);
  }
  install_signal_handlers();
  struct InterruptiblePacer final : fdr::SteadyPacer {
    void sleep_until(std::chrono::microseconds when) override {
      // Sleep in short slices so Ctrl-C ends the replay promptly.
      while (g_stop == 0 && now() < when) {
        fdr::SteadyPacer::sleep_until(std::min(when, now() + std::chrono::milliseconds(50)));
      }
      if (g_stop != 0) {
        throw std::runtime_error("interrupted");
      }
    }
  } pacer;
  const std::size_t n = fdr::replay(reader, *sink, sel.options, pacer);
  std::cerr << "replayed " << n << " records\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
    (args.empty() ? std::cerr : std::cout) << kUsage;
    return args.empty() ? kExitUsage : 0;
  }
  const std::string& command = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());
  try {
    if (command == "record") return cmd_record(rest);
    if (command == "recover") return cmd_recover(rest);
    if (command == "info") return cmd_info(rest);
    if (command == "export") return cmd_export(rest);
    if (command == "replay") return cmd_replay(rest);
    throw UsageError("unknown command '" + command + "'");
  } catch (const UsageError& e) {
    std::cerr << "fdr: " << e.what() << "\nrun 'fdr --help' for usage\n";
    return kExitUsage;
  } catch (const std::invalid_argument& e) {
    std::cerr << "fdr: " << e.what() << "\n";
    return kExitUsage;
  } catch (const std::exception& e) {
    std::cerr << "fdr: error: " << e.what() << "\n";
    return kExitError;
  }
}

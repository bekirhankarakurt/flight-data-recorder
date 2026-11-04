#include <sstream>
#include <thread>

#include "fdr/net.hpp"
#include "fdr/reader.hpp"
#include "fdr/replay.hpp"
#include "fdr/sink.hpp"
#include "fdr/source.hpp"
#include "fdr/timearg.hpp"
#include "testing.hpp"

namespace {

using std::chrono::microseconds;

// Virtual clock: sleeping just advances time and logs the targets.
class FakePacer final : public fdr::Pacer {
 public:
  microseconds now() override { return now_; }
  void sleep_until(microseconds when) override {
    targets.push_back(when);
    now_ = when;
  }
  std::vector<microseconds> targets;

 private:
  microseconds now_{5000};
};

class CollectSink final : public fdr::Sink {
 public:
  void emit(const fdr::Record& r) override { records.push_back(r); }
  std::vector<fdr::Record> records;
};

std::vector<fdr::Record> fixed_records() {
  std::vector<fdr::Record> v;
  for (const std::uint64_t ts : {1000ULL, 1000ULL, 3000ULL, 6000ULL, 10000ULL}) {
    fdr::Record r;
    r.timestamp_us = ts;
    r.channel = static_cast<std::uint16_t>(v.size() % 2);
    r.payload = {static_cast<std::uint8_t>(v.size())};
    v.push_back(r);
  }
  return v;
}

void expect_throws(const std::function<void()>& fn) {
  bool threw = false;
  try {
    fn();
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

TEST(replay_follows_original_timing) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fixed_records());
  const fdr::Reader reader(dir.path());
  CollectSink sink;
  FakePacer pacer;
  fdr::ReplayOptions options;
  CHECK_EQ(fdr::replay(reader, sink, options, pacer), std::size_t{5});
  CHECK_EQ(sink.records.size(), std::size_t{5});
  // First two share a timestamp so only 3 sleeps happen, at start + (ts - 1000).
  const std::vector<microseconds> expected = {microseconds{7000}, microseconds{10000},
                                              microseconds{14000}};
  CHECK(pacer.targets == expected);
}

TEST(replay_speed_scales_the_schedule) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fixed_records());
  const fdr::Reader reader(dir.path());
  CollectSink sink;
  FakePacer pacer;
  fdr::ReplayOptions options;
  options.speed = 2.0;
  (void)fdr::replay(reader, sink, options, pacer);
  const std::vector<microseconds> expected = {microseconds{6000}, microseconds{7500},
                                              microseconds{9500}};
  CHECK(pacer.targets == expected);
}

TEST(replay_speed_zero_never_sleeps) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fixed_records());
  const fdr::Reader reader(dir.path());
  CollectSink sink;
  FakePacer pacer;
  fdr::ReplayOptions options;
  options.speed = 0;
  CHECK_EQ(fdr::replay(reader, sink, options, pacer), std::size_t{5});
  CHECK(pacer.targets.empty());
}

TEST(replay_rejects_bad_speed) {
  fdr_test::TempDir dir;
  const fdr::Reader reader(dir.path());
  CollectSink sink;
  FakePacer pacer;
  fdr::ReplayOptions options;
  options.speed = -1.0;
  expect_throws([&] { (void)fdr::replay(reader, sink, options, pacer); });
}

TEST(replay_filters_by_time_and_channel) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fixed_records());
  const fdr::Reader reader(dir.path());
  CollectSink sink;
  FakePacer pacer;
  fdr::ReplayOptions options;
  options.speed = 0;
  options.from_us = 2000;
  options.to_us = 9000;
  options.channel = 0;
  CHECK_EQ(fdr::replay(reader, sink, options, pacer), std::size_t{1});
  CHECK_EQ(sink.records[0].timestamp_us, std::uint64_t{3000});
}

TEST(csv_sink_format) {
  std::ostringstream out;
  fdr::CsvSink sink(out);
  fdr::Record r;
  r.timestamp_us = 12345;
  r.channel = 7;
  r.payload = {0x00, 0xAB, 0x0F};
  sink.emit(r);
  r.payload.clear();
  sink.emit(r);
  CHECK_EQ(out.str(), std::string("timestamp_us,channel,length,payload_hex\n"
                                  "12345,7,3,00ab0f\n"
                                  "12345,7,0,\n"));
}

TEST(udp_sink_delivers_payloads_over_loopback) {
  fdr::UdpReceiver receiver(0);
  fdr::UdpSink sink("127.0.0.1", receiver.port());
  const auto records = fdr_test::make_records(20, 41, 200);
  for (const auto& r : records) {
    sink.emit(r);
  }
  std::vector<std::uint8_t> buf(fdr::kMaxPayloadSize + 1);
  for (const auto& r : records) {
    const auto n = receiver.receive(buf.data(), buf.size(), 1000);
    CHECK(n.has_value());
    if (n) {
      CHECK(std::vector<std::uint8_t>(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*n)) ==
            r.payload);
    }
  }
  CHECK(!receiver.receive(buf.data(), buf.size(), 20).has_value());
}

TEST(udp_source_records_datagrams_then_goes_idle) {
  fdr::UdpSource source(0, 5, 150, [] { return false; });
  fdr::UdpSender sender("127.0.0.1", source.port());
  const std::vector<std::uint8_t> a = {1, 2, 3};
  const std::vector<std::uint8_t> b = {9};
  sender.send(a.data(), a.size());
  sender.send(b.data(), b.size());
  fdr::Record r;
  CHECK(source.next(r));
  CHECK(r.payload == a);
  CHECK_EQ(r.channel, std::uint16_t{5});
  CHECK(r.timestamp_us > 1600000000000000ULL);
  CHECK(source.next(r));
  CHECK(r.payload == b);
  CHECK(!source.next(r));
}

TEST(udp_source_stops_on_request) {
  fdr::UdpSource source(0, 0, 0, [] { return true; });
  fdr::Record r;
  CHECK(!source.next(r));
}

TEST(sim_source_is_deterministic_and_well_formed) {
  fdr::SimConfig config;
  config.seed = 7;
  config.start_us = 1000000;
  config.rate_hz = 200;
  config.count = 30;
  fdr::SimSource a(config);
  fdr::SimSource b(config);
  config.seed = 8;
  fdr::SimSource c(config);
  fdr::Record ra;
  fdr::Record rb;
  fdr::Record rc;
  std::size_t n = 0;
  bool differs = false;
  std::uint64_t prev = 0;
  while (a.next(ra)) {
    CHECK(b.next(rb));
    CHECK(c.next(rc));
    CHECK(ra == rb);
    differs = differs || ra != rc;
    CHECK_EQ(ra.timestamp_us, 1000000 + n * 5000);
    CHECK(n == 0 || ra.timestamp_us > prev);
    CHECK_EQ(ra.channel, static_cast<std::uint16_t>(n % 3));
    const std::size_t sizes[3] = {24, 12, 8};
    CHECK_EQ(ra.payload.size(), sizes[n % 3]);
    prev = ra.timestamp_us;
    ++n;
  }
  CHECK_EQ(n, std::size_t{30});
  CHECK(!b.next(rb));
  CHECK(differs);
}

TEST(sim_source_rejects_bad_rate) {
  fdr::SimConfig config;
  config.rate_hz = 0;
  expect_throws([&] { fdr::SimSource s(config); });
  config.rate_hz = 2000000;
  expect_throws([&] { fdr::SimSource s(config); });
}

TEST(time_arguments_parse_absolute_and_relative) {
  CHECK_EQ(fdr::parse_u64("42", "x"), std::uint64_t{42});
  CHECK_EQ(fdr::parse_time_us("1700000000000000", 5, "x"), 1700000000000000ULL);
  CHECK_EQ(fdr::parse_time_us("+2s", 1000, "x"), std::uint64_t{2001000});
  CHECK_EQ(fdr::parse_time_us("+1.5s", 0, "x"), std::uint64_t{1500000});
  CHECK_EQ(fdr::parse_time_us("+250ms", 10, "x"), std::uint64_t{250010});
  CHECK_EQ(fdr::parse_time_us("+40us", 0, "x"), std::uint64_t{40});
  for (const char* bad : {"", "-1", "abc", "12x", "+5", "+s", "+1m", "+1.2.3s", "2s",
                          "99999999999999999999"}) {
    expect_throws([&] { (void)fdr::parse_time_us(bad, 0, "x"); });
  }
  expect_throws([] { (void)fdr::parse_u64("1e3", "x"); });
}

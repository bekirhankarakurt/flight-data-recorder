#include <algorithm>

#include "fdr/reader.hpp"
#include "fdr/segment.hpp"
#include "fdr/writer.hpp"
#include "testing.hpp"

namespace fs = std::filesystem;

namespace {

std::vector<fdr::Record> read_back(const fs::path& dir) {
  const fdr::Reader reader(dir);
  std::vector<fdr::Record> out;
  reader.query(0, UINT64_MAX, [&](const fdr::Record& r) {
    out.push_back(r);
    return true;
  });
  return out;
}

}  // namespace

TEST(round_trip_across_many_segments) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(500, 1);
  fdr_test::write_all(dir.path(), records);
  CHECK(read_back(dir.path()) == records);
  const fdr::Reader reader(dir.path());
  CHECK(reader.summary().segments.size() > 5);
  CHECK_EQ(reader.summary().records, records.size());
  CHECK_EQ(reader.summary().corrupt_regions, std::size_t{0});
  CHECK_EQ(reader.summary().first_timestamp_us, records.front().timestamp_us);
  CHECK_EQ(reader.summary().last_timestamp_us, records.back().timestamp_us);
}

TEST(closed_segments_are_trimmed_and_open_one_is_preallocated) {
  fdr_test::TempDir dir;
  fdr::WriterConfig config;
  config.dir = dir.path();
  config.segment_bytes = 8192;
  fdr::Writer writer(config);
  for (const auto& r : fdr_test::make_records(10, 2)) {
    writer.append(r);
  }
  const auto open_files = fdr::list_segments(dir.path());
  CHECK_EQ(open_files.size(), std::size_t{1});
  CHECK_EQ(fs::file_size(open_files[0].path), std::uint64_t{8192});
  writer.close();
  CHECK(fs::file_size(open_files[0].path) < 8192);
}

TEST(no_preallocation_when_disabled) {
  fdr_test::TempDir dir;
  fdr::WriterConfig config;
  config.dir = dir.path();
  config.preallocate = false;
  fdr::Writer writer(config);
  writer.append(fdr_test::make_records(1, 3)[0]);
  CHECK(fs::file_size(fdr::list_segments(dir.path())[0].path) < 4096);
}

TEST(reopen_resumes_in_last_segment_and_continues_sequence) {
  fdr_test::TempDir dir;
  auto records = fdr_test::make_records(200, 4);
  const std::vector<fdr::Record> first(records.begin(), records.begin() + 100);
  const std::vector<fdr::Record> second(records.begin() + 100, records.end());
  fdr_test::write_all(dir.path(), first);
  const std::size_t segments_after_first = fdr::list_segments(dir.path()).size();
  fdr_test::write_all(dir.path(), second);
  CHECK(read_back(dir.path()) == records);
  const auto files = fdr::list_segments(dir.path());
  CHECK(files.size() > segments_after_first);
  for (std::size_t i = 0; i < files.size(); ++i) {
    CHECK_EQ(files[i].sequence, static_cast<std::uint32_t>(i + 1));
  }
}

TEST(append_rejects_backwards_time_and_oversized_records) {
  fdr_test::TempDir dir;
  fdr::WriterConfig config;
  config.dir = dir.path();
  fdr::Writer writer(config);
  fdr::Record r;
  r.timestamp_us = 100;
  writer.append(r);
  r.timestamp_us = 99;
  bool backwards = false;
  try {
    writer.append(r);
  } catch (const std::invalid_argument&) {
    backwards = true;
  }
  CHECK(backwards);
  r.timestamp_us = 100;
  r.payload.resize(fdr::kMaxPayloadSize + 1);
  bool oversized = false;
  try {
    writer.append(r);
  } catch (const std::length_error&) {
    oversized = true;
  }
  CHECK(oversized);
  CHECK_EQ(writer.stats().records, std::uint64_t{1});
}

TEST(timestamps_must_not_go_backwards_across_sessions) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fdr_test::make_records(5, 5, 10, 5000));
  fdr::WriterConfig config;
  config.dir = dir.path();
  fdr::Writer writer(config);
  fdr::Record r;
  r.timestamp_us = 1;
  bool threw = false;
  try {
    writer.append(r);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(invalid_config_is_rejected) {
  fdr_test::TempDir dir;
  fdr::WriterConfig config;
  config.dir = dir.path();
  config.segment_bytes = 100;
  bool small = false;
  try {
    fdr::Writer w(config);
  } catch (const std::invalid_argument&) {
    small = true;
  }
  CHECK(small);
  config.segment_bytes = 4096;
  config.batch_records = 0;
  bool batch = false;
  try {
    fdr::Writer w(config);
  } catch (const std::invalid_argument&) {
    batch = true;
  }
  CHECK(batch);
}

TEST(sync_policies_issue_the_expected_number_of_syncs) {
  const auto records = fdr_test::make_records(25, 6, 10);
  struct Case {
    fdr::SyncPolicy policy;
    std::uint64_t expected_before_close;
  };
  for (const Case c : {Case{fdr::SyncPolicy::kNone, 0}, Case{fdr::SyncPolicy::kEveryRecord, 25},
                       Case{fdr::SyncPolicy::kBatch, 2}}) {
    fdr_test::TempDir dir;
    fdr::WriterConfig config;
    config.dir = dir.path();
    config.segment_bytes = 1U << 20;
    config.sync = c.policy;
    config.batch_records = 10;
    fdr::Writer writer(config);
    for (const auto& r : records) {
      writer.append(r);
    }
    CHECK_EQ(writer.stats().syncs, c.expected_before_close);
  }
}

TEST(interval_policy_syncs_only_after_the_interval) {
  fdr_test::TempDir dir;
  fdr::WriterConfig config;
  config.dir = dir.path();
  config.sync = fdr::SyncPolicy::kInterval;
  config.interval = std::chrono::milliseconds(0);
  fdr::Writer writer(config);
  for (const auto& r : fdr_test::make_records(5, 7, 10)) {
    writer.append(r);
  }
  CHECK_EQ(writer.stats().syncs, std::uint64_t{5});

  fdr_test::TempDir dir2;
  config.dir = dir2.path();
  config.interval = std::chrono::hours(1);
  fdr::Writer lazy(config);
  for (const auto& r : fdr_test::make_records(5, 7, 10)) {
    lazy.append(r);
  }
  CHECK_EQ(lazy.stats().syncs, std::uint64_t{0});
}

TEST(time_range_queries_match_brute_force) {
  const auto records = fdr_test::make_records(600, 8, 60);
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), records, 8192);
  const std::uint64_t t0 = records.front().timestamp_us;
  const std::uint64_t t1 = records.back().timestamp_us;
  const std::uint64_t span = t1 - t0;

  for (const std::size_t stride : {std::size_t{1}, std::size_t{7}, std::size_t{64}}) {
    const fdr::Reader reader(dir.path(), stride);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> windows = {
        {0, UINT64_MAX},          {0, t0 - 1},
        {t0, t0},                 {t1, t1},
        {t1 + 1, UINT64_MAX},     {t0 + span / 3, t0 + span / 2},
        {records[100].timestamp_us, records[100].timestamp_us},
        {records[299].timestamp_us, records[450].timestamp_us},
        {t0 + span / 2, t0 + span / 3}};
    for (const auto& w : windows) {
      std::vector<fdr::Record> expected;
      for (const auto& r : records) {
        if (r.timestamp_us >= w.first && r.timestamp_us <= w.second) {
          expected.push_back(r);
        }
      }
      std::vector<fdr::Record> got;
      const std::size_t n = reader.query(w.first, w.second, [&](const fdr::Record& r) {
        got.push_back(r);
        return true;
      });
      CHECK_EQ(n, expected.size());
      CHECK(got == expected);
    }
  }
}

TEST(index_is_sparse_and_sorted) {
  const auto records = fdr_test::make_records(300, 9, 30);
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), records, 8192);
  const fdr::Reader sparse(dir.path(), 50);
  const fdr::Reader dense(dir.path(), 1);
  CHECK(sparse.index().size() < dense.index().size());
  CHECK_EQ(dense.index().size(), records.size());
  CHECK(std::is_sorted(sparse.index().begin(), sparse.index().end(),
                       [](const fdr::IndexEntry& a, const fdr::IndexEntry& b) {
                         return a.timestamp_us < b.timestamp_us;
                       }));
}

TEST(query_stops_when_visitor_returns_false) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fdr_test::make_records(100, 10));
  const fdr::Reader reader(dir.path());
  std::size_t seen = 0;
  const std::size_t n = reader.query(0, UINT64_MAX, [&](const fdr::Record&) {
    return ++seen < 3;
  });
  CHECK_EQ(n, std::size_t{3});
}

TEST(reader_handles_empty_and_missing_directories) {
  fdr_test::TempDir dir;
  const fdr::Reader empty(dir.path());
  CHECK_EQ(empty.summary().records, std::size_t{0});
  CHECK_EQ(empty.query(0, UINT64_MAX, [](const fdr::Record&) { return true; }), std::size_t{0});
  const fdr::Reader missing(dir.path() / "nope");
  CHECK_EQ(missing.summary().segments.size(), std::size_t{0});
}

TEST(channel_counts_are_reported) {
  const auto records = fdr_test::make_records(200, 11);
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), records);
  std::map<std::uint16_t, std::size_t> expected;
  for (const auto& r : records) {
    ++expected[r.channel];
  }
  CHECK(fdr::Reader(dir.path()).summary().channel_counts == expected);
}

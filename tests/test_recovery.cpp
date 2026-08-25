#include <fstream>

#include "fdr/reader.hpp"
#include "fdr/recovery.hpp"
#include "fdr/segment.hpp"
#include "fdr/writer.hpp"
#include "testing.hpp"

namespace fs = std::filesystem;

namespace {

void copy_dir(const fs::path& from, const fs::path& to) {
  fs::remove_all(to);
  fs::copy(from, to, fs::copy_options::recursive);
}

std::vector<fdr::Record> read_back(const fs::path& dir) {
  std::vector<fdr::Record> out;
  fdr::Reader(dir).query(0, UINT64_MAX, [&](const fdr::Record& r) {
    out.push_back(r);
    return true;
  });
  return out;
}

fs::path last_segment(const fs::path& dir) { return fdr::list_segments(dir).back().path; }

}  // namespace

TEST(crash_snapshot_with_preallocated_tail_loses_nothing) {
  fdr_test::TempDir dir;
  fdr_test::TempDir crashed;
  const auto records = fdr_test::make_records(60, 21);
  fdr::WriterConfig config;
  config.dir = dir.path();
  config.segment_bytes = 16384;
  fdr::Writer writer(config);
  for (const auto& r : records) {
    writer.append(r);
  }
  copy_dir(dir.path(), crashed.path());  // the writer never got to close()

  const auto report = fdr::recover(crashed.path(), true);
  CHECK_EQ(report.records, records.size());
  CHECK(report.truncated_bytes > 0);
  CHECK(!report.damaged());
  CHECK(read_back(crashed.path()) == records);
  for (const auto& f : fdr::list_segments(crashed.path())) {
    CHECK(fs::file_size(f.path) < 16384);
  }
}

TEST(torn_write_is_truncated_to_last_complete_record) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(40, 22);
  fdr_test::write_all(dir.path(), records);
  const fs::path last = last_segment(dir.path());
  fs::resize_file(last, fs::file_size(last) - 5);

  const auto dry = fdr::recover(dir.path(), false);
  CHECK(dry.damaged());
  CHECK_EQ(dry.torn_segments, std::size_t{1});
  const std::uint64_t size_before = fs::file_size(last);
  CHECK_EQ(fs::file_size(last), size_before);

  const auto report = fdr::recover(dir.path(), true);
  CHECK_EQ(report.torn_segments, std::size_t{1});
  CHECK(report.truncated_bytes > 0);
  const auto got = read_back(dir.path());
  CHECK_EQ(got.size(), records.size() - 1);
  CHECK(std::equal(got.begin(), got.end(), records.begin()));
  CHECK_EQ(fs::file_size(last), report.segments.back().valid_end);

  const auto again = fdr::recover(dir.path(), true);
  CHECK_EQ(again.truncated_bytes, std::uint64_t{0});
  CHECK(!again.damaged());
}

TEST(dry_run_never_modifies_files) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fdr_test::make_records(30, 23));
  const fs::path last = last_segment(dir.path());
  fs::resize_file(last, fs::file_size(last) - 3);
  const auto size = fs::file_size(last);
  (void)fdr::recover(dir.path(), false);
  CHECK_EQ(fs::file_size(last), size);
}

TEST(mid_file_corruption_loses_only_the_damaged_record) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(50, 24, 40);
  fdr_test::write_all(dir.path(), records, 1U << 20);
  const fs::path seg = last_segment(dir.path());
  {
    const fdr::Reader reader(dir.path(), 1);
    const auto target = reader.index()[20].offset;
    std::fstream f(seg, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(target + fdr::kFrameHeaderSize));
    f.put('\x5A');
    f.put('\xA5');
  }
  const auto report = fdr::recover(dir.path(), true);
  CHECK(report.damaged());
  CHECK_EQ(report.corrupt_regions, std::size_t{1});
  CHECK_EQ(report.records, records.size() - 1);
  auto expected = records;
  expected.erase(expected.begin() + 20);
  CHECK(read_back(dir.path()) == expected);
}

TEST(half_created_final_segment_is_removed) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(20, 25);
  fdr_test::write_all(dir.path(), records);
  const auto count = fdr::list_segments(dir.path()).size();
  const fs::path ghost = dir.path() / fdr::segment_file_name(static_cast<std::uint32_t>(count + 1));
  std::ofstream(ghost, std::ios::binary) << "garbage";
  const auto report = fdr::recover(dir.path(), true);
  CHECK_EQ(report.removed_segments, std::size_t{1});
  CHECK(!fs::exists(ghost));
  CHECK(read_back(dir.path()) == records);
}

TEST(zero_filled_final_segment_is_removed) {
  fdr_test::TempDir dir;
  fdr_test::write_all(dir.path(), fdr_test::make_records(20, 26));
  const auto count = fdr::list_segments(dir.path()).size();
  const fs::path ghost = dir.path() / fdr::segment_file_name(static_cast<std::uint32_t>(count + 1));
  std::ofstream(ghost, std::ios::binary) << std::string(4096, '\0');
  CHECK_EQ(fdr::recover(dir.path(), true).removed_segments, std::size_t{1});
  CHECK(!fs::exists(ghost));
}

TEST(writer_resumes_after_torn_write) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(80, 27);
  fdr_test::write_all(dir.path(), records);
  const fs::path last = last_segment(dir.path());
  fs::resize_file(last, fs::file_size(last) - 7);

  fdr::WriterConfig config;
  config.dir = dir.path();
  config.segment_bytes = 4096;
  fdr::Writer writer(config);
  CHECK_EQ(writer.recovery().torn_segments, std::size_t{1});
  fdr::Record extra;
  extra.timestamp_us = records.back().timestamp_us + 1;
  extra.payload = {1, 2, 3};
  writer.append(extra);
  writer.close();

  auto expected = records;
  expected.pop_back();
  expected.push_back(extra);
  CHECK(read_back(dir.path()) == expected);
}

TEST(truncated_first_segment_header_keeps_later_segments) {
  fdr_test::TempDir dir;
  const auto records = fdr_test::make_records(300, 28);
  fdr_test::write_all(dir.path(), records);
  const auto files = fdr::list_segments(dir.path());
  CHECK(files.size() > 2);
  fs::resize_file(files[0].path, 10);
  const auto report = fdr::recover(dir.path(), true);
  CHECK(report.records < records.size());
  CHECK(report.records > 0);
  CHECK_EQ(read_back(dir.path()).size(), report.records);
}

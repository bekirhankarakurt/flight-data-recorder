// Randomised fault injection: damage a finished recording at random offsets,
// recover, and check that exactly the records whose bytes were untouched
// survive, in order, and nothing is ever fabricated.

#include <fstream>
#include <map>
#include <random>

#include "fdr/reader.hpp"
#include "fdr/recovery.hpp"
#include "fdr/scan.hpp"
#include "fdr/segment.hpp"
#include "fdr/writer.hpp"
#include "testing.hpp"

namespace fs = std::filesystem;

namespace {

struct Frame {
  std::size_t segment;       // index into the sorted segment list
  std::uint64_t begin;       // byte range [begin, end) within the segment file
  std::uint64_t end;
  fdr::Record record;
};

std::vector<Frame> frames_of(const fs::path& dir) {
  std::vector<Frame> frames;
  const auto files = fdr::list_segments(dir);
  for (std::size_t s = 0; s < files.size(); ++s) {
    const auto data = fdr::read_file(files[s].path);
    fdr::scan_frames(data.data(), data.size(), fdr::kSegmentHeaderSize,
                     [&](const fdr::Record& r, std::uint64_t offset) {
                       std::vector<std::uint8_t> enc;
                       fdr::encode_frame(r, enc);
                       frames.push_back({s, offset, offset + enc.size(), r});
                       return true;
                     });
  }
  return frames;
}

void overwrite(const fs::path& file, std::uint64_t offset,
               const std::vector<std::uint8_t>& bytes) {
  std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
  f.seekp(static_cast<std::streamoff>(offset));
  f.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
}

std::vector<fdr::Record> read_back(const fs::path& dir) {
  std::vector<fdr::Record> out;
  fdr::Reader(dir).query(0, UINT64_MAX, [&](const fdr::Record& r) {
    out.push_back(r);
    return true;
  });
  return out;
}

}  // namespace

TEST(random_faults_never_fabricate_and_never_lose_intact_records) {
  fdr_test::TempDir base;
  const auto records = fdr_test::make_records(300, 31, 120);
  fdr_test::write_all(base.path(), records, 4096);
  const auto frames = frames_of(base.path());
  CHECK_EQ(frames.size(), records.size());
  const auto files = fdr::list_segments(base.path());

  std::mt19937_64 rng(0xF11D0001ULL);
  auto pick = [&rng](std::uint64_t n) { return n == 0 ? 0 : rng() % n; };

  for (int iteration = 0; iteration < 400; ++iteration) {
    fdr_test::TempDir work;
    fs::copy(base.path(), work.path(), fs::copy_options::recursive);
    const auto work_files = fdr::list_segments(work.path());

    const std::uint64_t kind = pick(4);
    if (kind == 0 || kind == 1) {
      // Truncate a segment (kind 0: any segment, kind 1: the last, like a power cut).
      const std::size_t s = kind == 0 ? pick(files.size()) : files.size() - 1;
      const std::uint64_t size = fs::file_size(work_files[s].path);
      const std::uint64_t cut = pick(size);
      fs::resize_file(work_files[s].path, cut);
    } else if (kind == 2) {
      // Overwrite a few random bytes.
      for (std::uint64_t k = 1 + pick(8); k > 0; --k) {
        const std::size_t s = pick(files.size());
        const std::uint64_t at = pick(fs::file_size(work_files[s].path));
        overwrite(work_files[s].path, at,
                  {static_cast<std::uint8_t>(1 + pick(255))});
      }
    } else {
      // Overwrite a random run with garbage or zeros (a torn sector).
      const std::size_t s = pick(files.size());
      const std::uint64_t size = fs::file_size(work_files[s].path);
      const std::uint64_t at = pick(size);
      const std::uint64_t len = std::min<std::uint64_t>(1 + pick(512), size - at);
      std::vector<std::uint8_t> junk(static_cast<std::size_t>(len));
      const bool zeros = pick(2) == 0;
      for (auto& b : junk) {
        b = zeros ? std::uint8_t{0} : static_cast<std::uint8_t>(rng());
      }
      overwrite(work_files[s].path, at, junk);
    }

    // Derive the survivors from the actual bytes on disk: a frame survives iff
    // it is byte-identical to the original.
    std::vector<fdr::Record> expected;
    std::map<std::size_t, std::vector<std::uint8_t>> damaged_files;
    for (const auto& f : frames) {
      auto it = damaged_files.find(f.segment);
      if (it == damaged_files.end()) {
        it = damaged_files.emplace(f.segment, fdr::read_file(work_files[f.segment].path)).first;
      }
      const auto original = fdr::read_file(files[f.segment].path);
      const bool intact =
          f.end <= it->second.size() &&
          std::equal(original.begin() + static_cast<std::ptrdiff_t>(f.begin),
                     original.begin() + static_cast<std::ptrdiff_t>(f.end),
                     it->second.begin() + static_cast<std::ptrdiff_t>(f.begin));
      if (intact) {
        expected.push_back(f.record);
      }
    }

    const auto report = fdr::recover(work.path(), true);
    const auto got = read_back(work.path());
    CHECK(got == expected);
    CHECK_EQ(report.records, expected.size());

    const auto again = fdr::recover(work.path(), true);
    CHECK_EQ(again.truncated_bytes, std::uint64_t{0});
    CHECK_EQ(again.records, expected.size());

    // The recorder must be able to continue after any of these faults.
    fdr::WriterConfig config;
    config.dir = work.path();
    config.segment_bytes = 4096;
    fdr::Writer writer(config);
    fdr::Record next;
    next.timestamp_us = (expected.empty() ? 0 : expected.back().timestamp_us) + 1;
    next.channel = 9;
    next.payload = {0xAB};
    writer.append(next);
    writer.close();
    expected.push_back(next);
    CHECK(read_back(work.path()) == expected);
  }
}

#include <cstring>
#include <fstream>

#include "fdr/crc32.hpp"
#include "fdr/endian.hpp"
#include "fdr/record.hpp"
#include "fdr/segment.hpp"
#include "testing.hpp"

namespace {

fdr::Record make_record(std::uint64_t ts, std::uint16_t channel, std::size_t n) {
  fdr::Record r;
  r.timestamp_us = ts;
  r.channel = channel;
  for (std::size_t i = 0; i < n; ++i) {
    r.payload.push_back(static_cast<std::uint8_t>(i * 7 + 3));
  }
  return r;
}

std::vector<std::uint8_t> encode(const fdr::Record& r) {
  std::vector<std::uint8_t> out;
  fdr::encode_frame(r, out);
  return out;
}

}  // namespace

TEST(crc32_matches_reference_vector) {
  const char* text = "123456789";
  const auto* p = reinterpret_cast<const std::uint8_t*>(text);
  CHECK_EQ(fdr::crc32(p, 9), 0xCBF43926U);
  CHECK_EQ(fdr::crc32(p, 0), 0U);
}

TEST(crc32_is_incremental) {
  const char* text = "123456789";
  const auto* p = reinterpret_cast<const std::uint8_t*>(text);
  const std::uint32_t part = fdr::crc32(p, 4);
  CHECK_EQ(fdr::crc32(p + 4, 5, part), fdr::crc32(p, 9));
}

TEST(endian_round_trip) {
  std::uint8_t buf[8] = {};
  fdr::put_le<std::uint32_t>(buf, 0x01020304U);
  CHECK_EQ(buf[0], std::uint8_t{4});
  CHECK_EQ(buf[3], std::uint8_t{1});
  CHECK_EQ(fdr::get_le<std::uint32_t>(buf), 0x01020304U);
  fdr::put_le<std::uint64_t>(buf, 0x1122334455667788ULL);
  CHECK_EQ(fdr::get_le<std::uint64_t>(buf), 0x1122334455667788ULL);
  fdr::put_le<std::uint16_t>(buf, std::uint16_t{0xBEEF});
  CHECK_EQ(fdr::get_le<std::uint16_t>(buf), std::uint16_t{0xBEEF});
}

TEST(frame_round_trip) {
  for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{255},
                              fdr::kMaxPayloadSize}) {
    const fdr::Record in = make_record(1234567890123ULL, 42, n);
    const auto bytes = encode(in);
    CHECK_EQ(bytes.size(), fdr::kFrameOverhead + n);
    fdr::Record out;
    const auto result = fdr::parse_frame(bytes.data(), bytes.size(), &out);
    CHECK(result.status == fdr::FrameStatus::kOk);
    CHECK_EQ(result.size, bytes.size());
    CHECK(out == in);
  }
}

TEST(frame_rejects_oversized_payload) {
  fdr::Record r;
  r.payload.resize(fdr::kMaxPayloadSize + 1);
  std::vector<std::uint8_t> out;
  bool threw = false;
  try {
    fdr::encode_frame(r, out);
  } catch (const std::length_error&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(out.empty());
}

TEST(frame_detects_every_truncation) {
  const auto bytes = encode(make_record(99, 1, 20));
  for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
    const auto result = fdr::parse_frame(bytes.data(), cut, nullptr);
    CHECK(result.status != fdr::FrameStatus::kOk);
  }
}

TEST(frame_detects_every_single_bit_flip) {
  const auto original = encode(make_record(99, 1, 20));
  for (std::size_t i = 0; i < original.size(); ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      auto bytes = original;
      bytes[i] = static_cast<std::uint8_t>(bytes[i] ^ (1U << bit));
      CHECK(fdr::parse_frame(bytes.data(), bytes.size(), nullptr).status !=
            fdr::FrameStatus::kOk);
    }
  }
}

TEST(frame_status_codes) {
  auto bytes = encode(make_record(1, 1, 8));
  CHECK(fdr::parse_frame(bytes.data(), 2, nullptr).status == fdr::FrameStatus::kTruncated);
  CHECK(fdr::parse_frame(bytes.data(), bytes.size() - 1, nullptr).status ==
        fdr::FrameStatus::kTruncated);
  auto bad_sync = bytes;
  bad_sync[0] = 0;
  CHECK(fdr::parse_frame(bad_sync.data(), bad_sync.size(), nullptr).status ==
        fdr::FrameStatus::kBadSync);
  std::uint8_t bad_len[fdr::kFrameOverhead + 8];
  CHECK_EQ(bytes.size(), sizeof(bad_len));
  std::memcpy(bad_len, bytes.data(), sizeof(bad_len));
  fdr::put_le<std::uint32_t>(bad_len + 4, 0xFFFFFFFFU);
  CHECK(fdr::parse_frame(bad_len, sizeof(bad_len), nullptr).status ==
        fdr::FrameStatus::kBadLength);
  bytes.back() = static_cast<std::uint8_t>(bytes.back() ^ 0xFFU);
  CHECK(fdr::parse_frame(bytes.data(), bytes.size(), nullptr).status ==
        fdr::FrameStatus::kBadCrc);
}

TEST(segment_header_round_trip_and_corruption) {
  std::uint8_t raw[fdr::kSegmentHeaderSize];
  fdr::encode_segment_header({7, 123456789ULL}, raw);
  fdr::SegmentHeader h;
  CHECK(fdr::decode_segment_header(raw, sizeof(raw), h));
  CHECK_EQ(h.sequence, 7U);
  CHECK_EQ(h.base_timestamp_us, 123456789ULL);
  CHECK(!fdr::decode_segment_header(raw, sizeof(raw) - 1, h));
  for (std::size_t i = 0; i < 28; ++i) {
    std::uint8_t copy[fdr::kSegmentHeaderSize];
    std::memcpy(copy, raw, sizeof(copy));
    copy[i] = static_cast<std::uint8_t>(copy[i] ^ 0x10U);
    CHECK(!fdr::decode_segment_header(copy, sizeof(copy), h));
  }
}

TEST(segment_names_and_listing) {
  CHECK_EQ(fdr::segment_file_name(42), std::string("fdr-000042.seg"));
  fdr_test::TempDir dir;
  for (const char* name : {"fdr-000010.seg", "fdr-000002.seg", "fdr-abc.seg", "fdr-.seg",
                           "notes.txt", "fdr-000003.seg.bak"}) {
    std::ofstream(dir.path() / name) << "x";
  }
  const auto files = fdr::list_segments(dir.path());
  CHECK_EQ(files.size(), std::size_t{2});
  CHECK_EQ(files[0].sequence, 2U);
  CHECK_EQ(files[1].sequence, 10U);
  CHECK(fdr::list_segments(dir.path() / "missing").empty());
}

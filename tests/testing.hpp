#pragma once

// Minimal dependency-free test harness: TEST(name), CHECK(cond), CHECK_EQ(a, b).

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fdr_test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failure_count() {
  static int failures = 0;
  return failures;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <typename T, typename = void>
struct Streamable : std::false_type {};
template <typename T>
struct Streamable<T, std::void_t<decltype(std::declval<std::ostream&>()
                                          << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string show(const T& value) {
  if constexpr (Streamable<T>::value) {
    std::ostringstream os;
    if constexpr (std::is_same<T, std::uint8_t>::value) {
      os << static_cast<unsigned>(value);
    } else {
      os << value;
    }
    return os.str();
  } else {
    return "<unprintable>";
  }
}

inline void report_failure(const char* file, int line, const std::string& what) {
  ++failure_count();
  std::cerr << file << ":" << line << ": check failed: " << what << "\n";
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file,
              int line) {
  if (!(a == b)) {
    report_failure(file, line,
                   std::string(ea) + " == " + eb + " (" + show(a) + " vs " + show(b) + ")");
  }
}

// Temporary directory removed on destruction.
class TempDir {
 public:
  TempDir() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("fdr-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace fdr_test

#define TEST(name)                                          \
  static void name();                                       \
  static ::fdr_test::Registrar registrar_##name(#name, name); \
  static void name()

#define CHECK(expr)                                                    \
  do {                                                                 \
    if (!(expr)) ::fdr_test::report_failure(__FILE__, __LINE__, #expr); \
  } while (false)

#define CHECK_EQ(a, b) ::fdr_test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)

// ---- recording helpers shared by the storage tests ----

#include "fdr/record.hpp"
#include "fdr/writer.hpp"

namespace fdr_test {

// Deterministic records with strictly increasing timestamps and payloads of
// 0..max_payload bytes.
inline std::vector<fdr::Record> make_records(std::size_t count, std::uint64_t seed,
                                             std::size_t max_payload = 120,
                                             std::uint64_t start_ts = 1000) {
  std::uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
  auto next = [&state]() {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  };
  std::vector<fdr::Record> records;
  std::uint64_t ts = start_ts;
  for (std::size_t i = 0; i < count; ++i) {
    fdr::Record r;
    ts += 1 + next() % 1000;
    r.timestamp_us = ts;
    r.channel = static_cast<std::uint16_t>(next() % 4);
    r.payload.resize(static_cast<std::size_t>(next() % (max_payload + 1)));
    for (auto& b : r.payload) {
      b = static_cast<std::uint8_t>(next() & 0xFFU);
    }
    records.push_back(std::move(r));
  }
  return records;
}

inline void write_all(const std::filesystem::path& dir, const std::vector<fdr::Record>& records,
                      std::uint64_t segment_bytes = 4096,
                      fdr::SyncPolicy sync = fdr::SyncPolicy::kNone) {
  fdr::WriterConfig config;
  config.dir = dir;
  config.segment_bytes = segment_bytes;
  config.sync = sync;
  fdr::Writer writer(config);
  for (const auto& r : records) {
    writer.append(r);
  }
  writer.close();
}

}  // namespace fdr_test

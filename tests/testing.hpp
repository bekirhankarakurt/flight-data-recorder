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

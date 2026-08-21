#include <exception>
#include <iostream>

#include "testing.hpp"

int main() {
  int failed_cases = 0;
  for (const auto& test : fdr_test::registry()) {
    const int before = fdr_test::failure_count();
    try {
      test.fn();
    } catch (const std::exception& e) {
      fdr_test::report_failure(test.name, 0, std::string("unexpected exception: ") + e.what());
    }
    const bool ok = fdr_test::failure_count() == before;
    std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << test.name << "\n";
    failed_cases += ok ? 0 : 1;
  }
  std::cout << fdr_test::registry().size() - static_cast<std::size_t>(failed_cases) << "/"
            << fdr_test::registry().size() << " tests passed\n";
  return failed_cases == 0 ? 0 : 1;
}

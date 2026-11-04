# Contributing

## Build and test

```bash
cmake -S . -B build -DFDR_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request, also run the sanitizer build and static analysis, which CI enforces:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DFDR_SANITIZE=ON -DFDR_WARNINGS_AS_ERRORS=ON
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFDR_BUILD_TESTS=OFF
clang-tidy -p build-tidy src/*.cpp apps/fdr.cpp
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 --inline-suppr -I include -q src apps
```

## Style

- Modern C++17, RAII, no raw `new`/`delete`, const-correct, `[[nodiscard]]` on results that must not be ignored, `enum class`.
- Zero warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; zero `clang-tidy` and `cppcheck` findings.
- No TODOs, stubs or dead code.
- Hardware-dependent code needs a simulation or replay path so it can be tested without hardware.
- Every behaviour change comes with a test. Storage-format changes also need a fault-injection case.

## Commits and pull requests

- Small, logical commits with plain imperative subjects ("Add X", "Fix Y"), 72 characters or fewer.
- Describe what changed and why in the body when it is not obvious.
- Update `CHANGELOG.md` under `[Unreleased]` and the README when behaviour or options change.

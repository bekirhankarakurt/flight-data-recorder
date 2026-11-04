# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0] - 2026-11-04

### Added
- Append-only segmented storage with CRC-32 framed records and sync markers.
- Crash recovery that truncates torn writes and trims pre-allocation padding, with resynchronisation after mid-file corruption.
- Configurable fsync policy (`none`, `every`, `batch`, `interval`) and segment pre-allocation.
- Sparse in-memory time index for time-range queries.
- `fdr` command line tool: `record`, `recover`, `info`, `export` (CSV) and `replay` (stdout or UDP, original or scaled speed).
- Deterministic telemetry simulator and UDP source so everything runs without hardware.
- Unit tests, randomised fault-injection tests and a CLI smoke test, run through CTest.
- Continuous integration (GCC, Clang, ASan/UBSan, cppcheck, clang-tidy, CodeQL), coverage and API documentation published to GitHub Pages.

[Unreleased]: https://github.com/bekirhankarakurt/flight-data-recorder/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bekirhankarakurt/flight-data-recorder/releases/tag/v1.0.0

# flight-data-recorder

Power-loss tolerant black-box recorder in C++17: CRC-framed, append-only segmented storage that recovers cleanly after a crash, with time-range queries, paced replay and CSV export.

[![CI](https://github.com/bekirhankarakurt/flight-data-recorder/actions/workflows/ci.yml/badge.svg)](https://github.com/bekirhankarakurt/flight-data-recorder/actions/workflows/ci.yml)
[![CodeQL](https://github.com/bekirhankarakurt/flight-data-recorder/actions/workflows/codeql.yml/badge.svg)](https://github.com/bekirhankarakurt/flight-data-recorder/actions/workflows/codeql.yml)
[![Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fbekirhankarakurt.github.io%2Fflight-data-recorder%2Fcoverage.json)](https://bekirhankarakurt.github.io/flight-data-recorder/coverage/)
[![Release](https://img.shields.io/github/v/release/bekirhankarakurt/flight-data-recorder)](https://github.com/bekirhankarakurt/flight-data-recorder/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

![fdr demo](docs/demo.gif)

**Documentation:** [API reference](https://bekirhankarakurt.github.io/flight-data-recorder/) · [coverage report](https://bekirhankarakurt.github.io/flight-data-recorder/coverage/) · [changelog](CHANGELOG.md) · [contributing](CONTRIBUTING.md) · [security policy](SECURITY.md)

## Why / what

- **Survives power loss.** Every record is a self-describing frame (sync marker + length + CRC-32). A torn write is detected and cut off; nothing before it is lost.
- **Survives bit rot, too.** Damage in the middle of a file costs only the damaged records: readers resynchronise on the next valid sync marker.
- **Tunable durability.** Choose `fdatasync` per record, per batch, per interval, or never, and pre-allocate segments so a full disk fails at start-up, not mid-flight.
- **Query and replay.** A sparse in-memory time index gives fast range queries; replay re-emits data to stdout or UDP at original, scaled or unthrottled speed.
- **Runs without hardware.** A deterministic telemetry simulator stands in for the sensor feed, so everything builds, runs and is tested on any Linux box. Real feeds arrive over UDP.

## Demo

Real output from `fdr` (3000 simulated records at 200 Hz, two 64 KiB segments; then a torn write, recovery, and resuming the recording):

```console
$ fdr record demo --start-us 1700000000000000 --rate 200 --count 3000 --segment-kib 64
recorded 3000 records to demo (2 new segments, 116000 bytes, 48 syncs)

$ fdr info demo
directory:  demo
segments:   2
records:    3000
time range: 1700000000000000 .. 1700000014995000 us (14.995 s)
corruption: 0 regions, 0 bytes skipped
channel 0: 1000 records
channel 1: 1000 records
channel 2: 1000 records

 segment   records       bytes          first_us           last_us
       1      1693       65504  1700000000000000  1700000008460000
       2      1307       50560  1700000008465000  1700000014995000

$ fdr export demo --from +1s --to +1.01s
timestamp_us,channel,length,payload_hex
1700000001000000,2,8,46e2c54703097141
1700000001005000,0,24,6519623b97900fbdceaa1c41444c89bb4750fc3bf7cc123c
1700000001010000,1,12,e644c817dc5f6e13b84c0f00

$ truncate -s -9 demo/fdr-000002.seg        # simulate a write torn by power loss
$ fdr recover demo --dry-run; echo "exit=$?"
dry run (no files changed)
segments:         2
records:          2999
torn segments:    1
corrupt regions:  0 (0 bytes skipped)
removed segments: 0
bytes to trim:    23
exit=2

$ fdr recover demo
segments:         2
records:          2999
torn segments:    1
corrupt regions:  0 (0 bytes skipped)
removed segments: 0
bytes trimmed:    23

$ fdr record demo --count 10
recorded 10 records to demo (0 new segments, 396 bytes, 1 syncs)
```

## Quick start (60 seconds)

```bash
git clone https://github.com/bekirhankarakurt/flight-data-recorder.git
cd flight-data-recorder
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure      # optional: run the tests
```

```bash
build/fdr record flight1 --count 3000 --rate 200   # simulated telemetry
build/fdr info flight1
build/fdr export flight1 --from +1s --to +2s -o slice.csv
build/fdr replay flight1 --speed 4 | head
```

## Requirements and build options

| Requirement | Version |
|---|---|
| Linux (POSIX file and socket APIs) | any recent distribution |
| C++17 compiler | GCC >= 9 or Clang >= 9 |
| CMake | >= 3.16 |
| Third-party libraries | none |

| CMake option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | Standard build type |
| `FDR_BUILD_TESTS` | `ON` | Build the unit tests and CLI smoke test |
| `FDR_WARNINGS_AS_ERRORS` | `OFF` | Add `-Werror` (CI turns it on) |
| `FDR_SANITIZE` | `OFF` | Build with AddressSanitizer + UBSan |

Prebuilt `linux-x86_64` and `linux-aarch64` binaries are attached to each [release](https://github.com/bekirhankarakurt/flight-data-recorder/releases).

All targets compile warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

## Usage

`fdr <command> [options] DIR` — `DIR` is the recording directory.

| Command | Options | What it does |
|---|---|---|
| `record DIR` | `--source sim\|udp`, `--count N`, `--rate HZ`, `--seed N`, `--start-us N`, `--realtime`, `--port N`, `--channel N`, `--idle-ms N`, `--segment-kib N`, `--sync none\|every\|batch\|interval`, `--batch N`, `--interval-ms N`, `--no-prealloc` | Records from the simulator or a UDP feed. Runs crash recovery first, then appends. Ctrl-C finishes cleanly. |
| `recover DIR` | `--dry-run` | Cuts torn writes and padding; `--dry-run` only reports and exits `2` if damage is found. |
| `info DIR` | | Totals, time range, per-channel and per-segment summary, corruption count. |
| `export DIR` | `--from T`, `--to T`, `--channel N`, `-o FILE` | CSV: `timestamp_us,channel,length,payload_hex`. |
| `replay DIR` | `--from T`, `--to T`, `--channel N`, `--speed X\|max`, `--udp HOST:PORT` | Re-emits records with their original spacing divided by `X`; CSV to stdout, or raw payloads as UDP datagrams. |

`T` is an absolute timestamp in microseconds (`1700000001000000`) or an offset from the first record: `+2.5s`, `+300ms`, `+40us`.
Exit codes: `0` ok, `1` runtime error, `2` damage found (`recover --dry-run`), `64` usage error.

### Recipes

**Record a UDP telemetry feed** (stops after 2 s of silence; each datagram becomes one record on channel 3):

```bash
build/fdr record flight2 --source udp --port 47000 --channel 3 --idle-ms 2000
```

**Replay a window into a ground-station tool listening on UDP, at half speed:**

```bash
build/fdr replay flight1 --from +2s --to +5s --speed 0.5 --udp 127.0.0.1:47000
```

**Check a recording after a crash, then repair and keep recording:**

```bash
build/fdr recover flight1 --dry-run || build/fdr recover flight1
build/fdr record flight1 --count 100
```

**Maximum durability for a flight-critical channel** (sync every record) versus a bulk logger (sync once a second):

```bash
build/fdr record critical --sync every
build/fdr record bulk --sync interval --interval-ms 1000
```

**Pull one channel into a spreadsheet:**

```bash
build/fdr export flight1 --channel 1 -o gps.csv
```

## Architecture

```
  Source                      Writer                         Disk
 ┌────────────┐  Record  ┌───────────────────┐  pwrite  ┌────────────────────────┐
 │ SimSource  │─────────▶│ encode_frame      │─────────▶│ fdr-000001.seg (closed)│
 │ UdpSource  │          │ rotate / prealloc │ fdatasync│ fdr-000002.seg (closed)│
 └────────────┘          │ sync policy       │          │ fdr-000003.seg (open,  │
                         └───────────────────┘          │   pre-allocated)       │
                                  ▲                     └───────────┬────────────┘
                     recover() on open                              │ read
                                  │                                 ▼
 ┌────────────┐  emit   ┌──────────────────┐  query   ┌─────────────────────────┐
 │ CsvSink    │◀────────│ replay + Pacer   │◀─────────│ Reader: scan_frames +   │
 │ UdpSink    │         └──────────────────┘          │ sparse time index (RAM) │
 └────────────┘                                       └─────────────────────────┘
```

Design trade-offs:

| Decision | Why | Cost |
|---|---|---|
| Append-only segments, never rewritten | A crash can only damage the tail; old data is never at risk | Corruption in the middle is skipped, not repaired |
| Sync marker + CRC on every frame | Resynchronise after any damage; no false positives in practice (CRC-32 plus length check) | 24 bytes of overhead per record |
| Trim torn tail only on `recover`/open | Mid-file damage is kept and skipped so later records remain readable | Reports keep counting that corruption |
| Pre-allocated segments (`posix_fallocate`) | Disk-full surfaces at segment start; fewer metadata updates per append | A crash leaves zero padding (trimmed on recovery); disable with `--no-prealloc` |
| Index rebuilt in RAM, never stored | Cannot go stale or be corrupted; one scan on open | Open time grows with recording size |
| Reads use `pread`, not `mmap` | A live recorder may shrink a file; `mmap` would raise `SIGBUS` | An extra copy per segment |
| Non-decreasing timestamps enforced | Keeps the index valid and replay ordered | Writer rejects clock steps backwards (`timestamp went backwards`) |
| `Pacer` interface for replay | Timing logic is unit-tested with a fake clock | One virtual call per record |

## Data format

All integers are little-endian. Segment files are named `fdr-NNNNNN.seg`.

**Segment header (32 bytes)**

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | Magic `FDRSEG01` |
| 8 | 4 | Format version (1) |
| 12 | 4 | Segment sequence number |
| 16 | 8 | Timestamp of first record (µs) |
| 24 | 4 | CRC-32 of bytes 0–23 |
| 28 | 4 | Reserved (0) |

**Record frame (24 + n bytes, followed immediately by the next frame)**

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Sync marker `0xA55AF00D` |
| 4 | 4 | Payload length *n* (≤ 65536) |
| 8 | 8 | Timestamp (µs) |
| 16 | 2 | Channel |
| 18 | 2 | Reserved (0) |
| 20 | n | Payload |
| 20+n | 4 | CRC-32 of bytes 4 … 19+n |

**Simulated channels** (`--source sim`; cycles 0, 1, 2)

| Channel | Payload |
|---|---|
| 0 IMU | 6 × float32: ax ay az (m/s²), gx gy gz (rad/s) |
| 1 GPS | 3 × int32: latitude, longitude (1e-7°), altitude (mm) |
| 2 Baro | 2 × float32: pressure (Pa), temperature (°C) |

## Testing

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Covers |
|---|---|
| `test_core` | CRC-32 reference vectors, every single-bit flip and truncation of a frame, segment headers |
| `test_storage` | Round trips across rotation, resume, sync policies, pre-allocation, time-range queries vs. brute force |
| `test_recovery` | Crash snapshots, torn writes, mid-file corruption, half-created segments, resume after repair |
| `test_fault_injection` | 400 seeded random faults (truncate, overwrite, zero a sector) at random offsets; asserts that exactly the untouched records survive, nothing is fabricated, and recording can continue |
| `test_replay` | Pacing with a fake clock, filters, CSV format, UDP over loopback, simulator determinism |
| `cli_smoke` | End-to-end run of every command, including exit codes |

CI runs GCC and Clang (Release, `-Werror`), a GCC ASan/UBSan job, cppcheck, clang-tidy and CodeQL; line coverage is published with the docs. For the sanitizer build locally:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DFDR_SANITIZE=ON && cmake --build build-asan -j && ctest --test-dir build-asan
```

## Troubleshooting / FAQ

**`timestamp went backwards` when resuming a recording.** Timestamps must never decrease within a recording. With `--source sim`, leave out `--start-us`: it defaults to the current time, or just past the newest stored record if that is later. An explicit `--start-us` (or a UDP host whose clock stepped back) that precedes stored data is rejected.

**Does `fdatasync` guarantee durability?** It is only as honest as the storage stack: disks and SSDs with volatile write caches can still lose data. Use hardware with power-loss protection for the real thing.

**What does a crash cost me?** With `--sync every`, nothing that `append` returned from. With `batch` or `interval`, up to the unsynced records. The tail is trimmed automatically the next time `record` opens the directory, or by `fdr recover`.

**`recover` reports corrupt regions but I did not touch the files.** That is real media or memory damage. The affected records are skipped, everything else stays readable. Corrupt regions are not rewritten, so the count persists.

**Why is the open segment as large as `--segment-kib`?** It is pre-allocated. Closing the recorder trims it; use `--no-prealloc` to grow files on demand.

**Can I read a recording while it is being written?** Yes. The reader sees the records already written; the zero padding is ignored.

## Project layout

```
.
├── CMakeLists.txt           build definition and options
├── include/fdr/
│   ├── crc32.hpp            CRC-32 (IEEE)
│   ├── endian.hpp           little-endian helpers
│   ├── record.hpp           Record, frame encode / parse
│   ├── segment.hpp          segment header, naming, listing
│   ├── fd.hpp               RAII file descriptor, pread/pwrite helpers
│   ├── scan.hpp             frame scanner with resynchronisation
│   ├── recovery.hpp         crash recovery / repair
│   ├── writer.hpp           append-only writer, sync policies
│   ├── reader.hpp           reader with sparse time index
│   ├── source.hpp           SimSource, UdpSource
│   ├── sink.hpp             CsvSink, UdpSink
│   ├── replay.hpp           paced replay
│   ├── net.hpp              UDP socket wrappers
│   └── timearg.hpp          time and integer argument parsing
├── src/                     implementations of the headers above
├── apps/fdr.cpp             command line tool
├── docs/                    demo.tape (vhs script) and demo.gif
├── tests/                   dependency-free test harness and suites
├── .clang-tidy, Doxyfile    static analysis and API docs configuration
├── CHANGELOG.md, CONTRIBUTING.md, SECURITY.md
└── .github/                 workflows (CI, CodeQL, Pages, Release), issue and PR templates, Dependabot
```

## Roadmap

- [x] Segmented append-only storage with CRC-32 framing
- [x] Crash recovery with torn-write truncation and resynchronisation
- [x] Configurable fsync policy and pre-allocation
- [x] Time-range index, CSV export, paced replay (stdout / UDP)
- [x] Randomised fault-injection tests
- [ ] Persistent index sidecar for instant open on very large recordings
- [ ] Segment retention (ring-buffer mode with a size or age cap)
- [ ] Per-channel decoders for CSV export (typed columns)
- [ ] Serial / CAN-bus sources

## License

MIT — see [LICENSE](LICENSE).

Bekirhan Karakurt - github.com/bekirhankarakurt

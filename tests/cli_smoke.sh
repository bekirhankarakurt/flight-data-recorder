#!/usr/bin/env bash
# End-to-end check of the fdr command line: record, inspect, export, replay,
# tear a write, recover. Usage: cli_smoke.sh /path/to/fdr
set -euo pipefail

fdr=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
rec=$work/rec

fail() { echo "FAIL: $*" >&2; exit 1; }

"$fdr" record "$rec" --start-us 1700000000000000 --rate 200 --count 3000 --segment-kib 64 \
  | grep -q '^recorded 3000 records' || fail "record"

"$fdr" info "$rec" | grep -q '^records:    3000$' || fail "info record count"
"$fdr" info "$rec" | grep -q '^segments:   2$' || fail "info segment count"

rows=$("$fdr" export "$rec" --from +1s --to +1.02s | wc -l)
[ "$rows" -eq 6 ] || fail "export window expected 6 lines (header + 5), got $rows"

"$fdr" export "$rec" --channel 1 -o "$work/ch1.csv" | grep -q 'exported 1000 records' \
  || fail "export channel filter"

"$fdr" replay "$rec" --speed max --to +50ms 2>/dev/null | wc -l | grep -q '^12$' \
  || fail "replay window"

"$fdr" recover "$rec" --dry-run >/dev/null || fail "clean recording reported damaged"

last=$(ls "$rec" | tail -1)
truncate -s -9 "$rec/$last"
set +e
"$fdr" recover "$rec" --dry-run >/dev/null
status=$?
set -e
[ "$status" -eq 2 ] || fail "torn write not detected (exit $status)"
"$fdr" recover "$rec" | grep -q '^records:          2999$' || fail "recover"
"$fdr" recover "$rec" --dry-run >/dev/null || fail "recording still damaged after recover"

"$fdr" record "$rec" --count 10 | grep -q '^recorded 10 records' || fail "resume after recover"
"$fdr" info "$rec" | grep -q '^records:    3009$' || fail "records after resume"

# A recording that ends in the future can still be extended with the default start time.
"$fdr" record "$work/future" --start-us 4000000000000000 --count 5 >/dev/null
"$fdr" record "$work/future" --count 5 | grep -q '^recorded 5 records' || fail "default start after future data"

if "$fdr" nonsense 2>/dev/null; then fail "unknown command accepted"; fi
echo "cli smoke test passed"

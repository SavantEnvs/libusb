#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS libusb's own test programs and a known-answer probe (all built by mayhem/build.sh).
# Passing is decided by parsing each program's summary output, so a neutered binary (no output) fails.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"
TEST_BUILD="$SRC/mayhem/build-tests"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

passed=0 failed=0 skipped=0

# Upstream test programs: each prints "Ran N tests / Passed P / Failed F / Error in E / Skipped S".
for t in init_context set_option stress; do
  bin="$TEST_BUILD/tests/$t"
  if [ ! -x "$bin" ]; then echo "missing test binary $bin (build.sh bug)" >&2; failed=$((failed + 1)); continue; fi
  out=$("$bin" 2>&1); rc=$?
  echo "$out"
  ran=$(sed -n 's/^.*Ran \([0-9][0-9]*\) tests.*$/\1/p' <<<"$out" | tail -n1)
  p=$(sed -n 's/^.*Passed \([0-9][0-9]*\) tests.*$/\1/p' <<<"$out" | tail -n1)
  f=$(sed -n 's/^.*Failed \([0-9][0-9]*\) tests.*$/\1/p' <<<"$out" | tail -n1)
  e=$(sed -n 's/^.*Error in \([0-9][0-9]*\) tests.*$/\1/p' <<<"$out" | tail -n1)
  s=$(sed -n 's/^.*Skipped \([0-9][0-9]*\) tests.*$/\1/p' <<<"$out" | tail -n1)
  if [ -z "$ran" ] || [ "$ran" -lt 1 ] || [ -z "$p" ] || [ "$rc" -ne 0 ]; then
    echo "FAIL $t: no valid summary (rc=$rc)" >&2
    failed=$((failed + 1))
    continue
  fi
  passed=$((passed + p)); failed=$((failed + ${f:-0} + ${e:-0})); skipped=$((skipped + ${s:-0}))
done

# stress_mt has no testlib summary; it prints "All done, N errors" after its threaded init/exit loops.
bin="$TEST_BUILD/tests/stress_mt"
if [ ! -x "$bin" ]; then
  echo "missing test binary $bin (build.sh bug)" >&2; failed=$((failed + 1))
else
  out=$("$bin" 2>&1); rc=$?
  echo "$out"
  if [ "$rc" -eq 0 ] && grep -q '^All done, 0 errors$' <<<"$out" && grep -q 'Starting 8 threads' <<<"$out"; then
    passed=$((passed + 1))
  else
    echo "FAIL stress_mt (rc=$rc)" >&2; failed=$((failed + 1))
  fi
fi

# Known-answer probe: fixed descriptor bytes -> asserted parsed fields.
expected='ss rc=0
ss attr=2 speeds=14 func=2 u1=10 u2=255
u2 rc=0
u2 attr=6
cid rc=0
cid uuid=000102030405060708090a0b0c0d0e0f
ss-short rc=-1
err -7=LIBUSB_ERROR_TIMEOUT
err -4=LIBUSB_ERROR_NO_DEVICE
strerror=Access denied (insufficient permissions)
version=1.0.99'
probe_bin="$TEST_BUILD/kat_probe"
if [ ! -x "$probe_bin" ]; then
  echo "missing $probe_bin (build.sh bug)" >&2; failed=$((failed + 1))
else
  actual=$("$probe_bin" 2>&1)
  # The patch level moves with upstream releases; assert major.minor only.
  actual=$(sed 's/^version=\([0-9]*\.[0-9]*\)\.[0-9]*$/version=\1.99/' <<<"$actual")
  if [ "$actual" = "$expected" ]; then
    passed=$((passed + 1))
  else
    echo "KAT probe mismatch:" >&2
    diff <(echo "$expected") <(echo "$actual") >&2
    failed=$((failed + 1))
  fi
fi

emit_ctrf "libusb-testlib" "$passed" "$failed" "$skipped"

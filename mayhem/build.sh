#!/usr/bin/env bash
#
# mayhem/build.sh — builds libusb sanitized (fuzz harnesses + standalone reproducers) and, separately
# with normal flags, the upstream test programs plus a known-answer probe for mayhem/test.sh.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
FUZZ_BUILD="$SRC/mayhem/build-fuzz"
TEST_BUILD="$SRC/mayhem/build-tests"

# Upstream ships no generated configure script (it is gitignored), so generate it here.
autoreconf -fi

# 1) Sanitized, coverage-instrumented static libusb. netlink hotplug (no libudev) keeps it self-contained.
mkdir -p "$FUZZ_BUILD"
(cd "$FUZZ_BUILD" && \
  "$SRC/configure" CC="$CC" \
    CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link" \
    --disable-shared --enable-static --disable-udev && \
  make -j"$MAYHEM_JOBS" -C libusb)
LIBUSB_A="$FUZZ_BUILD/libusb/.libs/libusb-1.0.a"
[ -f "$LIBUSB_A" ]

# 2) Harnesses: fuzzer binary + standalone reproducer, leak detection off at build time.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$FUZZ_BUILD/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_BUILD/standalone_main.o"
HARNESS_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -I$FUZZ_BUILD -I$SRC/libusb"
for h in fuzz_bos_descriptor fuzz_control_setup fuzz_descriptor_parsers; do
  # bos and control_setup are mayhem-layer supersets of the upstream harnesses (see their file headers).
  src="$SRC/mayhem/$h/$h.c"; [ -f "$src" ] || src="$SRC/tests/fuzz/$h.c"
  # fuzz_descriptor_parsers unity-includes descriptor.c and so must not also link libusb.
  lib="$LIBUSB_A"; [ "$h" != fuzz_descriptor_parsers ] || lib=""
  $CC $HARNESS_FLAGS "$src" "$lib" "$FUZZ_BUILD/lsan_off.o" $LIB_FUZZING_ENGINE -lpthread -o "/mayhem/$h"
  $CC $HARNESS_FLAGS "$src" "$lib" "$FUZZ_BUILD/lsan_off.o" "$FUZZ_BUILD/standalone_main.o" -lpthread -o "/mayhem/$h-standalone"
done

# 3) Test suite with the project's NORMAL flags. Upstream links tests -static, which would hide them
#    from the LD_PRELOAD sabotage check, so link them dynamically against the static library.
mkdir -p "$TEST_BUILD"
(cd "$TEST_BUILD" && \
  env -u CFLAGS -u LDFLAGS "$SRC/configure" CC="$CC" CFLAGS="$COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" \
    --disable-shared --enable-static --disable-udev --enable-tests-build && \
  make -j"$MAYHEM_JOBS" -C libusb && \
  make -j"$MAYHEM_JOBS" -C tests test_static_link_flag= stress stress_mt set_option init_context)
$CC -O1 $COVERAGE_FLAGS -I"$TEST_BUILD" -I"$SRC/libusb" "$SRC/mayhem/kat_probe.c" \
  "$TEST_BUILD/libusb/.libs/libusb-1.0.a" -lpthread -o "$TEST_BUILD/kat_probe"

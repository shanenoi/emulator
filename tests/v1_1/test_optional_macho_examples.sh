#!/bin/sh
set -eu

if [ "${EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN:-0}" = "1" ]; then
    echo "skipping optional Mach-O toolchain smoke test: EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN=1"
    exit 0
fi

if ! command -v clang >/dev/null 2>&1; then
    echo "skipping optional Mach-O toolchain check: clang is not available"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "skipping optional Mach-O toolchain check: python3 is not available"
    exit 0
fi

mkdir -p tests/v1_1/tmp
python3 examples/v1_1/generate_macho_fixtures.py --output-dir tests/v1_1/tmp
./emulator info tests/v1_1/tmp/minimal_exit.macho >/dev/null
./emulator run tests/v1_1/tmp/minimal_exit.macho >/dev/null || status=$?
status=${status:-0}
if [ "$status" -ne 7 ]; then
    echo "optional Mach-O fixture smoke failed: expected exit status 7, got $status" >&2
    exit 1
fi

if clang --target=arm64-apple-macos -c -x assembler /dev/null -o tests/v1_1/tmp/empty_macho.o >/dev/null 2>&1; then
    echo "optional Mach-O toolchain check passed: deterministic fixtures run and clang can emit arm64 Mach-O objects"
else
    echo "skipping optional real-toolchain Mach-O object check: clang cannot emit arm64-apple-macos objects here"
fi

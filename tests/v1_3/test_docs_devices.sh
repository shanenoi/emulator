#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

fail() { echo "test_docs_devices.sh failed: $*" >&2; exit 1; }
need_file() { [ -f "$1" ] || fail "missing file: $1"; }
contains() { grep -Fq "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains_i() { if grep -Eiq "$2" "$1"; then fail "stale/overclaim text in $1 matching: $2"; fi; }

need_file docs/test-plan-v1.3.md
need_file lessons/v1.3-memory-mapped-devices.md
need_file examples/v1_3/README.md
need_file examples/v1_3/generate_device_fixtures.py
need_file tests/fixtures/device_fixture_writer.py
need_file tests/v1_3/test_v1_3.c
need_file tests/v1_3/test_cli_devices.sh
need_file tests/v1_3/test_debugger_devices.sh
need_file tests/v1_3/test_docs_devices.sh

contains README.md "v1.3 Test Plan"
contains README.md "v1.3 Lesson"
contains README.md "implemented/tested teaching profile for **v1.3 — Memory-Mapped Devices**"
contains README.md "v0.1 through v1.3 deterministic test suite"
contains README.md "UART 0x09000000"
contains README.md "timer 0x09010000"
contains README.md "random 0x09020000"
contains README.md "interrupts"
contains README.md "DMA"
not_contains_i README.md "v1\.3 test suite is still the next step|upcoming v1\.3 test pass|dedicated v1\.3 coverage is the next testing milestone|initial implementation for \*\*v1\.3"

for f in lessons/v1.3-memory-mapped-devices.md examples/v1_3/README.md docs/test-plan-v1.3.md; do
    contains "$f" "0x09000000"
    contains "$f" "0x09010000"
    contains "$f" "0x09020000"
    contains "$f" "UART"
    contains "$f" "timer"
    contains "$f" "random"
    contains "$f" "deterministic"
done

for f in lessons/v1.3-memory-mapped-devices.md examples/v1_3/README.md; do
    not_contains_i "$f" "real PL011|real ARM timer|real hardware RNG|production hardware|interrupt controller|DMA support"
done

contains lessons/v1.3-memory-mapped-devices.md "not a real system-on-chip"
contains lessons/v1.3-memory-mapped-devices.md "unsupported write width"
contains lessons/v1.3-memory-mapped-devices.md "write to read-only register"
contains examples/v1_3/README.md "make examples/v1_3/mmio_uart_hello.bin"
contains examples/v1_3/README.md "./emulator run examples/v1_3/mmio_uart_hello.bin"

printf '%s\n' "v1.3 docs device tests passed"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_4/tmp
mkdir -p "$TMP"
python3 tests/fixtures/exception_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_cli_exceptions.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq -- "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

./emulator trace "$TMP/cli_handled_brk.bin" --exception-vector 0x1080 >"$TMP/cli_brk.out" 2>"$TMP/cli_brk.err"
[ ! -s "$TMP/cli_brk.err" ] || fail "CLI BRK stderr was not empty"
contains "$TMP/cli_brk.out" "trace exception-enter cause=breakpoint-or-trap(0x02)"
contains "$TMP/cli_brk.out" "fault=0x0000000000000014"
contains "$TMP/cli_brk.out" "trace exception-return cause=breakpoint-or-trap(0x02)"
contains "$TMP/cli_brk.out" "x0  = 0x0000000000000002"
contains "$TMP/cli_brk.out" "x3  = 0x0000000000001004"

./emulator trace "$TMP/mmio_handled_brk.bin" >"$TMP/mmio_brk.out" 2>"$TMP/mmio_brk.err"
[ ! -s "$TMP/mmio_brk.err" ] || fail "MMIO BRK stderr was not empty"
contains "$TMP/mmio_brk.out" "trace exception-enter cause=breakpoint-or-trap(0x02)"
contains "$TMP/mmio_brk.out" "vector=0x0000000000001080"
contains "$TMP/mmio_brk.out" "halted"

./emulator trace "$TMP/handled_invalid.bin" --exception-vector 0x1080 >"$TMP/invalid.out" 2>"$TMP/invalid.err"
[ ! -s "$TMP/invalid.err" ] || fail "handled invalid stderr was not empty"
contains "$TMP/invalid.out" "trace exception-enter cause=invalid-instruction(0x01)"
contains "$TMP/invalid.out" "resume_pc=0x0000000000001000"
contains "$TMP/invalid.out" "trace exception-return cause=invalid-instruction(0x01) resume_pc=0x0000000000001004"
contains "$TMP/invalid.out" "x0  = 0x0000000000000001"

./emulator trace "$TMP/handled_svc1.bin" --exception-vector 0x1080 >"$TMP/svc1.out" 2>"$TMP/svc1.err"
[ ! -s "$TMP/svc1.err" ] || fail "handled SVC #1 stderr was not empty"
contains "$TMP/svc1.out" "trace exception-enter cause=svc-trap(0x03)"
contains "$TMP/svc1.out" "fault=0x0000000000000001"
contains "$TMP/svc1.out" "x0  = 0x0000000000000003"

./emulator trace "$TMP/mmio_skip_device_fault.bin" >"$TMP/device.out" 2>"$TMP/device.err"
[ ! -s "$TMP/device.err" ] || fail "handled device fault stderr was not empty"
contains "$TMP/device.out" "trace exception-enter cause=device-fault(0x20)"
contains "$TMP/device.out" "fault=0x0000000009000000"
contains "$TMP/device.out" "trace exception-return cause=device-fault(0x20) resume_pc=0x0000000000001024"
contains "$TMP/device.out" "x5  = 0x0000000000000000"

./emulator trace "$TMP/mmio_timer_once.bin" >"$TMP/timer.out" 2>"$TMP/timer.err"
[ ! -s "$TMP/timer.err" ] || fail "timer interrupt stderr was not empty"
contains "$TMP/timer.out" "trace exception-enter cause=timer-interrupt(0x40)"
contains "$TMP/timer.out" "trace exception-return cause=timer-interrupt(0x40)"
contains "$TMP/timer.out" "x0  = 0x0000000009030000"

./emulator info "$TMP/cli_handled_brk.bin" --exception-vector 0x1080 --timer-interrupt 7 --queue-timer --interrupts off >"$TMP/info.out" 2>"$TMP/info.err"
[ ! -s "$TMP/info.err" ] || fail "info stderr was not empty"
contains "$TMP/info.out" "exception_vector_configured: yes"
contains "$TMP/info.out" "exception_vector_base: 0x0000000000001080"
contains "$TMP/info.out" "interrupts_enabled: no"
contains "$TMP/info.out" "timer_interrupt_interval: 0x0000000000000007"
contains "$TMP/info.out" "pending_timer_interrupt: yes"
contains "$TMP/info.out" "devices: 3 legacy + 1 exception"
contains "$TMP/info.out" "name=exception"

if ./emulator run "$TMP/unhandled_brk.bin" >"$TMP/unhandled.out" 2>"$TMP/unhandled.err"; then
    fail "unhandled BRK unexpectedly succeeded"
fi
contains "$TMP/unhandled.err" "unhandled exception"
contains "$TMP/unhandled.err" "breakpoint-or-trap"

if ./emulator run "$TMP/eret_without_exception.bin" >"$TMP/eret.out" 2>"$TMP/eret.err"; then
    fail "ERET without exception unexpectedly succeeded"
fi
contains "$TMP/eret.err" "eret without active exception"

if ./emulator run "$TMP/vector_fault.bin" --exception-vector 0x1080 >"$TMP/double.out" 2>"$TMP/double.err"; then
    fail "vector decode double fault unexpectedly succeeded"
fi
contains "$TMP/double.err" "double fault"
contains "$TMP/double.err" "invalid-instruction"

if ./emulator run "$TMP/cli_handled_brk.bin" --exception-vector 0x1082 >"$TMP/badvec.out" 2>"$TMP/badvec.err"; then
    fail "unaligned vector unexpectedly succeeded"
fi
contains "$TMP/badvec.err" "exception vector must be 4-byte aligned"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "--exception-vector"
contains "$TMP/help.out" "--timer-interrupt"
contains "$TMP/help.out" "exception-controller MMIO device"

printf '%s\n' "v1.4 CLI exception tests passed"

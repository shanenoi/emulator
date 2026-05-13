#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_3/tmp
mkdir -p "$TMP"
python3 tests/fixtures/device_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_cli_devices.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

./emulator run "$TMP/uart_hi.bin" >"$TMP/uart_hi.out" 2>"$TMP/uart_hi.err"
[ ! -s "$TMP/uart_hi.err" ] || fail "uart_hi stderr was not empty"
printf 'Hi\n' >"$TMP/uart_hi.expected"
head -c 3 "$TMP/uart_hi.out" >"$TMP/uart_hi.prefix"
cmp "$TMP/uart_hi.expected" "$TMP/uart_hi.prefix" || fail "UART Hi output prefix mismatch"
contains "$TMP/uart_hi.out" "halted"

./emulator run "$TMP/uart_no_svc.bin" >"$TMP/uart_no_svc.out" 2>"$TMP/uart_no_svc.err"
[ ! -s "$TMP/uart_no_svc.err" ] || fail "uart_no_svc stderr was not empty"
head -c 1 "$TMP/uart_no_svc.out" >"$TMP/uart_no_svc.prefix"
printf 'M' >"$TMP/uart_no_svc.expected"
cmp "$TMP/uart_no_svc.expected" "$TMP/uart_no_svc.prefix" || fail "MMIO-only UART output mismatch"

./emulator run "$TMP/uart_syscall_order.bin" >"$TMP/uart_order.out" 2>"$TMP/uart_order.err"
[ ! -s "$TMP/uart_order.err" ] || fail "uart/syscall order stderr was not empty"
head -c 3 "$TMP/uart_order.out" >"$TMP/uart_order.prefix"
printf 'ABC' >"$TMP/uart_order.expected"
cmp "$TMP/uart_order.expected" "$TMP/uart_order.prefix" || fail "UART/syscall ordering mismatch"

./emulator run "$TMP/uart_nul_high.bin" >"$TMP/uart_nul_high.out" 2>"$TMP/uart_nul_high.err"
[ ! -s "$TMP/uart_nul_high.err" ] || fail "uart_nul_high stderr was not empty"
head -c 3 "$TMP/uart_nul_high.out" >"$TMP/uart_nul_high.prefix"
printf '\000X\377' >"$TMP/uart_nul_high.expected"
cmp "$TMP/uart_nul_high.expected" "$TMP/uart_nul_high.prefix" || fail "UART NUL/high-bit byte output mismatch"

./emulator run "$TMP/uart_after_hlt.bin" >"$TMP/uart_after_hlt.out" 2>"$TMP/uart_after_hlt.err"
[ ! -s "$TMP/uart_after_hlt.err" ] || fail "uart_after_hlt stderr was not empty"
head -c 1 "$TMP/uart_after_hlt.out" >"$TMP/uart_after_hlt.prefix"
printf 'A' >"$TMP/uart_after_hlt.expected"
cmp "$TMP/uart_after_hlt.expected" "$TMP/uart_after_hlt.prefix" || fail "UART after-HLT output mismatch"
not_contains "$TMP/uart_after_hlt.out" "Z"

./emulator run "$TMP/uart_large.bin" >"$TMP/uart_large.out" 2>"$TMP/uart_large.err"
[ ! -s "$TMP/uart_large.err" ] || fail "uart_large stderr was not empty"
python3 - <<'PY'
from pathlib import Path
out = Path('tests/v1_3/tmp/uart_large.out').read_bytes()
expected = b'0123456789abcdef' * 64
if out[:len(expected)] != expected:
    raise SystemExit('large UART payload mismatch')
PY

./emulator trace "$TMP/uart_no_svc.bin" >"$TMP/trace.out" 2>"$TMP/trace.err"
[ ! -s "$TMP/trace.err" ] || fail "trace stderr was not empty"
contains "$TMP/trace.out" "trace pc=0x000000000000100c"
contains "$TMP/trace.out" "strb w1, [x0]"
contains "$TMP/trace.out" "Mtrace pc="

./emulator regs "$TMP/timer_read.bin" >"$TMP/timer_regs.out" 2>"$TMP/timer_regs.err"
[ ! -s "$TMP/timer_regs.err" ] || fail "timer regs stderr was not empty"
contains "$TMP/timer_regs.out" "x1  = 0x0000000000000000"
contains "$TMP/timer_regs.out" "x2  = 0x0000000000000000"

./emulator regs "$TMP/random_read.bin" >"$TMP/random_regs.out" 2>"$TMP/random_regs.err"
[ ! -s "$TMP/random_regs.err" ] || fail "random regs stderr was not empty"
contains "$TMP/random_regs.out" "x1  = 0x000000002a72c675"
contains "$TMP/random_regs.out" "x2  = 0x0000000080c2a550"

./emulator info "$TMP/uart_hi.bin" >"$TMP/info.out" 2>"$TMP/info.err"
[ ! -s "$TMP/info.err" ] || fail "info stderr was not empty"
contains "$TMP/info.out" "devices: 3"
contains "$TMP/info.out" "name=uart"
contains "$TMP/info.out" "0x0000000009000000-0x0000000009001000"
contains "$TMP/info.out" "name=timer"
contains "$TMP/info.out" "name=random"

if ./emulator dump "$TMP/uart_hi.bin" 0x09000000 4 >"$TMP/dump_device.out" 2>"$TMP/dump_device.err"; then
    fail "dumping a device address unexpectedly succeeded"
fi
contains "$TMP/dump_device.err" "dump range out of bounds"

if ./emulator run "$TMP/uart_word_width.bin" >"$TMP/bad_width.out" 2>"$TMP/bad_width.err"; then
    fail "unsupported UART width unexpectedly succeeded"
fi
[ ! -s "$TMP/bad_width.out" ] || fail "unsupported UART width emitted partial stdout"
contains "$TMP/bad_width.err" "unsupported write width"
contains "$TMP/bad_width.err" "device=uart"

if ./emulator run "$TMP/uart_half_width.bin" >"$TMP/half_width.out" 2>"$TMP/half_width.err"; then
    fail "unsupported UART halfword width unexpectedly succeeded"
fi
[ ! -s "$TMP/half_width.out" ] || fail "unsupported UART halfword emitted partial stdout"
contains "$TMP/half_width.err" "unsupported write width"

if ./emulator run "$TMP/uart_cross_register.bin" >"$TMP/cross_register.out" 2>"$TMP/cross_register.err"; then
    fail "UART cross-register write unexpectedly succeeded"
fi
[ ! -s "$TMP/cross_register.out" ] || fail "UART cross-register write emitted partial stdout"
contains "$TMP/cross_register.err" "unaligned write access"

if ./emulator run "$TMP/uart_bad_offset.bin" >"$TMP/bad_offset.out" 2>"$TMP/bad_offset.err"; then
    fail "invalid UART offset unexpectedly succeeded"
fi
[ ! -s "$TMP/bad_offset.out" ] || fail "invalid UART offset emitted partial stdout"
contains "$TMP/bad_offset.err" "invalid write register"
contains "$TMP/bad_offset.err" "device=uart"

if ./emulator run "$TMP/timer_unaligned.bin" >"$TMP/timer_unaligned.out" 2>"$TMP/timer_unaligned.err"; then
    fail "unaligned timer access unexpectedly succeeded"
fi
contains "$TMP/timer_unaligned.err" "unaligned read access"

./emulator regs "$TMP/timer_reset.bin" >"$TMP/timer_reset.out" 2>"$TMP/timer_reset.err"
[ ! -s "$TMP/timer_reset.err" ] || fail "timer reset stderr was not empty"
contains "$TMP/timer_reset.out" "x1  = 0x0000000000000000"
contains "$TMP/timer_reset.out" "x2  = 0x0000000000000000"

if ./emulator run "$TMP/timer_byte.bin" >"$TMP/timer_byte.out" 2>"$TMP/timer_byte.err"; then
    fail "timer byte read unexpectedly succeeded"
fi
contains "$TMP/timer_byte.err" "unsupported read width"

if ./emulator run "$TMP/random_invalid.bin" >"$TMP/random_invalid.out" 2>"$TMP/random_invalid.err"; then
    fail "invalid random offset unexpectedly succeeded"
fi
contains "$TMP/random_invalid.err" "invalid read register"

./emulator regs "$TMP/random_seed.bin" >"$TMP/random_seed.out" 2>"$TMP/random_seed.err"
[ ! -s "$TMP/random_seed.err" ] || fail "random seed stderr was not empty"
contains "$TMP/random_seed.out" "x2  = 0x00000000722d9803"

if ./emulator run "$TMP/random_half.bin" >"$TMP/random_half.out" 2>"$TMP/random_half.err"; then
    fail "random halfword read unexpectedly succeeded"
fi
contains "$TMP/random_half.err" "unsupported read width"

if ./emulator run "$TMP/edge_device_boundary.bin" >"$TMP/cross.out" 2>"$TMP/cross.err"; then
    fail "cross-device-boundary access unexpectedly succeeded"
fi
[ ! -s "$TMP/cross.out" ] || fail "cross-device-boundary access emitted partial stdout"
contains "$TMP/cross.err" "crosses device boundary"

./emulator run "$TMP/uart_elf.elf" >"$TMP/elf.out" 2>"$TMP/elf.err"
[ ! -s "$TMP/elf.err" ] || fail "ELF UART stderr was not empty"
head -c 1 "$TMP/elf.out" >"$TMP/elf.prefix"
printf 'E' >"$TMP/elf.expected"
cmp "$TMP/elf.expected" "$TMP/elf.prefix" || fail "ELF UART output mismatch"

./emulator run "$TMP/uart_constant.elf" >"$TMP/const.out" 2>"$TMP/const.err"
[ ! -s "$TMP/const.err" ] || fail "ELF constant stderr was not empty"
head -c 1 "$TMP/const.out" >"$TMP/const.prefix"
printf 'K' >"$TMP/const.expected"
cmp "$TMP/const.expected" "$TMP/const.prefix" || fail "ELF device constant output mismatch"

if ./emulator run "$TMP/overlap_device.elf" >"$TMP/overlap.out" 2>"$TMP/overlap.err"; then
    fail "ELF segment overlapping device unexpectedly loaded"
fi
contains "$TMP/overlap.err" "reserved device range"

if ./emulator run "$TMP/entry_device.elf" >"$TMP/entry_device.out" 2>"$TMP/entry_device.err"; then
    fail "ELF entry in device range unexpectedly executed"
fi
contains "$TMP/entry_device.err" "reserved non-executable device range"

if ./emulator run "$TMP/stp_device.bin" >"$TMP/stp.out" 2>"$TMP/stp.err"; then
    fail "STP to device unexpectedly succeeded"
fi
contains "$TMP/stp.err" "unsupported write width"

./emulator regs "$TMP/pre_index_timer.bin" >"$TMP/pre_index.out" 2>"$TMP/pre_index.err"
[ ! -s "$TMP/pre_index.err" ] || fail "pre-index timer stderr was not empty"
contains "$TMP/pre_index.out" "x0  = 0x0000000009010000"
contains "$TMP/pre_index.out" "x1  = 0x0000000000000000"

./emulator regs "$TMP/post_index_uart.bin" >"$TMP/post_index.out" 2>"$TMP/post_index.err"
[ ! -s "$TMP/post_index.err" ] || fail "post-index UART stderr was not empty"
head -c 1 "$TMP/post_index.out" >"$TMP/post_index.prefix"
printf 'P' >"$TMP/post_index.expected"
cmp "$TMP/post_index.expected" "$TMP/post_index.prefix" || fail "post-index UART output mismatch"
contains "$TMP/post_index.out" "x0  = 0x0000000009000004"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "MMIO teaching devices"
contains "$TMP/help.out" "UART 0x09000000"
not_contains "$TMP/help.out" "fake syscalls are the only output"

printf '%s\n' "v1.3 CLI device tests passed"

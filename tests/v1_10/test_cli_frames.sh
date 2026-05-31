#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_10/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_frames.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

python3 - <<'PY'
from pathlib import Path

def u32(x): return x.to_bytes(4, 'little')
def movz_x(rd, imm, shift=0): return 0xd2800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def movz_w(rd, imm, shift=0): return 0x52800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def ldr_w(rt, rn, imm): return 0xb9400000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def str_w(rt, rn, imm): return 0xb9000000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def strb(rt, rn, imm=0): return 0x39000000 | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def add_w_imm(rd, rn, imm): return 0x11000000 | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rd & 31)
def cbz_w(rt, imm): return 0x34000000 | (((imm // 4) & 0x7ffff) << 5) | (rt & 31)
def branch(imm): return 0x14000000 | ((imm // 4) & 0x03ffffff)
def svc(): return 0xd4000001

def exit0(program):
    program.extend([movz_x(0, 0), movz_x(8, 93), svc()])

# Wait until the runner advances at least one frame, then echo the low counter digit.
program = [
    movz_x(20, 0x0906, 16),
    movz_x(21, 0x0900, 16),
    # loop:
    ldr_w(1, 20, 0x00),
    cbz_w(1, -4),
    ldr_w(2, 20, 0x04),
    add_w_imm(2, 2, ord('0')),
    strb(2, 21, 0),
    movz_w(3, 1),
    str_w(3, 20, 0x0c),
]
exit0(program)
Path('tests/v1_10/tmp/frame_echo.bin').write_bytes(b''.join(u32(x) for x in program))

# Infinite branch for deterministic frame-slice completion without guest exit.
Path('tests/v1_10/tmp/infinite.bin').write_bytes(u32(branch(0)))
PY

./emulator run "$TMP/frame_echo.bin" --frames 2 --instructions-per-frame 8 >"$TMP/frame_echo.out" 2>"$TMP/frame_echo.err"
[ ! -s "$TMP/frame_echo.err" ] || fail "frame echo stderr was not empty"
printf '1' >"$TMP/frame_echo.expected"
head -c 1 "$TMP/frame_echo.out" >"$TMP/frame_echo.prefix"
cmp "$TMP/frame_echo.expected" "$TMP/frame_echo.prefix" || fail "frame echo output mismatch"

./emulator run "$TMP/infinite.bin" --frames 3 --instructions-per-frame 1 >"$TMP/infinite.out" 2>"$TMP/infinite.err"
[ ! -s "$TMP/infinite.err" ] || fail "infinite frame stderr was not empty"
contains "$TMP/infinite.out" "halted"

if ./emulator run "$TMP/infinite.bin" --frames 0 >"$TMP/bad_zero.out" 2>"$TMP/bad_zero.err"; then
    fail "--frames 0 unexpectedly succeeded"
fi
contains "$TMP/bad_zero.err" "invalid --frames value"

if ./emulator run "$TMP/infinite.bin" --frames nope >"$TMP/bad_text.out" 2>"$TMP/bad_text.err"; then
    fail "--frames text unexpectedly succeeded"
fi
contains "$TMP/bad_text.err" "invalid --frames value"

if ./emulator run "$TMP/infinite.bin" --frames 1 --interactive >"$TMP/bad_combo.out" 2>"$TMP/bad_combo.err"; then
    fail "--frames with --interactive unexpectedly succeeded"
fi
contains "$TMP/bad_combo.err" "--frames cannot be combined with --interactive"

if ./emulator run "$TMP/infinite.bin" --fps 60 >"$TMP/bad_fps_scope.out" 2>"$TMP/bad_fps_scope.err"; then
    fail "--fps without --interactive unexpectedly succeeded"
fi
contains "$TMP/bad_fps_scope.err" "--fps is only supported with --interactive"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "--frames <N>"
contains "$TMP/help.out" "0x09060000"

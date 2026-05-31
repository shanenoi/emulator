#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_7/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_keyboard.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

python3 - <<'PY'
from pathlib import Path

def u32(x): return x.to_bytes(4, 'little')
def movz_x(rd, imm, shift=0): return 0xd2800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def ldr_w(rt, rn, imm): return 0xb9400000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def strb(rt, rn, imm=0): return 0x39000000 | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def hlt(): return 0xd4400000

# x0 = keyboard base, read status/data, x3 = uart base, echo one input byte.
program = [
    movz_x(0, 0x0904, 16),
    ldr_w(1, 0, 0),
    ldr_w(2, 0, 4),
    movz_x(3, 0x0900, 16),
    strb(2, 3),
    hlt(),
]
Path('tests/v1_7/tmp/kbd_echo_one.bin').write_bytes(b''.join(u32(x) for x in program))
Path('tests/v1_7/tmp/input.txt').write_bytes(b'Z')
PY

./emulator run "$TMP/kbd_echo_one.bin" --input q >"$TMP/input.out" 2>"$TMP/input.err"
[ ! -s "$TMP/input.err" ] || fail "--input stderr was not empty"
head -c 1 "$TMP/input.out" >"$TMP/input.prefix"
printf 'q' >"$TMP/input.expected"
cmp "$TMP/input.expected" "$TMP/input.prefix" || fail "--input echo mismatch"
contains "$TMP/input.out" "halted"

./emulator run "$TMP/kbd_echo_one.bin" --input-file "$TMP/input.txt" >"$TMP/file.out" 2>"$TMP/file.err"
[ ! -s "$TMP/file.err" ] || fail "--input-file stderr was not empty"
head -c 1 "$TMP/file.out" >"$TMP/file.prefix"
printf 'Z' >"$TMP/file.expected"
cmp "$TMP/file.expected" "$TMP/file.prefix" || fail "--input-file echo mismatch"

./emulator regs "$TMP/kbd_echo_one.bin" >"$TMP/empty.out" 2>"$TMP/empty.err"
[ ! -s "$TMP/empty.err" ] || fail "empty keyboard stderr was not empty"
contains "$TMP/empty.out" "x1  = 0x0000000000000000"
contains "$TMP/empty.out" "x2  = 0x0000000000000000"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "--input <text>"
contains "$TMP/help.out" "0x09040000"

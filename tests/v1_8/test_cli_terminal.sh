#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_8/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_terminal.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

python3 - <<'PY'
from pathlib import Path

def u32(x): return x.to_bytes(4, 'little')
def movz_x(rd, imm, shift=0): return 0xd2800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def movz_w(rd, imm, shift=0): return 0x52800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def movk_w(rd, imm, shift=0): return 0x72800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)
def ldr_w(rt, rn, imm): return 0xb9400000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def str_w(rt, rn, imm): return 0xb9000000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def strb(rt, rn, imm=0): return 0x39000000 | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)
def svc(): return 0xd4000001

def write_char(program, ch):
    program.append(movz_w(1, ord(ch)))
    program.append(strb(1, 0, 0x14))

def exit0(program):
    program.extend([movz_x(0, 0), movz_x(8, 93), svc()])

# Draw HELLO at row 0 via TERM_DATA and @ at x=4,y=1 via TERM_CELL, then exit via syscall.
program = [movz_x(0, 0x0905, 16)]
program.append(movz_w(1, 1))
program.append(str_w(1, 0, 0x18))
for ch in "HELLO":
    write_char(program, ch)
program.append(movz_w(1, 14))
program.append(str_w(1, 0, 0x20))
program.append(movz_w(1, ord('@')))
program.append(strb(1, 0, 0x24))
exit0(program)
Path('tests/v1_8/tmp/terminal_draw.bin').write_bytes(b''.join(u32(x) for x in program))

# Read width/height and exit with width+height so CLI size is guest-visible.
program = [movz_x(0, 0x0905, 16), ldr_w(1, 0, 0x04), ldr_w(2, 0, 0x08),
           0x0b020020, # add w0, w1, w2
           movz_x(8, 93), svc()]
Path('tests/v1_8/tmp/terminal_size_exit.bin').write_bytes(b''.join(u32(x) for x in program))
PY

./emulator run "$TMP/terminal_draw.bin" --screen-size 10x3 --screen-dump --screen-border unicode >"$TMP/unicode.out" 2>"$TMP/unicode.err"
[ ! -s "$TMP/unicode.err" ] || fail "unicode stderr was not empty"
cat >"$TMP/unicode.expected" <<'EOF_EXPECTED'
┌──────────┐
│HELLO     │
│    @     │
│          │
└──────────┘
EOF_EXPECTED
cmp "$TMP/unicode.expected" "$TMP/unicode.out" || fail "unicode screen dump mismatch"

./emulator run "$TMP/terminal_draw.bin" --screen-size 10x3 --screen-dump --screen-border ascii >"$TMP/ascii.out" 2>"$TMP/ascii.err"
[ ! -s "$TMP/ascii.err" ] || fail "ascii stderr was not empty"
cat >"$TMP/ascii.expected" <<'EOF_EXPECTED'
+----------+
|HELLO     |
|    @     |
|          |
+----------+
EOF_EXPECTED
cmp "$TMP/ascii.expected" "$TMP/ascii.out" || fail "ascii screen dump mismatch"

./emulator run "$TMP/terminal_draw.bin" --screen-size 10x3 --screen-dump --screen-border none >"$TMP/none.out" 2>"$TMP/none.err"
[ ! -s "$TMP/none.err" ] || fail "none stderr was not empty"
python3 - <<'PY'
from pathlib import Path
expected = b"HELLO     \n    @     \n          \n"
actual = Path('tests/v1_8/tmp/none.out').read_bytes()
if actual != expected:
    raise SystemExit(f'none screen dump mismatch: {actual!r}')
PY

# Guest exits with width + height = 13.
if ./emulator run "$TMP/terminal_size_exit.bin" --screen-size 10x3 >/dev/null 2>/dev/null; then
    fail "screen size program unexpectedly exited with status 0"
else
    status=$?
    [ "$status" -eq 13 ] || fail "expected exit status 13, got $status"
fi

if ./emulator run "$TMP/terminal_draw.bin" --screen-size 0x3 >"$TMP/bad_size.out" 2>"$TMP/bad_size.err"; then
    fail "invalid screen size unexpectedly succeeded"
fi
contains "$TMP/bad_size.err" "invalid --screen-size value"

if ./emulator run "$TMP/terminal_draw.bin" --screen-border emoji >"$TMP/bad_border.out" 2>"$TMP/bad_border.err"; then
    fail "invalid screen border unexpectedly succeeded"
fi
contains "$TMP/bad_border.err" "invalid --screen-border value"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "--screen-size <WxH>"
contains "$TMP/help.out" "--screen-dump"
contains "$TMP/help.out" "--screen-border <unicode|ascii|none>"
contains "$TMP/help.out" "0x09050000"

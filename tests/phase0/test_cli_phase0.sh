#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/phase0/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_phase0.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq "$2" "$1" || { echo "--- $1 ---" >&2; cat "$1" >&2; fail "expected $1 to contain: $2"; }; }
not_contains() { if grep -Fq "$2" "$1"; then echo "--- $1 ---" >&2; cat "$1" >&2; fail "did not expect $1 to contain: $2"; fi; }
not_success() {
    if "$@" >"$TMP/cmd.out" 2>"$TMP/cmd.err"; then
        fail "command unexpectedly succeeded: $*"
    fi
}
run_debug() {
    script="$1"
    binary="$2"
    out="$3"
    err="$4"
    printf '%s' "$script" | ./emulator debug "$binary" >"$out" 2>"$err"
}

make regression-examples >/dev/null

# Minimal raw fixture for stable CLI and debugger formatting checks: NOP; HLT.
printf '\037\040\003\325\000\000\100\324' >"$TMP/nop_hlt.bin"

./emulator regs "$TMP/nop_hlt.bin" >"$TMP/regs.out" 2>"$TMP/regs.err"
[ ! -s "$TMP/regs.err" ] || fail "regs stderr was not empty"
contains "$TMP/regs.out" "x0  = 0x0000000000000000"
contains "$TMP/regs.out" "sp  = 0x0000000000100000"
contains "$TMP/regs.out" "pc  = 0x0000000000001004"
contains "$TMP/regs.out" "nzcv = 0000"
contains "$TMP/regs.out" "instructions = 0x0000000000000002"

./emulator dump "$TMP/nop_hlt.bin" 0x1000 8 >"$TMP/dump.out" 2>"$TMP/dump.err"
[ ! -s "$TMP/dump.err" ] || fail "dump stderr was not empty"
contains "$TMP/dump.out" "0x0000000000001000: 1f 20 03 d5 00 00 40 d4"

not_success ./emulator dump "$TMP/nop_hlt.bin" nope 8
contains "$TMP/cmd.err" "invalid dump address or length"
not_success ./emulator dump "$TMP/nop_hlt.bin" 0x1000 nope
contains "$TMP/cmd.err" "invalid dump address or length"
not_success ./emulator run "$TMP/missing.bin"
contains "$TMP/cmd.err" "failed to open"

run_debug '
regs
mem 0x1000 8
step
regs
wat
mem nope 8
quit
' "$TMP/nop_hlt.bin" "$TMP/debug.out" "$TMP/debug.err"
contains "$TMP/debug.out" "tiny-aarch64 debugger v0.5"
contains "$TMP/debug.out" "emu>"
contains "$TMP/debug.out" "pc  = 0x0000000000001000"
contains "$TMP/debug.out" "0x0000000000001000: 1f 20 03 d5 00 00 40 d4"
contains "$TMP/debug.out" "pc  = 0x0000000000001004"
contains "$TMP/debug.out" "instructions = 0x0000000000000001"
contains "$TMP/debug.err" "unknown command: wat"
contains "$TMP/debug.err" "usage: mem <address> <length>"
not_contains "$TMP/debug.err" "usage: regs"

printf '%s\n' "phase0 CLI/debugger characterization tests passed"
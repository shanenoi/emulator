#!/bin/sh
set -eu

TMP_DIR="tests/v0_1/tmp"
mkdir -p "$TMP_DIR"

require_contains() {
    file="$1"
    needle="$2"
    if ! grep -Fq "$needle" "$file"; then
        echo "FAIL: expected $file to contain: $needle" >&2
        echo "--- $file ---" >&2
        cat "$file" >&2
        exit 1
    fi
}

require_not_success() {
    if "$@" >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt"; then
        echo "FAIL: command unexpectedly succeeded: $*" >&2
        exit 1
    fi
}

make examples >/dev/null

./emulator run examples/v0_1/add.bin >"$TMP_DIR/add.out" 2>"$TMP_DIR/add.err"
require_contains "$TMP_DIR/add.out" "halted"
require_contains "$TMP_DIR/add.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/add.out" "x1  = 0x0000000000000003"
require_contains "$TMP_DIR/add.out" "x2  = 0x0000000000000005"
require_contains "$TMP_DIR/add.out" "sp  = 0x0000000000100000"
require_contains "$TMP_DIR/add.out" "pc  = 0x000000000000100c"
require_contains "$TMP_DIR/add.out" "instructions = 0x0000000000000004"

require_not_success ./emulator
require_contains "$TMP_DIR/stderr.txt" "usage: emulator run <raw-binary>"

require_not_success ./emulator unknown examples/v0_1/add.bin
require_contains "$TMP_DIR/stderr.txt" "error: unknown command: unknown"

require_not_success ./emulator run "$TMP_DIR/missing.bin"
require_contains "$TMP_DIR/stderr.txt" "loader error"
require_contains "$TMP_DIR/stderr.txt" "missing.bin"

printf '\377\377\377\377' >"$TMP_DIR/unsupported.bin"
require_not_success ./emulator run "$TMP_DIR/unsupported.bin"
require_contains "$TMP_DIR/stderr.txt" "decode error"
require_contains "$TMP_DIR/stderr.txt" "0xffffffff"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"

: >"$TMP_DIR/empty.bin"
require_not_success ./emulator run "$TMP_DIR/empty.bin"
require_contains "$TMP_DIR/stderr.txt" "input file is empty"

printf 'v0.1 CLI tests passed\n'

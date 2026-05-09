#!/bin/sh
set -eu

TMP_DIR="tests/v0_2/tmp"
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

require_not_contains() {
    file="$1"
    needle="$2"
    if grep -Fq "$needle" "$file"; then
        echo "FAIL: expected $file not to contain: $needle" >&2
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

# TC-V02-CLI-001: run supports a v0.2 loop example.
./emulator run examples/v0_2/cbnz_countdown.bin >"$TMP_DIR/countdown.out" 2>"$TMP_DIR/countdown.err"
require_contains "$TMP_DIR/countdown.out" "halted"
require_contains "$TMP_DIR/countdown.out" "x0  = 0x0000000000000000"
require_contains "$TMP_DIR/countdown.out" "x1  = 0x0000000000000005"
require_not_contains "$TMP_DIR/countdown.out" "trace pc="

# TC-V02-CLI-001: run supports the CMP + B.cond loop example.
./emulator run examples/v0_2/cmp_bcond_loop.bin >"$TMP_DIR/cmp_loop.out" 2>"$TMP_DIR/cmp_loop.err"
require_contains "$TMP_DIR/cmp_loop.out" "halted"
require_contains "$TMP_DIR/cmp_loop.out" "x0  = 0x0000000000000005"
require_contains "$TMP_DIR/cmp_loop.out" "x1  = 0x0000000000000005"

# TC-V02-TRACE-001/002/004 and TC-V02-ACC-004: trace prints PCs and final dump.
./emulator trace examples/v0_2/trace_loop.bin >"$TMP_DIR/trace.out" 2>"$TMP_DIR/trace.err"
require_contains "$TMP_DIR/trace.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace.out" "trace pc=0x0000000000001008"
require_contains "$TMP_DIR/trace.out" "halted"
require_contains "$TMP_DIR/trace.out" "x0  = 0x0000000000000000"
require_contains "$TMP_DIR/trace.out" "instructions = 0x"

# TC-V02-TRACE-003: trace reports instruction-limit errors and repeated address.
require_not_success ./emulator trace examples/v0_2/infinite_branch.bin
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "instruction limit"

# TC-V02-CLI-002: invalid trace invocation prints usage.
require_not_success ./emulator trace
require_contains "$TMP_DIR/stderr.txt" "usage: emulator run <raw-binary>"
require_contains "$TMP_DIR/stderr.txt" "emulator trace <raw-binary>"

# TC-V02-CLI-003: invalid command still fails.
require_not_success ./emulator branch examples/v0_2/cbnz_countdown.bin
require_contains "$TMP_DIR/stderr.txt" "error: unknown command: branch"

# TC-V02-CLI-004: instruction-limit failure exits non-zero.
require_not_success ./emulator run examples/v0_2/infinite_branch.bin
require_contains "$TMP_DIR/stderr.txt" "instruction limit"
require_not_contains "$TMP_DIR/stdout.txt" "halted"

printf 'v0.2 CLI/trace tests passed\n'

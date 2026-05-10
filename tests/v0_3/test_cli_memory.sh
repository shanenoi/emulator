#!/bin/sh
set -eu

TMP_DIR="tests/v0_3/tmp"
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

# TC-V03-CLI-001 and TC-V03-ACC-001: run supports the basic store/load example.
./emulator run examples/v0_3/memory_store_load.bin >"$TMP_DIR/memory_store_load.out" 2>"$TMP_DIR/memory_store_load.err"
require_contains "$TMP_DIR/memory_store_load.out" "halted"
require_contains "$TMP_DIR/memory_store_load.out" "x1  = 0x000000000000002a"
require_contains "$TMP_DIR/memory_store_load.out" "sp  = 0x0000000000100000"

# TC-V03-ACC-002: stack push/pop example demonstrates LIFO behavior.
./emulator run examples/v0_3/stack_push_pop.bin >"$TMP_DIR/stack_push_pop.out" 2>"$TMP_DIR/stack_push_pop.err"
require_contains "$TMP_DIR/stack_push_pop.out" "halted"
require_contains "$TMP_DIR/stack_push_pop.out" "x2  = 0x0000000000000002"
require_contains "$TMP_DIR/stack_push_pop.out" "x3  = 0x0000000000000001"
require_contains "$TMP_DIR/stack_push_pop.out" "sp  = 0x0000000000100000"

# TC-V03-ACC-003: pair store/load stack example.
./emulator run examples/v0_3/stp_ldp_stack.bin >"$TMP_DIR/stp_ldp_stack.out" 2>"$TMP_DIR/stp_ldp_stack.err"
require_contains "$TMP_DIR/stp_ldp_stack.out" "halted"
require_contains "$TMP_DIR/stp_ldp_stack.out" "x2  = 0x0000000000000064"
require_contains "$TMP_DIR/stp_ldp_stack.out" "x3  = 0x00000000000000c8"
require_contains "$TMP_DIR/stp_ldp_stack.out" "sp  = 0x0000000000100000"

# TC-V03-ACC-004: W register load/store example zero-extends into X register.
./emulator run examples/v0_3/w_register_load_store.bin >"$TMP_DIR/w_register_load_store.out" 2>"$TMP_DIR/w_register_load_store.err"
require_contains "$TMP_DIR/w_register_load_store.out" "halted"
require_contains "$TMP_DIR/w_register_load_store.out" "x1  = 0x00000000ffff0123"

# TC-V03-ACC-005: invalid memory access example fails clearly.
require_not_success ./emulator run examples/v0_3/invalid_memory_access.bin
require_contains "$TMP_DIR/stderr.txt" "out of bounds"

# TC-V03-CLI-002/003: dump syntax accepts hex values and shows memory after program execution.
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8 >"$TMP_DIR/dump_hex.out" 2>"$TMP_DIR/dump_hex.err"
require_contains "$TMP_DIR/dump_hex.out" "halted"
require_contains "$TMP_DIR/dump_hex.out" "memory dump address=0x00000000000ffff8 length=0x0000000000000008"
require_contains "$TMP_DIR/dump_hex.out" "0x00000000000ffff8: 2a 00 00 00 00 00 00 00"

# TC-V03-CLI-002: dump syntax also accepts decimal values through strtoull base 0.
./emulator dump examples/v0_3/memory_store_load.bin 1048568 8 >"$TMP_DIR/dump_decimal.out" 2>"$TMP_DIR/dump_decimal.err"
require_contains "$TMP_DIR/dump_decimal.out" "0x00000000000ffff8: 2a 00 00 00 00 00 00 00"

# TC-V03-CLI-004: out-of-bounds dump range fails clearly.
require_not_success ./emulator dump examples/v0_3/stack_push_pop.bin 0xffff8 16
require_contains "$TMP_DIR/stderr.txt" "dump range out of bounds"

# TC-V03-CLI-002: missing/invalid dump arguments print usage and fail.
require_not_success ./emulator dump examples/v0_3/memory_store_load.bin
require_contains "$TMP_DIR/stderr.txt" "usage: emulator run <raw-binary>"
require_not_success ./emulator dump examples/v0_3/memory_store_load.bin nope 8
require_contains "$TMP_DIR/stderr.txt" "invalid dump address or length"

# TC-V03-CLI-005: trace still works for a memory program and includes final dump.
./emulator trace examples/v0_3/stack_push_pop.bin >"$TMP_DIR/trace_stack.out" 2>"$TMP_DIR/trace_stack.err"
require_contains "$TMP_DIR/trace_stack.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace_stack.out" "trace pc=0x0000000000001008"
require_contains "$TMP_DIR/trace_stack.out" "halted"
require_contains "$TMP_DIR/trace_stack.out" "x2  = 0x0000000000000002"

printf 'v0.3 CLI/memory tests passed\n'

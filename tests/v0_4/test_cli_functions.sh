#!/bin/sh
set -eu

TMP_DIR="tests/v0_4/tmp"
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

make regression-examples >/dev/null

# TC-V04-CLI-001 and TC-V04-ACC-001: simple BL/RET call.
./emulator run examples/v0_4/simple_call.bin >"$TMP_DIR/simple_call.out" 2>"$TMP_DIR/simple_call.err"
require_contains "$TMP_DIR/simple_call.out" "halted"
require_contains "$TMP_DIR/simple_call.out" "x0  = 0x0000000000000005"
require_contains "$TMP_DIR/simple_call.out" "pc  = 0x0000000000001008"

# TC-V04-ACC-002: sequential calls return to the correct next instruction each time.
./emulator run examples/v0_4/sequential_calls.bin >"$TMP_DIR/sequential_calls.out" 2>"$TMP_DIR/sequential_calls.err"
require_contains "$TMP_DIR/sequential_calls.out" "halted"
require_contains "$TMP_DIR/sequential_calls.out" "x0  = 0x0000000000000003"

# TC-V04-CLI-002 and TC-V04-ACC-003: nested calls with saved X30.
./emulator run examples/v0_4/nested_calls.bin >"$TMP_DIR/nested_calls.out" 2>"$TMP_DIR/nested_calls.err"
require_contains "$TMP_DIR/nested_calls.out" "halted"
require_contains "$TMP_DIR/nested_calls.out" "x0  = 0x0000000000000003"
require_contains "$TMP_DIR/nested_calls.out" "sp  = 0x0000000000100000"

# TC-V04-CLI-003 and TC-V04-ACC-004: stack-frame-style call preserves frame state.
./emulator run examples/v0_4/call_with_stack_frame.bin >"$TMP_DIR/call_with_stack_frame.out" 2>"$TMP_DIR/call_with_stack_frame.err"
require_contains "$TMP_DIR/call_with_stack_frame.out" "halted"
require_contains "$TMP_DIR/call_with_stack_frame.out" "x0  = 0x0000000000000001"
require_contains "$TMP_DIR/call_with_stack_frame.out" "x29 = 0x0000000000001234"
require_contains "$TMP_DIR/call_with_stack_frame.out" "sp  = 0x0000000000100000"

# TC-V04-CLI-001 variants: explicit RET X30 and RET X0 examples.
./emulator run examples/v0_4/ret_x30.bin >"$TMP_DIR/ret_x30.out" 2>"$TMP_DIR/ret_x30.err"
require_contains "$TMP_DIR/ret_x30.out" "halted"
require_contains "$TMP_DIR/ret_x30.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/ret_x30.out" "x1  = 0x0000000000000003"
./emulator run examples/v0_4/ret_custom_register.bin >"$TMP_DIR/ret_custom_register.out" 2>"$TMP_DIR/ret_custom_register.err"
require_contains "$TMP_DIR/ret_custom_register.out" "halted"
require_contains "$TMP_DIR/ret_custom_register.out" "x0  = 0x000000000000100c"
require_contains "$TMP_DIR/ret_custom_register.out" "x2  = 0x0000000000000007"

# TC-V04-CLI-004 and TC-V04-ACC-005: invalid return fails clearly.
require_not_success ./emulator run examples/v0_4/invalid_return.bin
require_contains "$TMP_DIR/stderr.txt" "invalid return target"
require_contains "$TMP_DIR/stderr.txt" "pc  = 0x0000000000001004"

# TC-V04-ERR-008: negative nested-call example hits the instruction limit instead of crashing.
require_not_success ./emulator run examples/v0_4/unsaved_nested_call.bin
require_contains "$TMP_DIR/stderr.txt" "instruction limit"

# TC-V04-TRACE-001: normal trace output shows call jump, return, and resumed caller path.
./emulator trace examples/v0_4/simple_call.bin >"$TMP_DIR/trace_simple_call.out" 2>"$TMP_DIR/trace_simple_call.err"
require_contains "$TMP_DIR/trace_simple_call.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace_simple_call.out" "trace pc=0x000000000000100c"
require_contains "$TMP_DIR/trace_simple_call.out" "trace pc=0x0000000000001010"
require_contains "$TMP_DIR/trace_simple_call.out" "trace pc=0x0000000000001004"
require_contains "$TMP_DIR/trace_simple_call.out" "halted"
require_contains "$TMP_DIR/trace_simple_call.out" "x0  = 0x0000000000000005"

# TC-V04-TRACE-002: nested call trace reaches caller, callee, nested callee, and return points.
./emulator trace examples/v0_4/nested_calls.bin >"$TMP_DIR/trace_nested_calls.out" 2>"$TMP_DIR/trace_nested_calls.err"
require_contains "$TMP_DIR/trace_nested_calls.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace_nested_calls.out" "trace pc=0x000000000000100c"
require_contains "$TMP_DIR/trace_nested_calls.out" "trace pc=0x0000000000001024"
require_contains "$TMP_DIR/trace_nested_calls.out" "trace pc=0x0000000000001018"
require_contains "$TMP_DIR/trace_nested_calls.out" "x0  = 0x0000000000000003"

printf 'v0.4 CLI/function tests passed\n'

#!/bin/sh
set -eu

TMP_DIR="tests/v0_6/tmp"
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
printf '\377\377\377\377' >"$TMP_DIR/unsupported.bin"

# TC-V06-CLI-001 through TC-V06-CLI-005.
require_not_success ./emulator
for text in "emulator run" "emulator trace" "emulator regs" "emulator dump" "emulator debug"; do
    require_contains "$TMP_DIR/stderr.txt" "$text"
done

require_not_success ./emulator nope examples/v0_1/add.bin
require_contains "$TMP_DIR/stderr.txt" "unknown command"
require_contains "$TMP_DIR/stderr.txt" "emulator regs"

require_not_success ./emulator run examples/v0_1/add.bin extra
require_contains "$TMP_DIR/stderr.txt" "emulator run <raw-binary>"
require_not_success ./emulator trace examples/v0_1/add.bin extra
require_contains "$TMP_DIR/stderr.txt" "emulator trace <raw-binary>"
require_not_success ./emulator regs examples/v0_1/add.bin extra
require_contains "$TMP_DIR/stderr.txt" "emulator regs <raw-binary>"

require_not_success ./emulator run
require_contains "$TMP_DIR/stderr.txt" "emulator run <raw-binary>"
require_not_success ./emulator trace
require_contains "$TMP_DIR/stderr.txt" "emulator trace <raw-binary>"
require_not_success ./emulator regs
require_contains "$TMP_DIR/stderr.txt" "emulator regs <raw-binary>"

require_not_success ./emulator run "$TMP_DIR/missing.bin"
require_contains "$TMP_DIR/stderr.txt" "$TMP_DIR/missing.bin"
require_not_contains "$TMP_DIR/stderr.txt" "unsupported instruction"

# TC-V06-EXAMPLES-001 through TC-V06-EXAMPLES-004.
for dir in examples/v0_1 examples/v0_2 examples/v0_3 examples/v0_4 examples/v0_5; do
    if [ ! -d "$dir" ]; then
        echo "FAIL: missing $dir" >&2
        exit 1
    fi
done
if [ ! -f examples/README.md ]; then
    echo "FAIL: missing examples/README.md" >&2
    exit 1
fi
for text in "make examples" "run" "trace" "debug" ".bin"; do
    require_contains examples/README.md "$text"
done

./emulator run examples/v0_1/add.bin >"$TMP_DIR/run_add.out" 2>"$TMP_DIR/run_add.err"
require_contains "$TMP_DIR/run_add.out" "x2  = 0x0000000000000005"
./emulator run examples/v0_2/cbnz_countdown.bin >"$TMP_DIR/run_loop.out" 2>"$TMP_DIR/run_loop.err"
require_contains "$TMP_DIR/run_loop.out" "x0  = 0x0000000000000000"
require_contains "$TMP_DIR/run_loop.out" "x1  = 0x0000000000000005"
./emulator run examples/v0_3/stack_push_pop.bin >"$TMP_DIR/run_stack.out" 2>"$TMP_DIR/run_stack.err"
require_contains "$TMP_DIR/run_stack.out" "sp  = 0x0000000000100000"
./emulator run examples/v0_4/simple_call.bin >"$TMP_DIR/run_call.out" 2>"$TMP_DIR/run_call.err"
require_contains "$TMP_DIR/run_call.out" "x0  = 0x0000000000000005"

./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt >"$TMP_DIR/script_add.out" 2>"$TMP_DIR/script_add.err"
require_contains "$TMP_DIR/script_add.out" "breakpoint hit"
require_contains "$TMP_DIR/script_add.out" "x2  = 0x0000000000000005"
./emulator debug examples/v0_4/simple_call.bin < examples/v0_5/debug_function_script.txt >"$TMP_DIR/script_function.out" 2>"$TMP_DIR/script_function.err"
require_contains "$TMP_DIR/script_function.out" "halted"
./emulator debug examples/v0_3/memory_store_load.bin < examples/v0_5/debug_memory_script.txt >"$TMP_DIR/script_memory.out" 2>"$TMP_DIR/script_memory.err"
require_contains "$TMP_DIR/script_memory.out" "memory dump"

# TC-V06-TRACE-001 through TC-V06-TRACE-006 and acceptance workflows.
./emulator trace examples/v0_1/add.bin >"$TMP_DIR/trace_add.out" 2>"$TMP_DIR/trace_add.err"
require_contains "$TMP_DIR/trace_add.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace_add.out" "0xd2800040"
require_contains "$TMP_DIR/trace_add.out" "movz x0, #0x2"
require_contains "$TMP_DIR/trace_add.out" "add x2, x0, #0x3"
require_contains "$TMP_DIR/trace_add.out" "hlt"
require_contains "$TMP_DIR/trace_add.out" "halted"

./emulator trace examples/v0_2/cbnz_countdown.bin >"$TMP_DIR/trace_loop.out" 2>"$TMP_DIR/trace_loop.err"
require_contains "$TMP_DIR/trace_loop.out" "cbnz x0"
require_contains "$TMP_DIR/trace_loop.out" "x0  = 0x0000000000000000"
require_contains "$TMP_DIR/trace_loop.out" "x1  = 0x0000000000000005"
loop_count=$(grep -Fc "trace pc=0x0000000000001008" "$TMP_DIR/trace_loop.out")
if [ "$loop_count" -lt 2 ]; then
    echo "FAIL: expected loop trace address to repeat" >&2
    cat "$TMP_DIR/trace_loop.out" >&2
    exit 1
fi

./emulator trace examples/v0_3/stack_push_pop.bin >"$TMP_DIR/trace_stack.out" 2>"$TMP_DIR/trace_stack.err"
require_contains "$TMP_DIR/trace_stack.out" "str x0, [sp, #-8]!"
require_contains "$TMP_DIR/trace_stack.out" "ldr x2, [sp], #8"

./emulator trace examples/v0_4/simple_call.bin >"$TMP_DIR/trace_call.out" 2>"$TMP_DIR/trace_call.err"
require_contains "$TMP_DIR/trace_call.out" "bl 0x"
require_contains "$TMP_DIR/trace_call.out" "ret"

printf 'trace on\nstep\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_trace.out" 2>"$TMP_DIR/debug_trace.err"
require_contains "$TMP_DIR/debug_trace.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/debug_trace.out" "0xd2800040"
require_contains "$TMP_DIR/debug_trace.out" "movz x0, #0x2"

printf '\037\040\003\325\377\377\377\377' >"$TMP_DIR/nop_bad.bin"
require_not_success ./emulator trace "$TMP_DIR/nop_bad.bin"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001004"
require_contains "$TMP_DIR/stderr.txt" "opcode=0xffffffff"

# TC-V06-ERR and TC-V06-REGS.
require_not_success ./emulator run "$TMP_DIR/unsupported.bin"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "opcode=0xffffffff"

require_not_success ./emulator run examples/v0_3/invalid_memory_access.bin
require_contains "$TMP_DIR/stderr.txt" "execution error at pc="
require_contains "$TMP_DIR/stderr.txt" "opcode=0x"
require_contains "$TMP_DIR/stderr.txt" "memory access out of bounds"

./emulator regs examples/v0_1/add.bin >"$TMP_DIR/regs_add.out" 2>"$TMP_DIR/regs_add.err"
require_contains "$TMP_DIR/regs_add.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/regs_add.out" "x1  = 0x0000000000000003"
require_contains "$TMP_DIR/regs_add.out" "x2  = 0x0000000000000005"
require_contains "$TMP_DIR/regs_add.out" "pc  = 0x000000000000100c"
require_contains "$TMP_DIR/regs_add.out" "instructions = 0x0000000000000004"
require_not_contains "$TMP_DIR/regs_add.out" "halted"

require_contains "$TMP_DIR/run_add.out" "x2  = 0x0000000000000005"
require_contains "$TMP_DIR/run_add.out" "pc  = 0x000000000000100c"
require_contains "$TMP_DIR/run_add.out" "instructions = 0x0000000000000004"

require_not_success ./emulator regs "$TMP_DIR/unsupported.bin"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc  = 0x0000000000001000"
require_not_success ./emulator regs examples/v0_1/add.bin extra
require_contains "$TMP_DIR/stderr.txt" "emulator regs <raw-binary>"

./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8 >"$TMP_DIR/dump.out" 2>"$TMP_DIR/dump.err"
require_contains "$TMP_DIR/dump.out" "memory dump address=0x00000000000ffff8"

printf 'trace on\nbreak 0x1008\nrun\nstep\nregs\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_accept.out" 2>"$TMP_DIR/debug_accept.err"
require_contains "$TMP_DIR/debug_accept.out" "breakpoint hit at 0x0000000000001008"
require_contains "$TMP_DIR/debug_accept.out" "trace pc=0x0000000000001008"
require_contains "$TMP_DIR/debug_accept.out" "add x2, x0, #0x3"
require_contains "$TMP_DIR/debug_accept.out" "x2  = 0x0000000000000005"

# TC-V06-DOCS.
for text in "run" "trace" "dump" "debug" "regs" "make examples" "opcode"; do
    require_contains README.md "$text"
done
if [ ! -f lessons/v0.6-assembler-friendly-runtime.md ]; then
    echo "FAIL: missing v0.6 lesson" >&2
    exit 1
fi
for text in "readable traces" "opcode" "run" "trace" "debug" "regs" "make examples"; do
    require_contains lessons/v0.6-assembler-friendly-runtime.md "$text"
done
require_contains docs/test-plan-v0.6.md "implemented and tested"
if grep -R "education/" README.md lessons examples --exclude-dir=.git >/dev/null 2>&1; then
    echo "FAIL: stale education/ reference found" >&2
    grep -R "education/" README.md lessons examples --exclude-dir=.git >&2
    exit 1
fi

printf 'v0.6 CLI/runtime tests passed\n'

#!/bin/sh
set -eu

TMP_DIR="tests/v0_5/tmp"
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

run_debug() {
    script="$1"
    binary="$2"
    out="$3"
    err="$4"
    printf '%s' "$script" | ./emulator debug "$binary" >"$out" 2>"$err"
}

make regression-examples >/dev/null
printf '\377\377\377\377' >"$TMP_DIR/unsupported.bin"
long_line=$(printf 'a%.0s' $(seq 1 300))

# TC-V05-CLI-001 and TC-V05-CLI-005: debug starts, prompts, and script mode works.
run_debug 'quit
' examples/v0_1/add.bin "$TMP_DIR/debug_quit.out" "$TMP_DIR/debug_quit.err"
require_contains "$TMP_DIR/debug_quit.out" "tiny-aarch64 debugger v0.5"
require_contains "$TMP_DIR/debug_quit.out" "emu>"
require_not_contains "$TMP_DIR/debug_quit.out" "halted"
run_debug 'run
quit
' examples/v0_1/add.bin "$TMP_DIR/debug_run.out" "$TMP_DIR/debug_run.err"
require_contains "$TMP_DIR/debug_run.out" "halted"

# TC-V05-CLI-002 through TC-V05-CLI-004: CLI argument errors.
require_not_success ./emulator debug "$TMP_DIR/missing.bin"
require_contains "$TMP_DIR/stderr.txt" "failed to open"
require_not_success ./emulator debug
require_contains "$TMP_DIR/stderr.txt" "emulator debug <raw-binary>"
require_not_success ./emulator debug examples/v0_1/add.bin extra
require_contains "$TMP_DIR/stderr.txt" "emulator debug <raw-binary>"

# TC-V05-REPL-001 through TC-V05-REPL-006: parsing, whitespace, aliases, invalid arguments.
run_debug 'help
quit
' examples/v0_1/add.bin "$TMP_DIR/help.out" "$TMP_DIR/help.err"
for cmd in run step continue regs mem break delete breakpoints trace quit; do
    require_contains "$TMP_DIR/help.out" "$cmd"
done
run_debug '

quit
' examples/v0_1/add.bin "$TMP_DIR/empty_lines.out" "$TMP_DIR/empty_lines.err"
require_not_contains "$TMP_DIR/empty_lines.err" "error:"
run_debug '   regs   
   quit   
' examples/v0_1/add.bin "$TMP_DIR/whitespace.out" "$TMP_DIR/whitespace.err"
require_contains "$TMP_DIR/whitespace.out" "pc  = 0x0000000000001000"
run_debug 'wat
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/unknown.out" "$TMP_DIR/unknown.err"
require_contains "$TMP_DIR/unknown.err" "unknown command: wat"
require_contains "$TMP_DIR/unknown.out" "pc  = 0x0000000000001000"
run_debug 'mem
break
trace maybe
delete
regs extra
trace on extra
break 0x1008 extra
mem 0x1000 8 extra
quit
' \
    examples/v0_1/add.bin "$TMP_DIR/invalid_args.out" "$TMP_DIR/invalid_args.err"
require_contains "$TMP_DIR/invalid_args.err" "usage: mem <address> <length>"
require_contains "$TMP_DIR/invalid_args.err" "usage: break <address>"
require_contains "$TMP_DIR/invalid_args.err" "usage: trace on|off"
require_contains "$TMP_DIR/invalid_args.err" "usage: delete <breakpoint-id-or-address>"
require_contains "$TMP_DIR/invalid_args.err" "usage: regs"
run_debug 'b 0x1008
r
s
c
q
' examples/v0_1/add.bin "$TMP_DIR/aliases.out" "$TMP_DIR/aliases.err"
require_contains "$TMP_DIR/aliases.out" "breakpoint hit at 0x0000000000001008"
require_contains "$TMP_DIR/aliases.out" "halted"

# TC-V05-RUN: run/reset/determinism/runtime error recovery.
run_debug 'run
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/run_regs.out" "$TMP_DIR/run_regs.err"
require_contains "$TMP_DIR/run_regs.out" "halted"
require_contains "$TMP_DIR/run_regs.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/run_regs.out" "x1  = 0x0000000000000003"
require_contains "$TMP_DIR/run_regs.out" "x2  = 0x0000000000000005"
require_contains "$TMP_DIR/run_regs.out" "pc  = 0x000000000000100c"
run_debug 'step
step
run
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/run_reset.out" "$TMP_DIR/run_reset.err"
require_contains "$TMP_DIR/run_reset.out" "instructions = 0x0000000000000004"
require_contains "$TMP_DIR/run_reset.out" "x2  = 0x0000000000000005"
run_debug 'run
regs
run
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/repeated_run.out" "$TMP_DIR/repeated_run.err"
count=$(grep -Fc "x2  = 0x0000000000000005" "$TMP_DIR/repeated_run.out")
if [ "$count" -lt 2 ]; then
    echo "FAIL: repeated run did not produce deterministic final state twice" >&2
    cat "$TMP_DIR/repeated_run.out" >&2
    exit 1
fi
run_debug 'run
regs
quit
' "$TMP_DIR/unsupported.bin" "$TMP_DIR/run_error.out" "$TMP_DIR/run_error.err"
require_contains "$TMP_DIR/run_error.err" "unsupported instruction"
require_contains "$TMP_DIR/run_error.out" "pc  = 0x0000000000001000"

# TC-V05-STEP: one-step behavior, HLT, post-HLT error, breakpoint ignored by step.
run_debug 'step
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/step_one.out" "$TMP_DIR/step_one.err"
require_contains "$TMP_DIR/step_one.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/step_one.out" "x1  = 0x0000000000000000"
require_contains "$TMP_DIR/step_one.out" "pc  = 0x0000000000001004"
require_contains "$TMP_DIR/step_one.out" "instructions = 0x0000000000000001"
run_debug 'step
step
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/step_two.out" "$TMP_DIR/step_two.err"
require_contains "$TMP_DIR/step_two.out" "x1  = 0x0000000000000003"
require_contains "$TMP_DIR/step_two.out" "pc  = 0x0000000000001008"
require_contains "$TMP_DIR/step_two.out" "instructions = 0x0000000000000002"
run_debug 'step
step
step
step
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/step_hlt.out" "$TMP_DIR/step_hlt.err"
require_contains "$TMP_DIR/step_hlt.out" "halted"
require_contains "$TMP_DIR/step_hlt.out" "instructions = 0x0000000000000004"
run_debug 'run
step
quit
' examples/v0_1/add.bin "$TMP_DIR/step_after_hlt.out" "$TMP_DIR/step_after_hlt.err"
require_contains "$TMP_DIR/step_after_hlt.err" "already halted"
run_debug 'break 0x1000
step
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/step_breakpoint.out" "$TMP_DIR/step_breakpoint.err"
require_contains "$TMP_DIR/step_breakpoint.out" "pc  = 0x0000000000001004"

# TC-V05-CONT and TC-V05-BP: breakpoint behavior, delete/list/duplicates/errors.
run_debug 'continue
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/continue_full.out" "$TMP_DIR/continue_full.err"
require_contains "$TMP_DIR/continue_full.out" "halted"
require_contains "$TMP_DIR/continue_full.out" "x2  = 0x0000000000000005"
run_debug 'break 0x1008
continue
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/break_before.out" "$TMP_DIR/break_before.err"
require_contains "$TMP_DIR/break_before.out" "breakpoint hit at 0x0000000000001008"
require_contains "$TMP_DIR/break_before.out" "x0  = 0x0000000000000002"
require_contains "$TMP_DIR/break_before.out" "x1  = 0x0000000000000003"
require_contains "$TMP_DIR/break_before.out" "x2  = 0x0000000000000000"
require_contains "$TMP_DIR/break_before.out" "pc  = 0x0000000000001008"
run_debug 'break 0x1008
continue
step
continue
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/break_progress.out" "$TMP_DIR/break_progress.err"
require_contains "$TMP_DIR/break_progress.out" "x2  = 0x0000000000000005"
require_contains "$TMP_DIR/break_progress.out" "halted"
run_debug 'break 0x1008
continue
regs
step
continue
regs
quit
' examples/v0_2/cbnz_countdown.bin "$TMP_DIR/loop_break.out" "$TMP_DIR/loop_break.err"
require_contains "$TMP_DIR/loop_break.out" "breakpoint hit at 0x0000000000001008"
require_contains "$TMP_DIR/loop_break.out" "x0  = 0x0000000000000005"
require_contains "$TMP_DIR/loop_break.out" "x1  = 0x0000000000000000"
run_debug 'continue
quit
' examples/v0_2/infinite_branch.bin "$TMP_DIR/instruction_limit.out" "$TMP_DIR/instruction_limit.err"
require_contains "$TMP_DIR/instruction_limit.err" "instruction limit reached"
run_debug 'break 0x1008
breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/breakpoints.out" "$TMP_DIR/breakpoints.err"
require_contains "$TMP_DIR/breakpoints.out" "1: 0x0000000000001008"
run_debug 'break 0x1002
break 0x100000
break 0x0
breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/break_errors.out" "$TMP_DIR/break_errors.err"
require_contains "$TMP_DIR/break_errors.err" "4-byte aligned"
require_contains "$TMP_DIR/break_errors.err" "outside memory"
require_contains "$TMP_DIR/break_errors.out" "0x0000000000000000"
run_debug 'break 0x1008
break 0x1008
breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/duplicate_break.out" "$TMP_DIR/duplicate_break.err"
require_contains "$TMP_DIR/duplicate_break.err" "already exists"
count=$(grep -Fc "0x0000000000001008" "$TMP_DIR/duplicate_break.out")
if [ "$count" -ne 2 ]; then
    echo "FAIL: duplicate breakpoint output should include one set line and one list line" >&2
    cat "$TMP_DIR/duplicate_break.out" >&2
    exit 1
fi
run_debug 'break 0x1008
breakpoints
delete 0x1008
breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/delete_break.out" "$TMP_DIR/delete_break.err"
require_contains "$TMP_DIR/delete_break.out" "breakpoint deleted"
require_contains "$TMP_DIR/delete_break.out" "no breakpoints"
run_debug 'delete 0x1008
breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/delete_missing.out" "$TMP_DIR/delete_missing.err"
require_contains "$TMP_DIR/delete_missing.err" "not found"
require_contains "$TMP_DIR/delete_missing.out" "no breakpoints"
run_debug 'breakpoints
quit
' examples/v0_1/add.bin "$TMP_DIR/no_breakpoints.out" "$TMP_DIR/no_breakpoints.err"
require_contains "$TMP_DIR/no_breakpoints.out" "no breakpoints"
run_debug 'break 0x1008
run
step
run
quit
' examples/v0_1/add.bin "$TMP_DIR/break_survives_run.out" "$TMP_DIR/break_survives_run.err"
count=$(grep -Fc "breakpoint hit at 0x0000000000001008" "$TMP_DIR/break_survives_run.out")
if [ "$count" -lt 2 ]; then
    echo "FAIL: breakpoint did not survive run reset" >&2
    cat "$TMP_DIR/break_survives_run.out" >&2
    exit 1
fi

# TC-V05-REGS: initial, partial, final, stable output tokens.
run_debug 'regs
quit
' examples/v0_1/add.bin "$TMP_DIR/regs_initial.out" "$TMP_DIR/regs_initial.err"
for reg in x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 x10 x11 x12 x13 x14 x15 x16 x17 x18 x19 x20 x21 x22 x23 x24 x25 x26 x27 x28 x29 x30; do
    require_contains "$TMP_DIR/regs_initial.out" "$reg"
done
require_contains "$TMP_DIR/regs_initial.out" "pc  = 0x0000000000001000"
require_contains "$TMP_DIR/regs_initial.out" "sp  = 0x0000000000100000"
require_contains "$TMP_DIR/regs_initial.out" "nzcv"
require_contains "$TMP_DIR/regs_initial.out" "instructions = 0x0000000000000000"

# TC-V05-MEM: dumps before/after execution, decimal args, zero-length, OOB, invalid syntax.
run_debug 'mem 0x1000 16
quit
' examples/v0_1/add.bin "$TMP_DIR/mem_before.out" "$TMP_DIR/mem_before.err"
require_contains "$TMP_DIR/mem_before.out" "memory dump address=0x0000000000001000 length=0x0000000000000010"
run_debug 'run
mem 0xffff8 8
quit
' examples/v0_3/memory_store_load.bin "$TMP_DIR/mem_after_store.out" "$TMP_DIR/mem_after_store.err"
require_contains "$TMP_DIR/mem_after_store.out" "0x00000000000ffff8: 2a 00 00 00 00 00 00 00"
run_debug 'mem 4096 16
quit
' examples/v0_1/add.bin "$TMP_DIR/mem_decimal.out" "$TMP_DIR/mem_decimal.err"
require_contains "$TMP_DIR/mem_decimal.out" "0x0000000000001000:"
run_debug 'mem 0x1000 0
quit
' examples/v0_1/add.bin "$TMP_DIR/mem_zero.out" "$TMP_DIR/mem_zero.err"
require_contains "$TMP_DIR/mem_zero.out" "memory dump address=0x0000000000001000 length=0x0000000000000000"
run_debug 'mem 0xfffff 8
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/mem_oob.out" "$TMP_DIR/mem_oob.err"
require_contains "$TMP_DIR/mem_oob.err" "out of bounds"
require_contains "$TMP_DIR/mem_oob.out" "pc  = 0x0000000000001000"
run_debug 'mem banana 8
mem 0x1000 nope
quit
' examples/v0_1/add.bin "$TMP_DIR/mem_invalid.out" "$TMP_DIR/mem_invalid.err"
require_contains "$TMP_DIR/mem_invalid.err" "usage: mem <address> <length>"

# TC-V05-TRACE: default/off/on/invalid argument.
run_debug 'step
quit
' examples/v0_1/add.bin "$TMP_DIR/trace_default.out" "$TMP_DIR/trace_default.err"
require_not_contains "$TMP_DIR/trace_default.out" "trace pc="
run_debug 'trace on
run
quit
' examples/v0_1/add.bin "$TMP_DIR/trace_on.out" "$TMP_DIR/trace_on.err"
require_contains "$TMP_DIR/trace_on.out" "trace enabled"
require_contains "$TMP_DIR/trace_on.out" "trace pc=0x0000000000001000"
run_debug 'trace on
step
trace off
step
quit
' examples/v0_1/add.bin "$TMP_DIR/trace_off.out" "$TMP_DIR/trace_off.err"
require_contains "$TMP_DIR/trace_off.out" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/trace_off.out" "trace disabled"
count=$(grep -Fc "trace pc=" "$TMP_DIR/trace_off.out")
if [ "$count" -ne 1 ]; then
    echo "FAIL: trace off should leave exactly one trace line" >&2
    cat "$TMP_DIR/trace_off.out" >&2
    exit 1
fi
run_debug 'trace maybe
quit
' examples/v0_1/add.bin "$TMP_DIR/trace_invalid.out" "$TMP_DIR/trace_invalid.err"
require_contains "$TMP_DIR/trace_invalid.err" "usage: trace on|off"

# TC-V05-ERR: runtime errors, long input, EOF handling.
run_debug 'step
regs
quit
' "$TMP_DIR/unsupported.bin" "$TMP_DIR/err_step.out" "$TMP_DIR/err_step.err"
require_contains "$TMP_DIR/err_step.err" "unsupported instruction"
require_contains "$TMP_DIR/err_step.out" "pc  = 0x0000000000001000"
run_debug 'continue
regs
quit
' examples/v0_4/invalid_return.bin "$TMP_DIR/invalid_return.out" "$TMP_DIR/invalid_return.err"
require_contains "$TMP_DIR/invalid_return.err" "invalid return target"
require_contains "$TMP_DIR/invalid_return.out" "pc  = 0x0000000000001004"
run_debug "$long_line
regs
quit
" examples/v0_1/add.bin "$TMP_DIR/long_line.out" "$TMP_DIR/long_line.err"
require_contains "$TMP_DIR/long_line.err" "input line too long"
require_contains "$TMP_DIR/long_line.out" "pc  = 0x0000000000001000"
./emulator debug examples/v0_1/add.bin </dev/null >"$TMP_DIR/eof.out" 2>"$TMP_DIR/eof.err"
require_contains "$TMP_DIR/eof.out" "quit"
printf 'regs
' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/partial_eof.out" 2>"$TMP_DIR/partial_eof.err"
require_contains "$TMP_DIR/partial_eof.out" "pc  = 0x0000000000001000"
require_contains "$TMP_DIR/partial_eof.out" "quit"

# TC-V05-ACC: full workflows and checked example scripts.
run_debug 'break 0x1008
run
regs
step
regs
continue
regs
quit
' examples/v0_1/add.bin "$TMP_DIR/acc_break.out" "$TMP_DIR/acc_break.err"
require_contains "$TMP_DIR/acc_break.out" "x2  = 0x0000000000000000"
require_contains "$TMP_DIR/acc_break.out" "x2  = 0x0000000000000005"
run_debug 'step
step
regs
step
regs
continue
regs
quit
' examples/v0_4/simple_call.bin "$TMP_DIR/acc_function.out" "$TMP_DIR/acc_function.err"
require_contains "$TMP_DIR/acc_function.out" "x30 = 0x0000000000001008"
require_contains "$TMP_DIR/acc_function.out" "x0  = 0x0000000000000005"
run_debug 'step
step
step
mem 0xffff8 8
continue
regs
quit
' examples/v0_3/stack_push_pop.bin "$TMP_DIR/acc_memory.out" "$TMP_DIR/acc_memory.err"
require_contains "$TMP_DIR/acc_memory.out" "0x00000000000ffff8: 01 00 00 00 00 00 00 00"
require_contains "$TMP_DIR/acc_memory.out" "sp  = 0x0000000000100000"
run_debug 'trace on
continue
regs
quit
' examples/v0_2/trace_loop.bin "$TMP_DIR/acc_trace_loop.out" "$TMP_DIR/acc_trace_loop.err"
require_contains "$TMP_DIR/acc_trace_loop.out" "trace pc=0x0000000000001008"
require_contains "$TMP_DIR/acc_trace_loop.out" "halted"
run_debug 'step
regs
run
quit
' "$TMP_DIR/unsupported.bin" "$TMP_DIR/acc_error.out" "$TMP_DIR/acc_error.err"
require_contains "$TMP_DIR/acc_error.err" "unsupported instruction"
require_contains "$TMP_DIR/acc_error.out" "pc  = 0x0000000000001000"

./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt >"$TMP_DIR/example_add_script.out" 2>"$TMP_DIR/example_add_script.err"
require_contains "$TMP_DIR/example_add_script.out" "x2  = 0x0000000000000005"
./emulator debug examples/v0_4/simple_call.bin < examples/v0_5/debug_function_script.txt >"$TMP_DIR/example_function_script.out" 2>"$TMP_DIR/example_function_script.err"
require_contains "$TMP_DIR/example_function_script.out" "x0  = 0x0000000000000005"
./emulator debug examples/v0_3/stack_push_pop.bin < examples/v0_5/debug_memory_script.txt >"$TMP_DIR/example_memory_script.out" 2>"$TMP_DIR/example_memory_script.err"
require_contains "$TMP_DIR/example_memory_script.out" "memory dump address="

printf 'v0.5 CLI/debugger tests passed\n'

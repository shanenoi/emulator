#!/bin/sh
set -eu

TMP_DIR="tests/v0_7/tmp"
mkdir -p "$TMP_DIR"

require_contains() {
    file="$1"
    needle="$2"
    if ! grep -Fq -- "$needle" "$file"; then
        echo "FAIL: expected $file to contain: $needle" >&2
        echo "--- $file ---" >&2
        cat "$file" >&2
        exit 1
    fi
}

require_not_contains() {
    file="$1"
    needle="$2"
    if grep -Fq -- "$needle" "$file"; then
        echo "FAIL: expected $file not to contain: $needle" >&2
        echo "--- $file ---" >&2
        cat "$file" >&2
        exit 1
    fi
}

require_exact_file() {
    file="$1"
    expected="$2"
    actual=$(cat "$file")
    if [ "$actual" != "$expected" ]; then
        echo "FAIL: expected exact contents in $file" >&2
        printf 'expected: <%s>\nactual:   <%s>\n' "$expected" "$actual" >&2
        exit 1
    fi
}

run_expect_status() {
    expected="$1"
    shift
    set +e
    "$@" >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt"
    status=$?
    set -e
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL: expected status $expected, got $status for: $*" >&2
        echo "--- stdout ---" >&2
        cat "$TMP_DIR/stdout.txt" >&2
        echo "--- stderr ---" >&2
        cat "$TMP_DIR/stderr.txt" >&2
        exit 1
    fi
}

make examples >/dev/null

# TC-V07-CLI-001.
run_expect_status 2 ./emulator
for text in "emulator run" "emulator trace" "emulator regs" "emulator dump" "emulator debug"; do
    require_contains "$TMP_DIR/stderr.txt" "$text"
done

# TC-V07-CLI-002 and TC-V07-ACC-001.
run_expect_status 0 ./emulator run examples/v0_7/hello.bin
require_exact_file "$TMP_DIR/stdout.txt" "hello, v0.7!"
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V07-CLI-003.
run_expect_status 0 ./emulator regs examples/v0_7/hello.bin
require_contains "$TMP_DIR/stdout.txt" "hello, v0.7!"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x0000000000000000"
require_contains "$TMP_DIR/stdout.txt" "x8  = 0x000000000000005d"
require_contains "$TMP_DIR/stdout.txt" "instructions = 0x0000000000000008"

# TC-V07-CLI-004 and TC-V07-TRACE-001/002.
run_expect_status 0 ./emulator trace examples/v0_7/hello.bin
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001010"
require_contains "$TMP_DIR/stdout.txt" "0xd4000001"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"
require_contains "$TMP_DIR/stdout.txt" "hello, v0.7!"
svc_line=$(grep -n "trace pc=0x0000000000001010" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
hello_line=$(grep -n "hello, v0.7!" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
if [ "$svc_line" -ge "$hello_line" ]; then
    echo "FAIL: expected svc trace before guest output" >&2
    cat "$TMP_DIR/stdout.txt" >&2
    exit 1
fi

# TC-V07-CLI-005 and TC-V07-DUMP-001.
run_expect_status 0 ./emulator dump examples/v0_7/hello.bin 0x1020 13
require_contains "$TMP_DIR/stdout.txt" "hello, v0.7!"
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x0000000000001020"
require_contains "$TMP_DIR/stdout.txt" "68 65 6c 6c 6f 2c 20 76 30 2e 37 21 0a"

# TC-V07-CLI-006.
run_expect_status 7 ./emulator run examples/v0_7/exit_status.bin
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V07-WRITE-002.
run_expect_status 0 ./emulator run examples/v0_7/stderr.bin
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" "error, v0.7!"

# TC-V07-ERR-002 through CLI: bad fd returns -EBADF, then example exits with low 8 bits of that value.
run_expect_status 247 ./emulator run examples/v0_7/bad_fd.bin
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V07-ERR-001 and TC-V07-CLI-007: unknown syscalls are recoverable and return -ENOSYS.
python3 - <<'PY' >"$TMP_DIR/unknown_syscall.bin"
import sys
ops = [
    0xd2807ce8,  # movz x8, #999
    0xd4000001,  # svc #0 => x0 = -ENOSYS
    0xd2800ba8,  # movz x8, #93
    0xd4000001,  # svc #0 => exit(low8(x0))
]
for op in ops:
    sys.stdout.buffer.write(op.to_bytes(4, 'little'))
PY
run_expect_status 218 ./emulator regs "$TMP_DIR/unknown_syscall.bin"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0xffffffffffffffda"
require_contains "$TMP_DIR/stdout.txt" "x8  = 0x000000000000005d"
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V07-TRACE-003 and TC-V07-DEBUG-003.
printf 'trace on\nrun\nregs\nquit\n' | ./emulator debug examples/v0_7/hello.bin >"$TMP_DIR/debug_trace.out" 2>"$TMP_DIR/debug_trace.err"
require_contains "$TMP_DIR/debug_trace.out" "trace enabled"
require_contains "$TMP_DIR/debug_trace.out" "svc #0x0"
require_contains "$TMP_DIR/debug_trace.out" "hello, v0.7!"
require_contains "$TMP_DIR/debug_trace.out" "exited status=0"
require_contains "$TMP_DIR/debug_trace.out" "x8  = 0x000000000000005d"

# TC-V07-DEBUG-001 and TC-V07-ACC-003: break before write SVC, inspect, step once, inspect return value.
printf 'break 0x1010\nrun\nregs\nstep\nregs\nquit\n' | ./emulator debug examples/v0_7/hello.bin >"$TMP_DIR/debug_break_svc.out" 2>"$TMP_DIR/debug_break_svc.err"
require_contains "$TMP_DIR/debug_break_svc.out" "breakpoint hit at 0x0000000000001010"
require_contains "$TMP_DIR/debug_break_svc.out" "x0  = 0x0000000000000001"
require_contains "$TMP_DIR/debug_break_svc.out" "x1  = 0x0000000000001020"
require_contains "$TMP_DIR/debug_break_svc.out" "x2  = 0x000000000000000d"
require_contains "$TMP_DIR/debug_break_svc.out" "hello, v0.7!"
require_contains "$TMP_DIR/debug_break_svc.out" "x0  = 0x000000000000000d"

# TC-V07-DEBUG-005: run resets guest-exit state and side effects happen once per run.
printf 'run\nrun\nquit\n' | ./emulator debug examples/v0_7/hello.bin >"$TMP_DIR/debug_run_twice.out" 2>"$TMP_DIR/debug_run_twice.err"
count=$(grep -Fc "hello, v0.7!" "$TMP_DIR/debug_run_twice.out")
if [ "$count" -ne 2 ]; then
    echo "FAIL: expected two hello outputs across two debug runs, got $count" >&2
    cat "$TMP_DIR/debug_run_twice.out" >&2
    exit 1
fi

# TC-V07-TRACE-004 and TC-V07-ACC-004: invalid guest memory produces a clear error with context.
python3 - <<'PY' >"$TMP_DIR/bad_memory.bin"
import sys
ops = [
    0xd2800020,  # movz x0, #1
    0xd2a00201,  # movz x1, #0x100000 (hw=1)
    0xd2800022,  # movz x2, #1
    0xd2800808,  # movz x8, #64
    0xd4000001,  # svc #0
]
for op in ops:
    sys.stdout.buffer.write(op.to_bytes(4, 'little'))
PY
run_expect_status 1 ./emulator trace "$TMP_DIR/bad_memory.bin"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"
require_contains "$TMP_DIR/stderr.txt" "syscall write buffer out of bounds"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001010"
require_contains "$TMP_DIR/stderr.txt" "opcode=0xd4000001"

# TC-V07-DEBUG-004: debugger reports runtime errors around a syscall with context.
printf 'run\nquit\n' | ./emulator debug "$TMP_DIR/bad_memory.bin" >"$TMP_DIR/debug_bad_memory.out" 2>"$TMP_DIR/debug_bad_memory.err"
require_contains "$TMP_DIR/debug_bad_memory.err" "syscall write buffer out of bounds"
require_contains "$TMP_DIR/debug_bad_memory.err" "pc=0x0000000000001010"
require_contains "$TMP_DIR/debug_bad_memory.err" "opcode=0xd4000001"

# TC-V07-REGRESS-002 representative older commands remain compatible.
run_expect_status 0 ./emulator run examples/v0_1/add.bin
require_contains "$TMP_DIR/stdout.txt" "x2  = 0x0000000000000005"
run_expect_status 0 ./emulator trace examples/v0_2/cbnz_countdown.bin
require_contains "$TMP_DIR/stdout.txt" "cbnz x0"
run_expect_status 0 ./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x00000000000ffff8"
printf 'run\nquit\n' | ./emulator debug examples/v0_4/simple_call.bin >"$TMP_DIR/debug_old.out" 2>"$TMP_DIR/debug_old.err"
require_contains "$TMP_DIR/debug_old.out" "halted"

# TC-V07-DOCS.
for text in "write = 64" "exit = 93" "SVC #0" "-EBADF" "-ENOSYS" "low 8 bits"; do
    require_contains README.md "$text"
    require_contains lessons/v0.7-toy-syscalls.md "$text"
done
require_contains README.md "v0.7 Test Plan"
require_contains examples/README.md "svc #0"
require_contains examples/README.md "write = 64"
require_contains examples/README.md "exit = 93"
require_not_contains README.md "v0.7 tests are intentionally not added yet"
require_not_contains README.md "v0.1 through v0.6 CLI checks"

printf 'v0.7 CLI/syscall tests passed\n'

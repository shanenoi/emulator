#!/bin/sh
set -eu

TMP_DIR="tests/v1_0/tmp"
mkdir -p "$TMP_DIR"

if [ "${EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN:-0}" = "1" ]; then
    echo "skipping optional v1.0 real-toolchain release examples: EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN=1"
    exit 0
fi

if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "skipping optional v1.0 real-toolchain release examples: clang or ld.lld is not available"
    exit 0
fi

run_expect_status() {
    expected="$1"
    shift
    set +e
    "$@" >"$TMP_DIR/optional_stdout.txt" 2>"$TMP_DIR/optional_stderr.txt"
    status=$?
    set -e
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL: expected status $expected, got $status for: $*" >&2
        echo "--- stdout ---" >&2
        cat "$TMP_DIR/optional_stdout.txt" >&2
        echo "--- stderr ---" >&2
        cat "$TMP_DIR/optional_stderr.txt" >&2
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

make \
    examples/v0_9/return_42.elf \
    examples/v0_9/fib.elf \
    examples/v0_9/sum_array.elf \
    examples/v0_9/string_len.elf \
    examples/v0_9/hello_c.elf \
    examples/v0_9/nested_calls.elf \
    examples/v0_9/stack_locals.elf \
    examples/v0_9/byte_copy.elf \
    examples/v0_9/static_local.elf \
    examples/v0_9/stderr_c.elf \
    examples/v0_9/bad_fd_c.elf \
    examples/v0_9/unknown_syscall_c.elf \
    examples/v0_9/invalid_write_c.elf >/dev/null

for target in \
    examples/v0_9/return_42.elf \
    examples/v0_9/fib.elf \
    examples/v0_9/sum_array.elf \
    examples/v0_9/string_len.elf \
    examples/v0_9/hello_c.elf \
    examples/v0_9/static_local.elf; do
    if [ ! -f "$target" ]; then
        echo "skipping optional v1.0 real-toolchain release examples: $target was not produced"
        exit 0
    fi
done

run_expect_status 42 ./emulator run examples/v0_9/return_42.elf
run_expect_status 55 ./emulator run examples/v0_9/fib.elf
run_expect_status 15 ./emulator run examples/v0_9/sum_array.elf
run_expect_status 5 ./emulator run examples/v0_9/string_len.elf
run_expect_status 10 ./emulator run examples/v0_9/nested_calls.elf
run_expect_status 31 ./emulator run examples/v0_9/stack_locals.elf
run_expect_status 187 ./emulator run examples/v0_9/byte_copy.elf
run_expect_status 7 ./emulator run examples/v0_9/static_local.elf
run_expect_status 0 ./emulator run examples/v0_9/hello_c.elf
require_exact_file "$TMP_DIR/optional_stdout.txt" "hello from c"
run_expect_status 0 ./emulator run examples/v0_9/stderr_c.elf
require_exact_file "$TMP_DIR/optional_stderr.txt" "error from c"
run_expect_status 0 ./emulator run examples/v0_9/bad_fd_c.elf
run_expect_status 0 ./emulator run examples/v0_9/unknown_syscall_c.elf
run_expect_status 1 ./emulator run examples/v0_9/invalid_write_c.elf
if ! grep -Fq "syscall write buffer out of bounds" "$TMP_DIR/optional_stderr.txt"; then
    echo "FAIL: invalid_write_c did not report invalid guest memory" >&2
    cat "$TMP_DIR/optional_stderr.txt" >&2
    exit 1
fi

if make examples/v0_9/hosted_printf.elf >"$TMP_DIR/hosted_make.out" 2>"$TMP_DIR/hosted_make.err"; then
    echo "FAIL: hosted/libc counterexample should not have a build target that succeeds" >&2
    exit 1
fi

echo "optional v1.0 real-toolchain release tests passed"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_5/tmp/examples
mkdir -p "$TMP"

fail() { echo "test_optional_kernel_examples.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

python3 examples/v1_5/generate_kernel_fixtures.py --output-dir "$TMP" >"$TMP/generate.out"
for name in single_task_exit two_task_yield sleep_then_exit task_fault_then_exit eret_task_fault_then_exit three_task_round_robin kernel_panic console_write sleep_deadlock infinite_task; do
    [ -s "$TMP/$name.bin" ] || fail "missing generated fixture $name.bin"
done

./emulator run "$TMP/single_task_exit.bin" --kernel --kernel-task 0x1008 >"$TMP/single.out" 2>"$TMP/single.err"
[ ! -s "$TMP/single.err" ] || fail "example single stderr was not empty"
[ ! -s "$TMP/single.out" ] || fail "example single stdout was not empty"

./emulator trace "$TMP/two_task_yield.bin" --kernel --kernel-task 0x1008 --kernel-task 0x1014 >"$TMP/two.out" 2>"$TMP/two.err"
[ ! -s "$TMP/two.err" ] || fail "example two stderr was not empty"
contains "$TMP/two.out" "trace kernel-task-switch index=1"

printf '%s\n' "v1.5 optional kernel example tests passed"

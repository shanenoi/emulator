#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_5/tmp
mkdir -p "$TMP"
python3 tests/fixtures/kernel_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_cli_kernel.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq -- "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

./emulator run "$TMP/single_task_exit.bin" --kernel --kernel-task 0x1008 >"$TMP/single.out" 2>"$TMP/single.err"
[ ! -s "$TMP/single.err" ] || fail "single stderr was not empty"
[ ! -s "$TMP/single.out" ] || fail "single stdout was not empty for guest-exit completion"

./emulator trace "$TMP/two_task_yield.bin" --kernel --kernel-task 0x1008 --kernel-task 0x1014 >"$TMP/two.out" 2>"$TMP/two.err"
[ ! -s "$TMP/two.err" ] || fail "two-task stderr was not empty"
contains "$TMP/two.out" "trace kernel-start-tasks count=2"
contains "$TMP/two.out" "trace kernel-task-switch index=0"
contains "$TMP/two.out" "trace kernel-task-switch index=1"
contains "$TMP/two.out" "trace kernel-complete tasks="

./emulator trace "$TMP/sleep_then_exit.bin" --kernel --timer-interrupt 2 --kernel-task 0x1008 --kernel-task 0x1018 >"$TMP/sleep.out" 2>"$TMP/sleep.err"
[ ! -s "$TMP/sleep.err" ] || fail "sleep stderr was not empty"
contains "$TMP/sleep.out" "trace kernel-task-switch index=0"
contains "$TMP/sleep.out" "trace kernel-task-switch index=1"
contains "$TMP/sleep.out" "trace kernel-complete tasks="

./emulator info "$TMP/single_task_exit.bin" --kernel --kernel-boot-info --kernel-task 0x1008 >"$TMP/info.out" 2>"$TMP/info.err"
[ ! -s "$TMP/info.err" ] || fail "info stderr was not empty"
contains "$TMP/info.out" "toy_kernel_enabled: yes"
contains "$TMP/info.out" "toy_kernel_boot_info: yes"
contains "$TMP/info.out" "toy_kernel_task_count: 1"
contains "$TMP/info.out" "task[0]: state=ready"
contains "$TMP/info.out" "name=kernel:boot-info"

./emulator run "$TMP/console_write.bin" --kernel --kernel-task 0x1014 >"$TMP/console.out" 2>"$TMP/console.err"
[ ! -s "$TMP/console.err" ] || fail "console stderr was not empty"
contains "$TMP/console.out" "KERN"

set +e
./emulator run "$TMP/kernel_panic.bin" --kernel >"$TMP/panic.out" 2>"$TMP/panic.err"
status=$?
set -e
[ "$status" -eq 70 ] || fail "kernel panic status was $status, expected 70"
contains "$TMP/panic.err" "toy kernel panic: code=0x0000000000000007"

set +e
./emulator run "$TMP/task_fault_then_exit.bin" --kernel --kernel-task 0x1008 --kernel-task 0x100c >"$TMP/fault.out" 2>"$TMP/fault.err"
status=$?
set -e
[ "$status" -eq 71 ] || fail "task fault status was $status, expected 71"
[ ! -s "$TMP/fault.err" ] || fail "task fault stderr was not empty"

if ./emulator run "$TMP/sleep_deadlock.bin" --kernel --kernel-task 0x1008 >"$TMP/deadlock.out" 2>"$TMP/deadlock.err"; then
    fail "sleep deadlock unexpectedly succeeded"
fi
contains "$TMP/deadlock.err" "deadlock"

if ./emulator run "$TMP/single_task_exit.bin" --kernel-task 0x1009 >"$TMP/bad_task.out" 2>"$TMP/bad_task.err"; then
    fail "misaligned task entry unexpectedly succeeded"
fi
contains "$TMP/bad_task.err" "4-byte aligned"

if ./emulator run "$TMP/single_task_exit.bin" --kernel $(for i in 0 1 2 3 4 5 6 7 8; do printf ' --kernel-task 0x1008'; done) >"$TMP/too_many.out" 2>"$TMP/too_many.err"; then
    fail "too many tasks unexpectedly succeeded"
fi
contains "$TMP/too_many.err" "too many --kernel-task entries"

if ./emulator run "$TMP/infinite_task.bin" --kernel --kernel-task 0x1008 >"$TMP/limit.out" 2>"$TMP/limit.err"; then
    fail "infinite task unexpectedly succeeded"
fi
contains "$TMP/limit.err" "instruction limit reached"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains "$TMP/help.out" "--kernel"
contains "$TMP/help.out" "--kernel-boot-info"
contains "$TMP/help.out" "--kernel-task"
contains "$TMP/help.out" "toy-kernel profile"

printf '%s\n' "v1.5 CLI kernel tests passed"

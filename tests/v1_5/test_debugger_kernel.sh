#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_5/tmp
mkdir -p "$TMP"
python3 tests/fixtures/kernel_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_debugger_kernel.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq -- "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

cat >"$TMP/debug_kernel.in" <<'EOS'
kernel
s
kernel
s
kernel
q
EOS
./emulator debug "$TMP/two_task_yield.bin" --kernel --kernel-task 0x1008 --kernel-task 0x1014 <"$TMP/debug_kernel.in" >"$TMP/debug_kernel.out" 2>"$TMP/debug_kernel.err"
[ ! -s "$TMP/debug_kernel.err" ] || fail "debug kernel stderr was not empty"
contains "$TMP/debug_kernel.out" "toy_kernel_enabled = yes"
contains "$TMP/debug_kernel.out" "tasks_started = no"
contains "$TMP/debug_kernel.out" "task[0] state=ready"
contains "$TMP/debug_kernel.out" "task[1] state=ready"
contains "$TMP/debug_kernel.out" "tasks_started = yes"
contains "$TMP/debug_kernel.out" "current_task = 0"
contains "$TMP/debug_kernel.out" "task[0] state=running"
contains "$TMP/debug_kernel.out" "current_task = 1"
contains "$TMP/debug_kernel.out" "task[0] state=ready"

cat >"$TMP/debug_continue.in" <<'EOS'
trace on
continue
kernel
q
EOS
./emulator debug "$TMP/two_task_yield.bin" --kernel --kernel-task 0x1008 --kernel-task 0x1014 <"$TMP/debug_continue.in" >"$TMP/debug_continue.out" 2>"$TMP/debug_continue.err"
[ ! -s "$TMP/debug_continue.err" ] || fail "debug continue stderr was not empty"
contains "$TMP/debug_continue.out" "trace enabled"
contains "$TMP/debug_continue.out" "trace kernel-start-tasks count=2"
contains "$TMP/debug_continue.out" "trace kernel-task-switch index=1"
contains "$TMP/debug_continue.out" "exited status=0"
contains "$TMP/debug_continue.out" "completed = yes"

cat >"$TMP/debug_bad.in" <<'EOS'
kernel extra
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
q
EOS
./emulator debug "$TMP/single_task_exit.bin" --kernel --kernel-task 0x1008 <"$TMP/debug_bad.in" >"$TMP/debug_bad.out" 2>"$TMP/debug_bad.err"
contains "$TMP/debug_bad.err" "error: usage: kernel"
contains "$TMP/debug_bad.err" "input line too long"

printf '%s\n' "v1.5 debugger kernel tests passed"

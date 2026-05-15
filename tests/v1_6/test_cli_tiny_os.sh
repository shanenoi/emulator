#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TMP="tests/v1_6/tmp"
mkdir -p "$TMP"
python3 tests/fixtures/tiny_os_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_cli_tiny_os failed: $*" >&2; exit 1; }

./emulator help | grep -q -- "BRK #0x160" || fail "help does not list v1.6 service trap"
./emulator help | grep -q -- "TASK_CREATE" || fail "help does not list TASK_CREATE"

./emulator run "$TMP/guest_create_task_exit.bin" --kernel --kernel-boot-info >"$TMP/create.out" 2>"$TMP/create.err" || fail "guest create task did not complete cleanly"

./emulator run "$TMP/mailbox_ping_pong.bin" --kernel --kernel-boot-info >"$TMP/ipc.out" 2>"$TMP/ipc.err"
[ "$(cat "$TMP/ipc.out")" = "OK" ] || fail "mailbox output changed: $(cat "$TMP/ipc.out")"

./emulator trace "$TMP/three_guest_tasks_yield.bin" --kernel --kernel-boot-info >"$TMP/trace.out" 2>"$TMP/trace.err"
grep -q "trace kernel-service create-task id=0" "$TMP/trace.out" || fail "trace lacks create-task id=0"
grep -q "trace kernel-service create-task id=2" "$TMP/trace.out" || fail "trace lacks create-task id=2"
grep -q "trace kernel-task-switch" "$TMP/trace.out" || fail "trace lacks task switches"
grep -q "trace kernel-complete tasks=3 status=0" "$TMP/trace.out" || fail "trace lacks completion summary"

./emulator trace "$TMP/host_guest_mixed.bin" --kernel --kernel-boot-info --kernel-task 0x1024 >"$TMP/mixed.out" 2>"$TMP/mixed.err"
grep -q "trace kernel-service create-task id=1" "$TMP/mixed.out" || fail "mixed host/guest trace lacks guest-created task"
grep -q "trace kernel-complete tasks=2 status=0" "$TMP/mixed.out" || fail "mixed host/guest run did not complete both tasks"

./emulator trace "$TMP/sleep_with_timer.bin" --kernel --kernel-boot-info --timer-interrupt 1 >"$TMP/sleep_timer.out" 2>"$TMP/sleep_timer.err"
grep -q "trace kernel-idle-until-wake" "$TMP/sleep_timer.out" || fail "timer-enabled sleep did not idle until wake"
grep -q "trace kernel-complete tasks=1 status=0" "$TMP/sleep_timer.out" || fail "timer-enabled sleep did not complete"

./emulator info "$TMP/mailbox_ping_pong.bin" --kernel --kernel-boot-info >"$TMP/info.out"
grep -q "toy_kernel_service_trap: 0x160" "$TMP/info.out" || fail "info lacks service trap"
grep -q "toy_kernel_supported_services:" "$TMP/info.out" || fail "info lacks service mask"
grep -q "toy_kernel_mailbox:" "$TMP/info.out" || fail "info lacks mailbox shape"
grep -q "toy_kernel_task_count: 0" "$TMP/info.out" || fail "info should report static metadata without executing guest-created tasks"

./emulator run "$TMP/invalid_task_create_flags.bin" --kernel --kernel-boot-info >"$TMP/badflags.out" 2>"$TMP/badflags.err" || fail "bad flags fixture should observe service error and halt"

./emulator run "$TMP/unknown_service_then_exit.bin" --kernel --kernel-boot-info >"$TMP/unknown.out" 2>"$TMP/unknown.err" || fail "unknown service should be recoverable"

set +e
./emulator run "$TMP/panic_service.bin" --kernel --kernel-boot-info >"$TMP/panic.out" 2>"$TMP/panic.err"
rc=$?
set -e
[ "$rc" -eq 70 ] || fail "panic exit status was $rc, expected 70"
grep -q "toy kernel panic" "$TMP/panic.err" || fail "panic error missing"

set +e
./emulator run "$TMP/task_fault_then_exit.bin" --kernel --kernel-boot-info >"$TMP/fault.out" 2>"$TMP/fault.err"
rc=$?
set -e
[ "$rc" -eq 71 ] || fail "task fault exit status was $rc, expected 71"

set +e
./emulator run "$TMP/sleep_deadlock.bin" --kernel --kernel-boot-info >"$TMP/deadlock.out" 2>"$TMP/deadlock.err"
rc=$?
set -e
[ "$rc" -ne 0 ] || fail "sleep deadlock should fail"
grep -q "deadlock" "$TMP/deadlock.err" || fail "deadlock diagnostic missing"

printf '%s\n' "tests/v1_6/test_cli_tiny_os.sh: passed"

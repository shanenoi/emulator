#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fail() { echo "test_docs_tiny_os failed: $*" >&2; exit 1; }

for f in docs/test-plan-v1.6.md lessons/v1.6-tiny-os-lab.md examples/v1_6/README.md README.md; do
  [ -f "$f" ] || fail "missing $f"
done

grep -q "v1.6 Test Plan" README.md || fail "README missing v1.6 test plan link"
grep -q "v1.6 Lesson" README.md || fail "README missing v1.6 lesson link"
grep -q "BRK #0x160" README.md || fail "README missing service trap"
grep -q "supported_services" README.md || fail "README missing supported services"
grep -q "TASK_CREATE" README.md || fail "README missing TASK_CREATE"
grep -q "WOULD_BLOCK" README.md || fail "README missing nonblocking mailbox status"
grep -q "not a real OS" README.md lessons/v1.6-tiny-os-lab.md || fail "docs must avoid real OS overclaim"

grep -q "Learning ladder" lessons/v1.6-tiny-os-lab.md || fail "lesson missing learning ladder"
grep -q "Common mistakes" lessons/v1.6-tiny-os-lab.md || fail "lesson missing common mistakes"
grep -q "Exercises" lessons/v1.6-tiny-os-lab.md || fail "lesson missing exercises"

for svc in TASK_CREATE TASK_YIELD TASK_EXIT TASK_SLEEP TASK_GET_ID TASK_GET_INFO TASK_SEND TASK_RECV CONSOLE_WRITE KERNEL_PANIC; do
  grep -q "$svc" README.md || fail "README missing $svc"
  grep -q "$svc" lessons/v1.6-tiny-os-lab.md || fail "lesson missing $svc"
done

for id in TC-V16-BOOT-001 TC-V16-TASK-001 TC-V16-VAL-014 TC-V16-SCHED-012 TC-V16-SVC-001 TC-V16-IPC-005 TC-V16-CLI-009 TC-V16-DBG-003 TC-V16-DOC-008 TC-V16-SAN-001; do
  grep -q "$id" docs/test-plan-v1.6.md || fail "test plan missing $id"
done

./emulator help > tests/v1_6/tmp/help.out
grep -q "BRK #0x160" tests/v1_6/tmp/help.out || fail "help missing BRK #0x160"
grep -q "TASK_SEND" tests/v1_6/tmp/help.out || fail "help missing TASK_SEND"

printf '%s\n' "tests/v1_6/test_docs_tiny_os.sh: passed"

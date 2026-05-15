#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TMP="tests/v1_6/tmp"
mkdir -p "$TMP"
python3 tests/fixtures/tiny_os_fixture_writer.py --output-dir "$TMP"
fail() { echo "test_debugger_tiny_os failed: $*" >&2; exit 1; }

cat >"$TMP/dbg.cmd" <<'CMDS'
kernel
tasks
step
kernel
continue
kernel
quit
CMDS
./emulator debug "$TMP/mailbox_ping_pong.bin" --kernel --kernel-boot-info <"$TMP/dbg.cmd" >"$TMP/dbg.out" 2>"$TMP/dbg.err"
grep -q "service_trap = 0x160" "$TMP/dbg.out" || fail "kernel output lacks service trap"
grep -q "supported_services =" "$TMP/dbg.out" || fail "kernel output lacks supported services"
grep -q "mailbox_ops =" "$TMP/dbg.out" || fail "kernel output lacks mailbox counters"
grep -q "task\[0\].*id=0x0000000000000000" "$TMP/dbg.out" || fail "tasks output lacks task id 0 after continue"
grep -q "task\[1\].*id=0x0000000000000001" "$TMP/dbg.out" || fail "tasks output lacks task id 1 after continue"
grep -q "mailbox=" "$TMP/dbg.out" || fail "task output lacks mailbox occupancy"

cat >"$TMP/dbg_bad.cmd" <<'CMDS'
tasks nope
this-command-does-not-exist
quit
CMDS
./emulator debug "$TMP/guest_create_task_exit.bin" --kernel --kernel-boot-info <"$TMP/dbg_bad.cmd" >"$TMP/dbg_bad.out" 2>"$TMP/dbg_bad.err"
cat "$TMP/dbg_bad.out" "$TMP/dbg_bad.err" >"$TMP/dbg_bad.all"
grep -Eq "unknown command|usage|error" "$TMP/dbg_bad.all" || fail "malformed debugger command was not reported"

printf '%s\n' "tests/v1_6/test_debugger_tiny_os.sh: passed"

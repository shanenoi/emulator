#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
TMP="tests/v1_6/tmp/examples"
mkdir -p "$TMP"
fail() { echo "test_optional_tiny_os_examples failed: $*" >&2; exit 1; }

python3 examples/v1_6/generate_tiny_os_fixtures.py --output-dir "$TMP"
for f in guest_create_task_exit.bin guest_get_id.bin mailbox_ping_pong.bin invalid_task_create_flags.bin; do
  [ -s "$TMP/$f" ] || fail "generator did not create $f"
done
sha1_before=$(sha256sum "$TMP"/*.bin | sort)
python3 examples/v1_6/generate_tiny_os_fixtures.py --output-dir "$TMP"
sha1_after=$(sha256sum "$TMP"/*.bin | sort)
[ "$sha1_before" = "$sha1_after" ] || fail "example generator is not deterministic"

./emulator run "$TMP/guest_create_task_exit.bin" --kernel --kernel-boot-info >"$TMP/create.out" 2>"$TMP/create.err" || fail "guest_create_task_exit example failed"
./emulator run "$TMP/guest_get_id.bin" --kernel --kernel-boot-info >"$TMP/getid.out" 2>"$TMP/getid.err" || fail "guest_get_id example failed"
./emulator run "$TMP/mailbox_ping_pong.bin" --kernel --kernel-boot-info >"$TMP/ipc.out" 2>"$TMP/ipc.err"
[ "$(cat "$TMP/ipc.out")" = "OK" ] || fail "mailbox example output changed"

printf '%s\n' "tests/v1_6/test_optional_tiny_os_examples.sh: passed"

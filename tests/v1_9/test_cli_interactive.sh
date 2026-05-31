#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

TMP_DIR="tests/v1_9/tmp"
mkdir -p "$TMP_DIR"
PROGRAM="$TMP_DIR/hlt.bin"
python3 - <<'PY' > "$PROGRAM"
import struct, sys
sys.stdout.buffer.write(struct.pack('<I', 0xd4400000))
PY

require_fail_contains() {
    local name="$1"
    local expected="$2"
    shift 2
    local out="$TMP_DIR/${name}.out"
    local status=0
    "$@" >"$out" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        echo "expected failure for $name" >&2
        cat "$out" >&2
        exit 1
    fi
    if ! grep -Fq -- "$expected" "$out"; then
        echo "expected '$expected' in $name output" >&2
        cat "$out" >&2
        exit 1
    fi
}

require_fail_contains invalid_fps "invalid --fps value" ./emulator run "$PROGRAM" --interactive --fps 0
require_fail_contains invalid_ipf "invalid --instructions-per-frame value" ./emulator run "$PROGRAM" --interactive --instructions-per-frame 0
require_fail_contains invalid_quit "invalid --quit-key value" ./emulator run "$PROGRAM" --interactive --quit-key q
require_fail_contains non_tty "--interactive requires stdin and stdout to be TTYs" ./emulator run "$PROGRAM" --interactive
require_fail_contains non_run "--interactive is only supported with the run command" ./emulator regs "$PROGRAM" --interactive

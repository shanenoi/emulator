#!/bin/sh
set -eu

TMP_DIR="tests/v1_2/tmp"
mkdir -p "$TMP_DIR"
python3 tests/fixtures/vm_fixture_writer.py --output-dir "$TMP_DIR"

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

run_debug() {
    script="$1"
    program="$2"
    printf '%s\n' "$script" | ./emulator debug "$program" >"$TMP_DIR/debug.out" 2>"$TMP_DIR/debug.err"
}

# TC-V12-DBG-001/002/007.
run_debug 'maps
map 0x1000
map 0xef000
map 0x2000
map -1
map 0x1000 extra
quit' "$TMP_DIR/simple_raw.bin"
require_contains "$TMP_DIR/debug.out" "mappings:"
require_contains "$TMP_DIR/debug.out" "raw:program"
require_contains "$TMP_DIR/debug.out" "address 0x0000000000001000 is in r-x mapping"
require_contains "$TMP_DIR/debug.out" "stack-guard"
require_contains "$TMP_DIR/debug.out" "address 0x0000000000002000 is unmapped"
require_contains "$TMP_DIR/debug.err" "usage: map <address>"

# TC-V12-DBG-003/004.
run_debug 'step
x 0x1000 4
x 0x2000 4
quit' "$TMP_DIR/execute_unmapped.bin"
require_contains "$TMP_DIR/debug.out" "pc=0x0000000000002000"
require_contains "$TMP_DIR/debug.out" "memory dump address=0x0000000000001000"
require_contains "$TMP_DIR/debug.err" "unmapped access"

# TC-V12-DBG-005/006: breakpoints must not be silently accepted in unmapped or non-executable memory.
run_debug 'break 0x2000
break 0xf0000
break 0x1000
breakpoints
run
quit' "$TMP_DIR/simple_raw.bin"
require_contains "$TMP_DIR/debug.err" "breakpoint address is not executable"
require_contains "$TMP_DIR/debug.out" "breakpoint 1 set at 0x0000000000001000"
require_contains "$TMP_DIR/debug.out" "breakpoint hit at 0x0000000000001000"
require_not_contains "$TMP_DIR/debug.out" "breakpoint 2 set"

# TC-V12-DBG-008: reset/run keeps mappings available.
run_debug 'maps
run
maps
quit' "$TMP_DIR/simple_raw.bin"
count=$(grep -c "mappings:" "$TMP_DIR/debug.out")
if [ "$count" -lt 2 ]; then
    echo "FAIL: expected mappings before and after run" >&2
    cat "$TMP_DIR/debug.out" >&2
    exit 1
fi

printf '%s\n' "v1.2 debugger virtual-memory tests passed"

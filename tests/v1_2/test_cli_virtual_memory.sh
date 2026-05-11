#!/bin/sh
set -eu

TMP_DIR="tests/v1_2/tmp"
mkdir -p "$TMP_DIR"
python3 tests/fixtures/vm_fixture_writer.py --output-dir "$TMP_DIR"
python3 tests/fixtures/macho_fixture_writer.py --output-dir "$TMP_DIR" >/dev/null

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

run_capture() {
    expected="$1"
    shift
    set +e
    "$@" >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt"
    status=$?
    set -e
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL: expected status $expected, got $status for: $*" >&2
        echo "--- stdout ---" >&2
        cat "$TMP_DIR/stdout.txt" >&2
        echo "--- stderr ---" >&2
        cat "$TMP_DIR/stderr.txt" >&2
        exit 1
    fi
}

# TC-V12-CLI-001 and TC-V12-REL-001.
run_capture 0 ./emulator info "$TMP_DIR/simple_raw.bin"
require_contains "$TMP_DIR/stdout.txt" "format: raw"
require_contains "$TMP_DIR/stdout.txt" "mappings:"
require_contains "$TMP_DIR/stdout.txt" "r-x"
require_contains "$TMP_DIR/stdout.txt" "raw:program"
require_contains "$TMP_DIR/stdout.txt" "rw-"
require_contains "$TMP_DIR/stdout.txt" "stack"
require_contains "$TMP_DIR/stdout.txt" "guard ---"

# TC-V12-CLI-002 and TC-V12-CLI-003.
run_capture 0 ./emulator info "$TMP_DIR/text_data.elf"
require_contains "$TMP_DIR/stdout.txt" "format: elf64"
require_contains "$TMP_DIR/stdout.txt" "elf:PT_LOAD"
require_contains "$TMP_DIR/stdout.txt" "r-x"
require_contains "$TMP_DIR/stdout.txt" "rw-"
run_capture 0 ./emulator info "$TMP_DIR/stdout_data.macho"
require_contains "$TMP_DIR/stdout.txt" "format: macho64"
require_contains "$TMP_DIR/stdout.txt" "mach_o_load_commands"
require_contains "$TMP_DIR/stdout.txt" "macho:__TEXT"
require_contains "$TMP_DIR/stdout.txt" "macho:__DATA"

# TC-V12-CLI-004/005/006/007/008/REL-004.
run_capture 1 ./emulator run "$TMP_DIR/write_code_page.bin"
require_contains "$TMP_DIR/stderr.txt" "write permission denied"
require_contains "$TMP_DIR/stderr.txt" "pc  ="
run_capture 1 ./emulator run "$TMP_DIR/execute_stack.bin"
require_contains "$TMP_DIR/stderr.txt" "execute permission denied"
run_capture 1 ./emulator run "$TMP_DIR/read_unmapped.bin"
require_contains "$TMP_DIR/stderr.txt" "unmapped access"
run_capture 1 ./emulator trace "$TMP_DIR/execute_unmapped.bin"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "unmapped access"
run_capture 1 ./emulator regs "$TMP_DIR/write_code_page.bin"
require_contains "$TMP_DIR/stderr.txt" "write permission denied"
require_contains "$TMP_DIR/stderr.txt" "x0  ="

# TC-V12-CLI-009 and TC-V12-CLI-010.
run_capture 0 ./emulator dump "$TMP_DIR/simple_raw.bin" 0x1000 12
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x0000000000001000"
run_capture 1 ./emulator dump "$TMP_DIR/simple_raw.bin" 0x2000 4
require_contains "$TMP_DIR/stderr.txt" "dump range is not readable"
require_contains "$TMP_DIR/stderr.txt" "unmapped access"
run_capture 2 ./emulator dump "/simple_raw.bin" -1 4
require_contains "$TMP_DIR/stderr.txt" "invalid dump address or length"

# TC-V12-CLI-011/012 and TC-V12-CPU-008.
run_capture 0 ./emulator help
require_contains "$TMP_DIR/stdout.txt" "emulator info"
require_contains "$TMP_DIR/stdout.txt" "emulator debug"
run_capture 7 ./emulator run "$TMP_DIR/exit7_raw.bin"
require_not_contains "$TMP_DIR/stderr.txt" "error:"

# TC-V12-EDGE-012: raw mapping is exact; page padding remains unmapped.
run_capture 1 ./emulator run "$TMP_DIR/execute_unmapped.bin"
require_contains "$TMP_DIR/stderr.txt" "unmapped access"

printf '%s\n' "v1.2 CLI virtual-memory tests passed"

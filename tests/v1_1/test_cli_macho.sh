#!/bin/sh
set -eu

TMP_DIR="tests/v1_1/tmp"
mkdir -p "$TMP_DIR"
python3 tests/fixtures/macho_fixture_writer.py --output-dir "$TMP_DIR"
python3 examples/v1_1/generate_macho_fixtures.py --output-dir examples/v1_1

fail() {
    echo "v1.1 Mach-O CLI test failed: $*" >&2
    exit 1
}

require_contains() {
    file="$1"
    needle="$2"
    grep -Fq -- "$needle" "$file" || {
        echo "FAIL: expected $file to contain: $needle" >&2
        echo "--- $file ---" >&2
        cat "$file" >&2
        exit 1
    }
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

require_exact_file() {
    file="$1"
    expected="$2"
    actual=$(cat "$file")
    if [ "$actual" != "$expected" ]; then
        echo "FAIL: expected exact contents in $file" >&2
        printf 'expected: <%s>\nactual:   <%s>\n' "$expected" "$actual" >&2
        exit 1
    fi
}

run_expect_status() {
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

# TC-V11-BUILD-004: deterministic generated fixtures.
python3 tests/fixtures/macho_fixture_writer.py --output-dir "$TMP_DIR/first"
python3 tests/fixtures/macho_fixture_writer.py --output-dir "$TMP_DIR/second"
for file in "$TMP_DIR"/first/*.macho "$TMP_DIR"/first/*.bin; do
    base=$(basename "$file")
    cmp "$file" "$TMP_DIR/second/$base" || fail "fixture generation is not deterministic for $base"
done

# TC-V11-DETECT-001/002: existing raw and ELF behavior remain reachable.
run_expect_status 0 ./emulator regs examples/v0_1/add.bin
require_contains "$TMP_DIR/stdout.txt" "pc  = 0x000000000000100c"
run_expect_status 8 ./emulator run examples/v0_8/exit_status_elf.elf

# TC-V11-CLI-001/EXEC-001/002.
run_expect_status 0 ./emulator run "$TMP_DIR/valid_exit0.macho"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""
run_expect_status 42 ./emulator run "$TMP_DIR/valid_exit42.macho"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V11-EXEC-003/004 and segment-backed data.
run_expect_status 0 ./emulator run "$TMP_DIR/stdout_data.macho"
require_exact_file "$TMP_DIR/stdout.txt" "hello, v1.1!"
require_exact_file "$TMP_DIR/stderr.txt" ""
run_expect_status 0 ./emulator run "$TMP_DIR/stderr_data.macho"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" "ERRv1.1"

# TC-V11-INSP-001/002 and CLI info command.
run_expect_status 0 ./emulator info "$TMP_DIR/stdout_data.macho"
require_contains "$TMP_DIR/stdout.txt" "format: macho64"
require_contains "$TMP_DIR/stdout.txt" "entry: 0x0000000000001000"
require_contains "$TMP_DIR/stdout.txt" "name=__TEXT"
require_contains "$TMP_DIR/stdout.txt" "name=__DATA"
require_contains "$TMP_DIR/stdout.txt" "mach_o_load_commands: 4"
require_contains "$TMP_DIR/stdout.txt" "mach_o_symbols: 1"

# TC-V11-CLI-002/003/004: trace, regs, dump on Mach-O.
run_expect_status 0 ./emulator trace "$TMP_DIR/stdout_data.macho"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"
require_contains "$TMP_DIR/stdout.txt" "hello, v1.1!"
run_expect_status 0 ./emulator regs "$TMP_DIR/entry_offset.macho"
require_contains "$TMP_DIR/stdout.txt" "pc  = 0x0000000000001014"
run_expect_status 0 ./emulator dump "$TMP_DIR/zero_fill_data.macho" 0x3000 12
require_contains "$TMP_DIR/stdout.txt" "44 41 54 41 00 00 00 00 00 00 00 00"

# TC-V11-CLI-005/006: debugger starts at Mach-O entry and breakpoints work.
printf 'regs\nstep\nregs\nbreak 0x1008\ncontinue\nquit\n' \
    | ./emulator debug "$TMP_DIR/entry_offset.macho" >"$TMP_DIR/debug.out" 2>"$TMP_DIR/debug.err"
require_contains "$TMP_DIR/debug.out" "pc  = 0x0000000000001008"
require_contains "$TMP_DIR/debug.out" "pc  = 0x000000000000100c"
require_contains "$TMP_DIR/debug.out" "breakpoint 1 set at 0x0000000000001008"
require_exact_file "$TMP_DIR/debug.err" ""

printf 'break 0x1000\nrun\nquit\n' \
    | ./emulator debug "$TMP_DIR/valid_exit0.macho" >"$TMP_DIR/debug_break.out" 2>"$TMP_DIR/debug_break.err"
require_contains "$TMP_DIR/debug_break.out" "breakpoint hit at 0x0000000000001000"
require_exact_file "$TMP_DIR/debug_break.err" ""

# TC-V11-CLI-007/ERR-001/002/003: invalid Mach-O categories are consistent across modes.
for mode in run trace regs info debug; do
    if [ "$mode" = debug ]; then
        printf 'quit\n' | ./emulator debug "$TMP_DIR/dylib.macho" >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt" && fail "debug accepted invalid dylib Mach-O"
    else
        run_expect_status 1 ./emulator "$mode" "$TMP_DIR/dylib.macho"
    fi
    require_contains "$TMP_DIR/stderr.txt" "shared libraries are unsupported"
done

invalid_cases='big_endian.macho big-endian
macho32.macho 32-bit
fat.macho fat/universal
wrong_cpu.macho unsupported CPU type
wrong_filetype.macho unsupported file type
no_main.macho LC_MAIN is required
duplicate_main.macho duplicate LC_MAIN
misaligned_entry.macho 4-byte aligned
entry_outside.macho entryoff is not inside
filesize_gt_vmsize.macho filesize exceeds vmsize
file_range_eof.macho file range outside file
mem_range_oob.macho memory range outside
overlap_1byte.macho overlapping
section_overflow.macho section table is truncated
cmd_too_small.macho invalid load command
cmd_unaligned.macho alignment
dylinker.macho dyld is unsupported
dylib.macho shared libraries are unsupported
dyld_info.macho rebasing and binding metadata is unsupported
dyld_info_only.macho rebasing and binding metadata is unsupported
relocations.macho relocations are unsupported'
printf '%s\n' "$invalid_cases" | while IFS=' ' read -r file needle_rest; do
    [ -n "$file" ] || continue
    run_expect_status 1 ./emulator run "$TMP_DIR/$file"
    require_contains "$TMP_DIR/stderr.txt" "$needle_rest"
done

# TC-V11-EXEC-006 and ERR-004.
run_expect_status 1 ./emulator run "$TMP_DIR/loop.macho"
require_contains "$TMP_DIR/stderr.txt" "instruction limit reached"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
run_expect_status 1 ./emulator run "$TMP_DIR/unsupported_instruction.macho"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "opcode=0xffffffff"

# TC-V11-EDGE-004: empty file and directory input are clear failures.
run_expect_status 1 ./emulator run "$TMP_DIR/empty.bin"
require_contains "$TMP_DIR/stderr.txt" "input file is empty"
run_expect_status 1 ./emulator run "$TMP_DIR"
require_contains "$TMP_DIR/stderr.txt" "failed to"

# TC-V11-DETECT-005: 1-3 byte inputs use the documented raw fallback policy.
for file in partial1.bin partial2.bin partial3.bin; do
    run_expect_status 1 ./emulator run "$TMP_DIR/$file"
    require_not_contains "$TMP_DIR/stderr.txt" "Mach-O loader error"
done

# TC-V11-DOC-003: help advertises only the supported profile.
run_expect_status 0 ./emulator help
require_contains "$TMP_DIR/stdout.txt" "Mach-O MH_EXECUTE"
require_contains "$TMP_DIR/stdout.txt" "supported little-endian arm64"

# Example fixtures remain usable.
run_expect_status 0 ./emulator run examples/v1_1/hello.macho
require_exact_file "$TMP_DIR/stdout.txt" "hello, v1.1!"
run_expect_status 7 ./emulator run examples/v1_1/minimal_exit.macho
run_expect_status 0 ./emulator info examples/v1_1/zero_fill.macho
require_contains "$TMP_DIR/stdout.txt" "mem_size=0x0000000000000040"

echo "v1.1 Mach-O CLI tests passed"

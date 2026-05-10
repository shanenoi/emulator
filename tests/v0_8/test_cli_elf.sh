#!/bin/sh
set -eu

TMP_DIR="tests/v0_8/tmp"
mkdir -p "$TMP_DIR"

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

python3 - <<'PY'
from pathlib import Path
import struct

TMP = Path('tests/v0_8/tmp')
TMP.mkdir(parents=True, exist_ok=True)

ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56
ET_EXEC = 2
ET_DYN = 3
EM_AARCH64 = 183
PT_LOAD = 1
PT_INTERP = 3
PT_NOTE = 4
PF_X = 1
PF_W = 2
PF_R = 4

def movz(rd, imm, shift=0, is64=True):
    return (0x80000000 if is64 else 0) | 0x52800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)

def add(rd, rn, imm, is64=True):
    return (0x91000000 if is64 else 0x11000000) | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rd & 31)

def b(offset):
    return 0x14000000 | ((offset // 4) & 0x03ffffff)

def code(*ops):
    return b''.join(struct.pack('<I', op) for op in ops)

SVC0 = 0xd4000001
SVC1 = 0xd4000021
HLT = 0xd4400000

def elf(path, entry, segments, e_type=ET_EXEC, e_machine=EM_AARCH64, extra_ph=None):
    phdrs = []
    if extra_ph:
        phdrs.extend(extra_ph)
    phdrs.extend(segments)
    phnum = len(phdrs)
    size = max([ELF64_EHDR_SIZE + phnum * ELF64_PHDR_SIZE] + [seg.get('offset', 0) + len(seg.get('data', b'')) for seg in phdrs])
    buf = bytearray(size)
    buf[0:4] = b'\x7fELF'
    buf[4] = 2
    buf[5] = 1
    buf[6] = 1
    struct.pack_into('<HHIQQQ', buf, 16, e_type, e_machine, 1, entry, ELF64_EHDR_SIZE, 0)
    struct.pack_into('<IHHHHHH', buf, 48, 0, ELF64_EHDR_SIZE, ELF64_PHDR_SIZE, phnum, 0, 0, 0)
    for i, seg in enumerate(phdrs):
        off = ELF64_EHDR_SIZE + i * ELF64_PHDR_SIZE
        data = seg.get('data', b'')
        filesz = seg.get('filesz', len(data))
        memsz = seg.get('memsz', filesz)
        struct.pack_into('<IIQQQQQQ', buf, off, seg.get('type', PT_LOAD), seg.get('flags', PF_R | PF_X),
                         seg.get('offset', 0), seg.get('vaddr', 0), seg.get('vaddr', 0), filesz, memsz,
                         seg.get('align', 1))
        if data and seg.get('offset', 0) + len(data) <= len(buf):
            buf[seg.get('offset', 0):seg.get('offset', 0) + len(data)] = data
    path.write_bytes(buf)

hello = b'hello from elf\n'
hello_text = code(
    movz(0, 1), movz(1, 0x2000), movz(2, len(hello)), movz(8, 64), SVC0,
    movz(0, 0), movz(8, 93), SVC0,
)
elf(TMP / 'hello.elf', 0x1000, [
    {'offset': 0x100, 'vaddr': 0x1000, 'data': hello_text, 'flags': PF_R | PF_X},
    {'offset': 0x200, 'vaddr': 0x2000, 'data': hello, 'flags': PF_R | PF_W},
])

stderr_text = code(movz(0, 2), movz(1, 0x2000), movz(2, 3), movz(8, 64), SVC0, HLT)
elf(TMP / 'stderr.elf', 0x1000, [
    {'offset': 0x100, 'vaddr': 0x1000, 'data': stderr_text, 'flags': PF_R | PF_X},
    {'offset': 0x200, 'vaddr': 0x2000, 'data': b'ERR', 'flags': PF_R | PF_W},
])

elf(TMP / 'exit9.elf', 0x1000, [{'offset': 0x100, 'vaddr': 0x1000, 'data': code(movz(0, 9), movz(8, 93), SVC0)}])
elf(TMP / 'mov_hlt.elf', 0x4000, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(movz(0, 123), HLT)}])
elf(TMP / 'entry_offset.elf', 0x4010, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(movz(0, 1), HLT, HLT, HLT, movz(0, 77), HLT)}])
elf(TMP / 'data_bss.elf', 0x1000, [
    {'offset': 0x100, 'vaddr': 0x1000, 'data': code(HLT), 'flags': PF_R | PF_X},
    {'offset': 0x200, 'vaddr': 0x2000, 'data': b'ABCD', 'filesz': 4, 'memsz': 12, 'flags': PF_R | PF_W},
])
elf(TMP / 'debug.elf', 0x4000, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(movz(0, 1), add(0, 0, 2), HLT)}])
elf(TMP / 'loop.elf', 0x4000, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(b(0))}])
elf(TMP / 'bad_svc.elf', 0x4000, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(SVC1)}])
elf(TMP / 'unknown_syscall.elf', 0x4000, [{'offset': 0x100, 'vaddr': 0x4000, 'data': code(movz(8, 999), SVC0, HLT)}])
elf(TMP / 'dynamic.elf', 0x1000, [{'offset': 0x100, 'vaddr': 0x1000, 'data': code(HLT)}], e_type=ET_DYN)
elf(TMP / 'interp.elf', 0x1000, [{'offset': 0x100, 'vaddr': 0x1000, 'data': code(HLT)}],
    extra_ph=[{'type': PT_INTERP, 'offset': 0x300, 'vaddr': 0, 'data': b'/lib/ld-linux-aarch64.so.1\0', 'flags': PF_R}])
elf(TMP / 'wrong_machine.elf', 0x1000, [{'offset': 0x100, 'vaddr': 0x1000, 'data': code(HLT)}], e_machine=62)
(TMP / 'truncated.elf').write_bytes(b'\x7fELF\x02\x01')
(TMP / 'raw.bin').write_bytes(code(movz(0, 5), HLT))

# File beginning with a partial ELF magic should still be raw, not a malformed ELF.
(TMP / 'three_byte_raw.bin').write_bytes(b'\x7fEL')
PY

# TC-V08-CLI-009.
run_expect_status 2 ./emulator
require_contains "$TMP_DIR/stderr.txt" "raw little-endian AArch64 binary"
require_contains "$TMP_DIR/stderr.txt" "ELF64 ET_EXEC"

# TC-V08-CLI-001 and TC-V08-ACC-002.
run_expect_status 0 ./emulator run "$TMP_DIR/hello.elf"
require_exact_file "$TMP_DIR/stdout.txt" "hello from elf"
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V08-CLI-002 and TC-V08-ACC-001.
run_expect_status 0 ./emulator regs "$TMP_DIR/mov_hlt.elf"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x000000000000007b"
require_contains "$TMP_DIR/stdout.txt" "pc  = 0x0000000000004004"

# TC-V08-CLI-003 and TC-V08-ACC-005.
run_expect_status 0 ./emulator trace "$TMP_DIR/hello.elf"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"
require_contains "$TMP_DIR/stdout.txt" "hello from elf"
svc_line=$(grep -n "svc #0x0" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
hello_line=$(grep -n "hello from elf" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
if [ "$svc_line" -ge "$hello_line" ]; then
    echo "FAIL: expected svc trace before guest output" >&2
    cat "$TMP_DIR/stdout.txt" >&2
    exit 1
fi

# TC-V08-CLI-004 and TC-V08-DBG-005/006 equivalent memory inspection paths.
run_expect_status 0 ./emulator dump "$TMP_DIR/data_bss.elf" 0x2000 12
require_contains "$TMP_DIR/stdout.txt" "41 42 43 44 00 00 00 00 00 00 00 00"

# TC-V08-CLI-005, TC-V08-DBG-001/002/003/007, and TC-V08-ACC-006.
printf 'regs\nbreak 0x4000\nrun\nstep\nregs\nbreak 0x4008\ncontinue\ntrace on\nstep\nquit\n' \
    | ./emulator debug "$TMP_DIR/debug.elf" >"$TMP_DIR/debug.out" 2>"$TMP_DIR/debug.err"
require_contains "$TMP_DIR/debug.out" "pc  = 0x0000000000004000"
require_contains "$TMP_DIR/debug.out" "breakpoint hit at 0x0000000000004000"
require_contains "$TMP_DIR/debug.out" "x0  = 0x0000000000000001"
require_contains "$TMP_DIR/debug.out" "breakpoint hit at 0x0000000000004008"
require_contains "$TMP_DIR/debug.out" "trace enabled"
require_contains "$TMP_DIR/debug.out" "trace pc=0x0000000000004008"
require_exact_file "$TMP_DIR/debug.err" ""

printf 'x 0x2000 12\nquit\n' | ./emulator debug "$TMP_DIR/data_bss.elf" >"$TMP_DIR/debug_mem.out" 2>"$TMP_DIR/debug_mem.err"
require_contains "$TMP_DIR/debug_mem.out" "memory dump address=0x0000000000002000"
require_contains "$TMP_DIR/debug_mem.out" "41 42 43 44 00 00 00 00 00 00 00 00"

# TC-V08-EXEC-002 and TC-V08-ACC-003.
run_expect_status 9 ./emulator run "$TMP_DIR/exit9.elf"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V08-EXEC-004.
run_expect_status 0 ./emulator run "$TMP_DIR/stderr.elf"
require_contains "$TMP_DIR/stdout.txt" "halted"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x0000000000000003"
require_contains "$TMP_DIR/stdout.txt" "x8  = 0x0000000000000040"
require_contains "$TMP_DIR/stdout.txt" "pc  = 0x0000000000001014"
require_exact_file "$TMP_DIR/stderr.txt" "ERR"

# TC-V08-EXEC-012.
run_expect_status 0 ./emulator regs "$TMP_DIR/unknown_syscall.elf"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0xffffffffffffffda"

# TC-V08-EXEC-011 and CLI runtime error context.
run_expect_status 1 ./emulator trace "$TMP_DIR/bad_svc.elf"
require_contains "$TMP_DIR/stdout.txt" "svc #0x1"
require_contains "$TMP_DIR/stderr.txt" "unsupported svc immediate"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000004000"

# TC-V08-CLI-006, TC-V08-CLI-007, TC-V08-DBG-009, and error-message checks.
run_expect_status 1 ./emulator run "$TMP_DIR/truncated.elf"
require_contains "$TMP_DIR/stderr.txt" "error:"
require_contains "$TMP_DIR/stderr.txt" "ELF header is truncated"
require_exact_file "$TMP_DIR/stdout.txt" ""

run_expect_status 1 ./emulator run "$TMP_DIR/dynamic.elf"
require_contains "$TMP_DIR/stderr.txt" "ET_DYN"
run_expect_status 1 ./emulator run "$TMP_DIR/interp.elf"
require_contains "$TMP_DIR/stderr.txt" "PT_INTERP"
run_expect_status 1 ./emulator run "$TMP_DIR/wrong_machine.elf"
require_contains "$TMP_DIR/stderr.txt" "machine 62"
require_contains "$TMP_DIR/stderr.txt" "expected AArch64"

set +e
printf 'quit\n' | ./emulator debug "$TMP_DIR/truncated.elf" >"$TMP_DIR/debug_bad.out" 2>"$TMP_DIR/debug_bad.err"
status=$?
set -e
if [ "$status" -ne 1 ]; then
    echo "FAIL: malformed ELF debug should exit 1" >&2
    exit 1
fi
require_not_contains "$TMP_DIR/debug_bad.out" "emu>"
require_contains "$TMP_DIR/debug_bad.err" "ELF header is truncated"

# TC-V08-CLI-010 and TC-V08-ERR-006.
run_expect_status 1 ./emulator run "$TMP_DIR/does-not-exist.elf"
require_contains "$TMP_DIR/stderr.txt" "failed to open"
require_not_contains "$TMP_DIR/stderr.txt" "malformed ELF"

# TC-V08-HDR-003 and TC-V08-CLI-008: raw paths still work.
run_expect_status 0 ./emulator regs "$TMP_DIR/raw.bin"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x0000000000000005"
run_expect_status 1 ./emulator run "$TMP_DIR/three_byte_raw.bin"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"

# Representative old raw examples still work through CLI/debugger.
run_expect_status 0 ./emulator run examples/v0_1/add.bin
require_contains "$TMP_DIR/stdout.txt" "x2  = 0x0000000000000005"
run_expect_status 0 ./emulator trace examples/v0_2/cbnz_countdown.bin
require_contains "$TMP_DIR/stdout.txt" "cbnz x0"
run_expect_status 0 ./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x00000000000ffff8"
printf 'run\nquit\n' | ./emulator debug examples/v0_4/simple_call.bin >"$TMP_DIR/debug_old.out" 2>"$TMP_DIR/debug_old.err"
require_contains "$TMP_DIR/debug_old.out" "halted"

# TC-V08-DOC-*.
require_contains README.md "v0.8 Test Plan"
require_contains README.md "ELF64"
require_contains README.md "ET_EXEC"
require_contains README.md "PT_LOAD"
require_contains README.md 'No dynamic linker and no `PT_INTERP`.'
require_contains README.md 'No `ET_DYN`/PIE load-bias policy.'
require_contains README.md "No relocations."
require_contains examples/README.md "v0.8"
require_contains examples/README.md "hello_elf.elf"
require_contains lessons/v0.8-elf-loader.md 'raw `.bin`'
require_contains lessons/v0.8-elf-loader.md "program headers"
require_contains lessons/v0.8-elf-loader.md "PT_LOAD"
require_contains lessons/v0.8-elf-loader.md "entry point"
require_contains lessons/v0.8-elf-loader.md "bss"
require_contains lessons/v0.8-elf-loader.md "dynamic linking"
test -f examples/v0_8/hello_elf.s
test -f examples/v0_8/exit_status_elf.s
test -f examples/v0_8/bss_elf.s

printf 'v0.8 CLI/ELF tests passed\n'
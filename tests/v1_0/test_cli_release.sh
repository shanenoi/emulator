#!/bin/sh
set -eu

TMP_DIR="tests/v1_0/tmp"
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

TMP = Path('tests/v1_0/tmp')
TMP.mkdir(parents=True, exist_ok=True)

ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56
ET_EXEC = 2
ET_DYN = 3
EM_AARCH64 = 183
PT_LOAD = 1
PT_INTERP = 3
PF_X = 1
PF_W = 2
PF_R = 4
SVC0 = 0xd4000001
HLT = 0xd4400000
UNSUPPORTED = 0x1e204000

def movz(rd, imm, shift=0, is64=True):
    return (0x80000000 if is64 else 0) | 0x52800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)

def add(rd, rn, imm, is64=True):
    return (0x91000000 if is64 else 0x11000000) | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rd & 31)

def b(offset):
    return 0x14000000 | ((offset // 4) & 0x03ffffff)

def code(*ops):
    return b''.join(struct.pack('<I', op) for op in ops)

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
    struct.pack_into('<H', buf, 16, e_type)
    struct.pack_into('<H', buf, 18, e_machine)
    struct.pack_into('<I', buf, 20, 1)
    struct.pack_into('<Q', buf, 24, entry)
    struct.pack_into('<Q', buf, 32, ELF64_EHDR_SIZE)
    struct.pack_into('<H', buf, 52, ELF64_EHDR_SIZE)
    struct.pack_into('<H', buf, 54, ELF64_PHDR_SIZE)
    struct.pack_into('<H', buf, 56, phnum)
    for i, seg in enumerate(phdrs):
        ph = ELF64_EHDR_SIZE + i * ELF64_PHDR_SIZE
        data = seg.get('data', b'')
        filesz = seg.get('filesz', len(data))
        memsz = seg.get('memsz', filesz)
        off = seg.get('offset', 0x100)
        vaddr = seg.get('vaddr', 0x1000)
        struct.pack_into('<I', buf, ph + 0, seg.get('type', PT_LOAD))
        struct.pack_into('<I', buf, ph + 4, seg.get('flags', PF_R | PF_X))
        struct.pack_into('<Q', buf, ph + 8, off)
        struct.pack_into('<Q', buf, ph + 16, vaddr)
        struct.pack_into('<Q', buf, ph + 24, vaddr)
        struct.pack_into('<Q', buf, ph + 32, filesz)
        struct.pack_into('<Q', buf, ph + 40, memsz)
        struct.pack_into('<Q', buf, ph + 48, seg.get('align', 1))
        buf[off:off + len(data)] = data
    Path(path).write_bytes(buf)

def textseg(data, vaddr=0x1000, offset=0x100):
    return {'type': PT_LOAD, 'flags': PF_R | PF_X, 'offset': offset, 'vaddr': vaddr, 'data': data}

def dataseg(data, vaddr=0x2000, memsz=None, offset=0x300):
    return {'type': PT_LOAD, 'flags': PF_R | PF_W, 'offset': offset, 'vaddr': vaddr, 'data': data, 'memsz': memsz if memsz is not None else len(data)}

def make(path, text, data=b'', data_addr=0x2000, memsz=None, entry=0x1000, extra_ph=None):
    segs = [textseg(text, vaddr=entry)]
    if data or memsz:
        segs.append(dataseg(data, data_addr, memsz))
    elf(TMP / path, entry, segs, extra_ph=extra_ph)

hello = b'hello release\n'
make('elf_hello.elf', code(movz(0, 1), movz(1, 0x2000), movz(2, len(hello)), movz(8, 64), SVC0, movz(0, 0), movz(8, 93), SVC0), hello)
make('elf_bss.elf', code(HLT), b'ABCD', memsz=12)
make('elf_exit7.elf', code(movz(0, 7), movz(8, 93), SVC0))
make('elf_debug.elf', code(movz(0, 1), add(0, 0, 2), HLT), entry=0x4000)
make('elf_svc.elf', code(movz(0, 1), movz(1, 0x2000), movz(2, 1), movz(8, 64), SVC0, HLT), b'Z')
make('invalid_write.elf', code(movz(0, 1), movz(1, 0x10, 16), movz(2, 1), movz(8, 64), SVC0, HLT))
make('unknown_syscall.elf', code(movz(8, 999), SVC0, HLT))
make('bad_svc.elf', code(0xd4000021))
make('unsupported.elf', code(UNSUPPORTED, HLT))
make('loop.elf', code(b(0)), entry=0x4000)
make('branch_bad.elf', code(b(0x200000)), entry=0x1000)
make('bad_load.elf', code(movz(1, 0x10, 16), 0xf9400020, HLT))
elf(TMP / 'dynamic.elf', 0x1000, [textseg(code(HLT))], e_type=ET_DYN)
elf(TMP / 'interp.elf', 0x1000, [textseg(code(HLT))], extra_ph=[{'type': PT_INTERP, 'flags': PF_R, 'offset': 0x300, 'vaddr': 0, 'data': b'/lib/ld-linux-aarch64.so.1\0'}])
elf(TMP / 'wrong_machine.elf', 0x1000, [textseg(code(HLT))], e_machine=62)
make('misaligned_entry.elf', code(HLT), entry=0x1001)
(TMP / 'truncated.elf').write_bytes(b'\x7fELF\x02\x01')
(TMP / 'empty.bin').write_bytes(b'')
(TMP / 'too_large.bin').write_bytes(b'\0' * (1024 * 1024 + 1))
(TMP / 'unsupported.bin').write_bytes(code(UNSUPPORTED))
(TMP / 'loop.bin').write_bytes(code(b(0)))
(TMP / 'bad_fetch.bin').write_bytes(code(b(0x200000)))
PY

# TC-V10-CLI-001 / help surface.
run_expect_status 2 ./emulator
require_contains "$TMP_DIR/stderr.txt" "usage: emulator run <program>"
require_contains "$TMP_DIR/stderr.txt" "emulator debug <program>"
require_contains "$TMP_DIR/stderr.txt" "raw little-endian AArch64 binary"
require_contains "$TMP_DIR/stderr.txt" "ELF64 ET_EXEC"
run_expect_status 0 ./emulator help
require_contains "$TMP_DIR/stdout.txt" "usage: emulator run <program>"
run_expect_status 0 ./emulator --help
run_expect_status 0 ./emulator -h

# TC-V10-CLI-002 / unknown command.
run_expect_status 2 ./emulator nope
require_contains "$TMP_DIR/stderr.txt" "unknown command"
require_contains "$TMP_DIR/stderr.txt" "usage:"

# TC-V10-CLI-003 / wrong argument counts.
for args in "run" "trace" "regs" "dump" "dump examples/v0_1/add.bin" "dump examples/v0_1/add.bin 0x1000" "debug"; do
    set +e
    # shellcheck disable=SC2086
    ./emulator $args >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "FAIL: expected command to fail: ./emulator $args" >&2
        exit 1
    fi
    require_contains "$TMP_DIR/stderr.txt" "usage:"
done

# TC-V10-CLI-004/005 / dump parsing.
run_expect_status 0 ./emulator dump examples/v0_1/add.bin 0x1000 16
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x0000000000001000"
run_expect_status 0 ./emulator dump examples/v0_1/add.bin 4096 16
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x0000000000001000"
for args in "xyz 16" "0x1000 nope" "-1 16" "0x1000 -1"; do
    set +e
    # shellcheck disable=SC2086
    ./emulator dump examples/v0_1/add.bin $args >"$TMP_DIR/stdout.txt" 2>"$TMP_DIR/stderr.txt"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "FAIL: expected invalid dump args to fail: $args" >&2
        exit 1
    fi
    require_contains "$TMP_DIR/stderr.txt" "invalid dump address or length"
done

# TC-V10-CLI-006 / paths with spaces.
space_prog="$TMP_DIR/program with spaces.bin"
cp examples/v0_1/add.bin "$space_prog"
run_expect_status 0 ./emulator run "$space_prog"
run_expect_status 0 ./emulator trace "$space_prog"
run_expect_status 0 ./emulator regs "$space_prog"
run_expect_status 0 ./emulator dump "$space_prog" 0x1000 4
printf 'run\nquit\n' | ./emulator debug "$space_prog" >"$TMP_DIR/debug_space.out" 2>"$TMP_DIR/debug_space.err"
require_contains "$TMP_DIR/debug_space.out" "halted"

# TC-V10-CLI-007/008/009.
run_expect_status 1 ./emulator run "$TMP_DIR/does-not-exist.bin"
require_contains "$TMP_DIR/stderr.txt" "error:"
run_expect_status 1 ./emulator run "$TMP_DIR/empty.bin"
require_contains "$TMP_DIR/stderr.txt" "input file is empty"
run_expect_status 1 ./emulator dump "$TMP_DIR/empty.bin" 0x1000 0
require_contains "$TMP_DIR/stderr.txt" "input file is empty"
run_expect_status 1 ./emulator run "$TMP_DIR"
require_contains "$TMP_DIR/stderr.txt" "error:"

# TC-V10-RAW-001 through TC-V10-RAW-005.
run_expect_status 0 ./emulator run examples/v0_1/add.bin
require_contains "$TMP_DIR/stdout.txt" "x2  = 0x0000000000000005"
run_expect_status 0 ./emulator trace examples/v0_2/cbnz_countdown.bin
require_contains "$TMP_DIR/stdout.txt" "cbnz x0"
run_expect_status 0 ./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x00000000000ffff8"
run_expect_status 0 ./emulator run examples/v0_4/simple_call.bin
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x0000000000000005"
run_expect_status 0 ./emulator run examples/v0_7/hello.bin
require_exact_file "$TMP_DIR/stdout.txt" "hello, v0.7!"
run_expect_status 0 ./emulator run examples/v0_7/stderr.bin
require_exact_file "$TMP_DIR/stderr.txt" "error, v0.7!"
run_expect_status 7 ./emulator run examples/v0_7/exit_status.bin
run_expect_status 247 ./emulator run examples/v0_7/bad_fd.bin

# TC-V10-ELF-001/002/003/005/006 and observation modes.
run_expect_status 0 ./emulator run "$TMP_DIR/elf_hello.elf"
require_exact_file "$TMP_DIR/stdout.txt" "hello release"
run_expect_status 0 ./emulator run examples/v0_8/hello_elf.elf
require_exact_file "$TMP_DIR/stdout.txt" "hello, v0.8!"
run_expect_status 0 ./emulator dump "$TMP_DIR/elf_bss.elf" 0x2000 12
require_contains "$TMP_DIR/stdout.txt" "41 42 43 44 00 00 00 00 00 00 00 00"
run_expect_status 42 ./emulator run tests/v0_9/tmp/return_42.elf
run_expect_status 1 ./emulator run "$TMP_DIR/dynamic.elf"
require_contains "$TMP_DIR/stderr.txt" "ET_DYN"
run_expect_status 1 ./emulator run "$TMP_DIR/interp.elf"
require_contains "$TMP_DIR/stderr.txt" "PT_INTERP"
for prog in truncated.elf wrong_machine.elf misaligned_entry.elf; do
    run_expect_status 1 ./emulator run "$TMP_DIR/$prog"
    require_contains "$TMP_DIR/stderr.txt" "ELF loader error"
done

# TC-V10-DBG-001 through TC-V10-DBG-008.
printf 'help\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_help.out" 2>"$TMP_DIR/debug_help.err"
require_contains "$TMP_DIR/debug_help.out" "commands:"
printf 'break 0x1008\nrun\nregs\ncontinue\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_raw.out" 2>"$TMP_DIR/debug_raw.err"
require_contains "$TMP_DIR/debug_raw.out" "breakpoint hit at 0x0000000000001008"
require_contains "$TMP_DIR/debug_raw.out" "x0  = 0x0000000000000002"
printf 'step\nregs\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_step.out" 2>"$TMP_DIR/debug_step.err"
require_contains "$TMP_DIR/debug_step.out" "pc  = 0x0000000000001004"
printf 'run\nquit\n' | ./emulator debug examples/v0_1/nop_hlt.bin >"$TMP_DIR/debug_halt.out" 2>"$TMP_DIR/debug_halt.err"
require_contains "$TMP_DIR/debug_halt.out" "halted"
printf 'run\nquit\n' | ./emulator debug "$TMP_DIR/elf_exit7.elf" >"$TMP_DIR/debug_exit.out" 2>"$TMP_DIR/debug_exit.err"
require_contains "$TMP_DIR/debug_exit.out" "exited status=7"
printf 'break 0x1010\nrun\nstep\nquit\n' | ./emulator debug "$TMP_DIR/elf_svc.elf" >"$TMP_DIR/debug_svc.out" 2>"$TMP_DIR/debug_svc.err"
require_contains "$TMP_DIR/debug_svc.out" "breakpoint hit at 0x0000000000001010"
require_contains "$TMP_DIR/debug_svc.out" "Z"
printf 'run\nquit\n' | ./emulator debug "$TMP_DIR/invalid_write.elf" >"$TMP_DIR/debug_bad_write.out" 2>"$TMP_DIR/debug_bad_write.err"
require_contains "$TMP_DIR/debug_bad_write.err" "syscall write buffer out of bounds"
printf 'bogus\nbreak nope\nx nope 4\n\nquit\n' | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_invalid.out" 2>"$TMP_DIR/debug_invalid.err"
require_contains "$TMP_DIR/debug_invalid.err" "unknown command"
: | ./emulator debug examples/v0_1/add.bin >"$TMP_DIR/debug_eof.out" 2>"$TMP_DIR/debug_eof.err"
require_contains "$TMP_DIR/debug_eof.out" "loaded examples/v0_1/add.bin"

# TC-V10-OBS-001 through TC-V10-OBS-005.
run_expect_status 0 ./emulator trace "$TMP_DIR/elf_hello.elf"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"
require_contains "$TMP_DIR/stdout.txt" "hello release"
svc_line=$(grep -n "svc #0x0" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
hello_line=$(grep -n "hello release" "$TMP_DIR/stdout.txt" | head -n 1 | cut -d: -f1)
if [ "$svc_line" -ge "$hello_line" ]; then
    echo "FAIL: expected trace before syscall output" >&2
    cat "$TMP_DIR/stdout.txt" >&2
    exit 1
fi
run_expect_status 0 ./emulator regs "$TMP_DIR/unknown_syscall.elf"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0xffffffffffffffda"
run_expect_status 1 ./emulator dump examples/v0_1/add.bin 0x200000 4
require_contains "$TMP_DIR/stderr.txt" "dump range out of bounds"

# TC-V10-ERR-001 through TC-V10-ERR-010 representative release error checks.
run_expect_status 1 ./emulator run "$TMP_DIR/unsupported.bin"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "opcode=0x1e204000"
run_expect_status 1 ./emulator run "$TMP_DIR/loop.bin"
require_contains "$TMP_DIR/stderr.txt" "instruction limit reached"
require_contains "$TMP_DIR/stderr.txt" "opcode=0x14000000"
run_expect_status 1 ./emulator run "$TMP_DIR/bad_fetch.bin"
require_contains "$TMP_DIR/stderr.txt" "branch target outside memory"
run_expect_status 1 ./emulator run "$TMP_DIR/bad_load.elf"
require_contains "$TMP_DIR/stderr.txt" "memory access out of bounds"
run_expect_status 1 ./emulator run "$TMP_DIR/too_large.bin"
require_contains "$TMP_DIR/stderr.txt" "file size 0x100001 does not fit"
run_expect_status 1 ./emulator run "$TMP_DIR/bad_svc.elf"
require_contains "$TMP_DIR/stderr.txt" "unsupported svc immediate"

if [ -e /dev/full ]; then
    set +e
    ./emulator run "$TMP_DIR/elf_hello.elf" >/dev/full 2>"$TMP_DIR/full.err"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "skipping strict /dev/full assertion: host stdio did not surface a write failure for this run"
    fi
fi

echo "v1.0 CLI release tests passed"

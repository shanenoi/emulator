#!/bin/sh
set -eu

TMP_DIR="tests/v0_9/tmp"
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

TMP = Path('tests/v0_9/tmp')
TMP.mkdir(parents=True, exist_ok=True)

ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56
ET_EXEC = 2
EM_AARCH64 = 183
PT_LOAD = 1
PT_INTERP = 3
PF_X = 1
PF_W = 2
PF_R = 4
SVC0 = 0xd4000001
HLT = 0xd4400000
UNSUPPORTED = 0xb24003e0
EMU_SYSCALL_WRITE = 64
EMU_SYSCALL_EXIT = 93


def movz(rd, imm, shift=0, is64=True):
    return (0x80000000 if is64 else 0) | 0x52800000 | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)

def addsub_imm(rd, rn, imm, sub=False, is64=True):
    return (0x91000000 if is64 else 0x11000000) | (0x40000000 if sub else 0) | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rd & 31)

def add_reg(rd, rn, rm, is64=True):
    return (0x8b000000 if is64 else 0x0b000000) | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31)

def mul(rd, rn, rm, is64=True):
    return (0x9b000000 if is64 else 0x1b000000) | ((rm & 31) << 16) | (31 << 10) | ((rn & 31) << 5) | (rd & 31)

def ldrstr(rt, rn, offset, load=True, size=1):
    size_bits = {1:0, 2:1, 4:2, 8:3}[size]
    imm12 = offset >> {1:0, 2:1, 4:2, 8:3}[size]
    return (size_bits << 30) | 0x39000000 | ((1 if load else 0) << 22) | ((imm12 & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)

def b(offset):
    return 0x14000000 | ((offset // 4) & 0x03ffffff)

def bl(offset):
    return 0x94000000 | ((offset // 4) & 0x03ffffff)

def ret(rn=30):
    return 0xd65f0000 | ((rn & 31) << 5)

def code(*ops):
    return b''.join(struct.pack('<I', op) for op in ops)

def start_to_main(main_ops):
    ops = [bl(0x10), movz(8, EMU_SYSCALL_EXIT), SVC0, HLT]
    ops.extend(main_ops)
    return code(*ops)

def elf(path, entry, segments, e_type=ET_EXEC, extra_ph=None):
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
    struct.pack_into('<H', buf, 18, EM_AARCH64)
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
        struct.pack_into('<I', buf, ph + 0, seg.get('type', PT_LOAD))
        struct.pack_into('<I', buf, ph + 4, seg.get('flags', PF_R | PF_X))
        struct.pack_into('<Q', buf, ph + 8, seg.get('offset', 0x100))
        struct.pack_into('<Q', buf, ph + 16, seg.get('vaddr', 0x1000))
        struct.pack_into('<Q', buf, ph + 32, filesz)
        struct.pack_into('<Q', buf, ph + 40, memsz)
        struct.pack_into('<Q', buf, ph + 48, seg.get('align', 1))
        off = seg.get('offset', 0x100)
        buf[off:off+len(data)] = data
    Path(path).write_bytes(buf)

def textseg(data):
    return {'type': PT_LOAD, 'flags': PF_R|PF_X, 'offset': 0x100, 'vaddr': 0x1000, 'data': data}

def dataseg(data, vaddr=0x2000, memsz=None):
    return {'type': PT_LOAD, 'flags': PF_R|PF_W, 'offset': 0x300, 'vaddr': vaddr, 'data': data, 'memsz': memsz if memsz is not None else len(data)}

def make(path, text, data=b'', data_addr=0x2000, memsz=None, extra_ph=None):
    segs = [textseg(text)]
    if data or memsz:
        segs.append(dataseg(data, data_addr, memsz))
    elf(TMP / path, 0x1000, segs, extra_ph=extra_ph)

make('return_42.elf', start_to_main([movz(0, 42), ret()]))
make('return_300.elf', start_to_main([movz(0, 300), ret()]))
make('negative_return.elf', start_to_main([0x92800000, ret()]))  # movn x0,#0 => -1
make('stack_locals.elf', start_to_main([
    addsub_imm(31, 31, 16, sub=True),
    movz(0, 31),
    ldrstr(0, 31, 0, load=False, size=1),
    movz(0, 0),
    ldrstr(0, 31, 0, load=True, size=1),
    addsub_imm(31, 31, 16),
    ret(),
]))
make('nested_calls.elf', code(
    bl(0x10), movz(8, EMU_SYSCALL_EXIT), SVC0, HLT,
    0xaa1e03f4, bl(0x0c), ret(20), HLT,
    0xaa1e03f3, bl(0x0c), ret(19), HLT,
    movz(0, 7), movz(1, 3), add_reg(0, 0, 1), ret()
))
make('hello_c.elf', start_to_main([
    movz(0, 1), movz(1, 0x2000), movz(2, 13), movz(8, EMU_SYSCALL_WRITE), SVC0, movz(0, 0), ret()
]), b'hello from c\n')
make('stderr_c.elf', start_to_main([
    movz(0, 2), movz(1, 0x2000), movz(2, 8), movz(8, EMU_SYSCALL_WRITE), SVC0, movz(0, 0), ret()
]), b'c error\n')
make('byte_copy.elf', start_to_main([
    movz(1, 0x2000), movz(2, 0x2010),
    ldrstr(3, 1, 0, True, 1), ldrstr(3, 2, 0, False, 1),
    ldrstr(3, 1, 1, True, 1), ldrstr(3, 2, 1, False, 1),
    ldrstr(3, 1, 2, True, 1), ldrstr(3, 2, 2, False, 1),
    movz(0, 0), ret()
]), b'ABC\x00' + b'\x00' * 16, memsz=32)
# TC-V09-CRT-008: global initialized int data.
make('global_sum.elf', start_to_main([
    movz(1, 0x2000),
    ldrstr(0, 1, 0, True, 4),
    ldrstr(2, 1, 4, True, 4),
    add_reg(0, 0, 2, False),
    ldrstr(2, 1, 8, True, 4),
    add_reg(0, 0, 2, False),
    ret()
]), struct.pack('<III', 1, 2, 3))
# TC-V09-CRT-009 and TC-V09-CRT-010: zero-filled global/static storage.
make('static_bss.elf', start_to_main([
    movz(1, 0x2000),
    ldrstr(0, 1, 0, True, 4),
    addsub_imm(0, 0, 7, False, False),
    ldrstr(0, 1, 0, False, 4),
    ret()
]), b'', memsz=16)
make('bad_fd.elf', start_to_main([
    movz(0, 99), movz(1, 0x2000), movz(2, 1), movz(8, EMU_SYSCALL_WRITE), SVC0, ret()
]), b'X')
make('unknown_syscall.elf', start_to_main([
    movz(8, 999), SVC0, ret()
]))
make('invalid_write.elf', start_to_main([
    movz(0, 1), movz(1, 0x10, 16), movz(2, 1), movz(8, EMU_SYSCALL_WRITE), SVC0, ret()
]))
make('unsupported.elf', code(UNSUPPORTED, HLT))
make('infinite.elf', code(b(0)))
make('recursive_limit.elf', code(bl(0)))
# Hosted/libc-like dynamic/interpreter rejection fixture.
elf(TMP / 'hosted_interp.elf', 0x1000, [textseg(code(HLT))], extra_ph=[{'type': PT_INTERP, 'flags': PF_R, 'offset': 0x80, 'vaddr': 0, 'data': b'/lib/ld-linux-aarch64.so.1\0'}])
PY

# TC-V09-CLI-001 and TC-V09-CRT-001.
run_expect_status 42 ./emulator run "$TMP_DIR/return_42.elf"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V09-CRT-003.
run_expect_status 44 ./emulator run "$TMP_DIR/return_300.elf"
require_exact_file "$TMP_DIR/stdout.txt" ""

# TC-V09-ERR-019.
run_expect_status 255 ./emulator run "$TMP_DIR/negative_return.elf"

# TC-V09-CLI-002.
run_expect_status 42 ./emulator regs "$TMP_DIR/return_42.elf"
require_contains "$TMP_DIR/stdout.txt" "x0  = 0x000000000000002a"
require_contains "$TMP_DIR/stdout.txt" "x8  = 0x000000000000005d"

# TC-V09-CLI-003 and TC-V09-CRT-002.
run_expect_status 42 ./emulator trace "$TMP_DIR/return_42.elf"
require_contains "$TMP_DIR/stdout.txt" "trace pc=0x0000000000001000"
require_contains "$TMP_DIR/stdout.txt" "bl 0x0000000000001010"
require_contains "$TMP_DIR/stdout.txt" "ret"
require_contains "$TMP_DIR/stdout.txt" "svc #0x0"

# TC-V09-CRT-004.
run_expect_status 31 ./emulator trace "$TMP_DIR/stack_locals.elf"
require_contains "$TMP_DIR/stdout.txt" "sub sp, sp, #0x10"
require_contains "$TMP_DIR/stdout.txt" "strb w0, [sp]"
require_contains "$TMP_DIR/stdout.txt" "ldrb w0, [sp]"

# TC-V09-CRT-005.
run_expect_status 10 ./emulator trace "$TMP_DIR/nested_calls.elf"
require_contains "$TMP_DIR/stdout.txt" "bl 0x0000000000001010"
require_contains "$TMP_DIR/stdout.txt" "bl 0x0000000000001020"
require_contains "$TMP_DIR/stdout.txt" "bl 0x0000000000001030"

# TC-V09-CRT-008, TC-V09-CRT-009, and TC-V09-CRT-010.
run_expect_status 6 ./emulator run "$TMP_DIR/global_sum.elf"
run_expect_status 7 ./emulator run "$TMP_DIR/static_bss.elf"

# TC-V09-CRT-007: recursion-like self-call reaches the instruction limit with context.
run_expect_status 1 ./emulator run "$TMP_DIR/recursive_limit.elf"
require_contains "$TMP_DIR/stderr.txt" "instruction limit reached"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "opcode=0x94000000"

# TC-V09-CLI-004 and TC-V09-CRT-014.
run_expect_status 0 ./emulator dump "$TMP_DIR/byte_copy.elf" 0x2010 4
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x0000000000002010"
require_contains "$TMP_DIR/stdout.txt" "41 42 43 00"

# TC-V09-CLI-005 and TC-V09-CLI-006.
printf 'break 0x1000\nrun\nstep\nregs\ncontinue\nquit\n' | ./emulator debug "$TMP_DIR/return_42.elf" >"$TMP_DIR/debug_start.out" 2>"$TMP_DIR/debug_start.err"
require_contains "$TMP_DIR/debug_start.out" "breakpoint hit at 0x0000000000001000"
require_contains "$TMP_DIR/debug_start.out" "x30 = 0x0000000000001004"
require_contains "$TMP_DIR/debug_start.out" "exited status=42"
printf 'break 0x1010\nrun\nregs\ncontinue\nquit\n' | ./emulator debug "$TMP_DIR/return_42.elf" >"$TMP_DIR/debug_main.out" 2>"$TMP_DIR/debug_main.err"
require_contains "$TMP_DIR/debug_main.out" "breakpoint hit at 0x0000000000001010"
require_contains "$TMP_DIR/debug_main.out" "pc  = 0x0000000000001010"

# TC-V09-CLI-007.
printf 'trace on\nrun\nquit\n' | ./emulator debug "$TMP_DIR/stack_locals.elf" >"$TMP_DIR/debug_trace.out" 2>"$TMP_DIR/debug_trace.err"
require_contains "$TMP_DIR/debug_trace.out" "trace enabled"
require_contains "$TMP_DIR/debug_trace.out" "sub sp, sp, #0x10"
require_contains "$TMP_DIR/debug_trace.out" "ldrb w0, [sp]"

# TC-V09-CLI-008.
run_expect_status 0 ./emulator run "$TMP_DIR/hello_c.elf"
require_exact_file "$TMP_DIR/stdout.txt" "hello from c"
require_exact_file "$TMP_DIR/stderr.txt" ""

# TC-V09-CLI-009.
run_expect_status 0 ./emulator run "$TMP_DIR/stderr_c.elf"
require_exact_file "$TMP_DIR/stdout.txt" ""
require_exact_file "$TMP_DIR/stderr.txt" "c error"

# TC-V09-CLI-010.
run_expect_status 1 ./emulator run "$TMP_DIR/unsupported.elf"
require_contains "$TMP_DIR/stderr.txt" "unsupported instruction"
require_contains "$TMP_DIR/stderr.txt" "pc=0x0000000000001000"
require_contains "$TMP_DIR/stderr.txt" "opcode=0xb24003e0"

# TC-V09-ERR-015 through TC-V09-ERR-018.
run_expect_status 247 ./emulator run "$TMP_DIR/bad_fd.elf"
run_expect_status 218 ./emulator run "$TMP_DIR/unknown_syscall.elf"
run_expect_status 1 ./emulator run "$TMP_DIR/invalid_write.elf"
require_contains "$TMP_DIR/stderr.txt" "syscall write buffer out of bounds"

# TC-V09-BUILD-001 is exercised by the top-level make test dependency on examples.
# TC-V09-BUILD-002 is exercised by tests/v0_9/test_optional_c_examples.sh when tools exist.

# TC-V09-BUILD-003 and TC-V09-BUILD-004.
require_contains README.md "-ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0"
require_contains examples/README.md "-ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0"
run_expect_status 1 ./emulator run "$TMP_DIR/hosted_interp.elf"
require_contains "$TMP_DIR/stderr.txt" "PT_INTERP"

# TC-V09-DOCS.
for text in "freestanding C" "_start" "hosted C" "logical-immediate" "UXTB" "SXTW" "write = 64" "exit = 93"; do
    require_contains README.md "$text"
    require_contains lessons/v0.9-tiny-c-programs.md "$text"
done
require_contains README.md "v0.9 Test Plan"
require_not_contains README.md "v0.9 automated tests are intentionally not added yet"
require_not_contains README.md "v0.1 through v0.8 C test runners"
require_not_contains README.md "v0.1 through v0.8 CLI checks"

# TC-V09-REG-001 through TC-V09-REG-008, representative regression checks.
run_expect_status 0 ./emulator run examples/v0_1/add.bin
require_contains "$TMP_DIR/stdout.txt" "x2  = 0x0000000000000005"
run_expect_status 0 ./emulator trace examples/v0_2/cbnz_countdown.bin
require_contains "$TMP_DIR/stdout.txt" "cbnz x0"
run_expect_status 0 ./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
require_contains "$TMP_DIR/stdout.txt" "memory dump address=0x00000000000ffff8"
printf 'run\nquit\n' | ./emulator debug examples/v0_4/simple_call.bin >"$TMP_DIR/debug_old.out" 2>"$TMP_DIR/debug_old.err"
require_contains "$TMP_DIR/debug_old.out" "halted"
run_expect_status 0 ./emulator run examples/v0_7/hello.bin
require_exact_file "$TMP_DIR/stdout.txt" "hello, v0.7!"
run_expect_status 0 ./emulator run examples/v0_8/hello_elf.elf
require_exact_file "$TMP_DIR/stdout.txt" "hello, v0.8!"

echo "v0.9 CLI/C-program tests passed"

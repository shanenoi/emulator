#!/usr/bin/env python3
"""Generate deterministic v1.4 exception test fixtures."""
from __future__ import annotations

import argparse
from pathlib import Path
from struct import pack, pack_into

LOAD = 0x1000
VECTOR = 0x1080
EXC = 0x09030000
UART = 0x09000000
ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56
ET_EXEC = 2
EM_AARCH64 = 183
PT_LOAD = 1
PF_X = 1
PF_W = 2
PF_R = 4

def emit(path: Path, words: list[int]) -> None:
    path.write_bytes(b''.join(pack('<I', w & 0xffffffff) for w in words))

def words_bytes(words: list[int]) -> bytes:
    return b''.join(pack('<I', w & 0xffffffff) for w in words)

def emit_elf(path: Path, entry: int, segments: list[dict[str, object]]) -> None:
    phnum = len(segments)
    size = max([ELF64_EHDR_SIZE + phnum * ELF64_PHDR_SIZE] +
               [int(seg.get('offset', 0)) + len(seg.get('data', b'')) for seg in segments])
    buf = bytearray(size)
    buf[0:4] = b'\x7fELF'
    buf[4] = 2
    buf[5] = 1
    buf[6] = 1
    pack_into('<HHIQQQ', buf, 16, ET_EXEC, EM_AARCH64, 1, entry, ELF64_EHDR_SIZE, 0)
    pack_into('<IHHHHHH', buf, 48, 0, ELF64_EHDR_SIZE, ELF64_PHDR_SIZE, phnum, 0, 0, 0)
    for i, seg in enumerate(segments):
        off = ELF64_EHDR_SIZE + i * ELF64_PHDR_SIZE
        data = seg.get('data', b'')
        file_offset = int(seg.get('offset', 0))
        filesz = int(seg.get('filesz', len(data)))
        memsz = int(seg.get('memsz', filesz))
        vaddr = int(seg.get('vaddr', 0))
        flags = int(seg.get('flags', PF_R | PF_X))
        pack_into('<IIQQQQQQ', buf, off, PT_LOAD, flags, file_offset, vaddr, vaddr, filesz, memsz, 0x1000)
        if data:
            buf[file_offset:file_offset + len(data)] = data
    path.write_bytes(buf)

def movz(rd: int, imm: int, shift: int = 0, is64: bool = True) -> int:
    return (0xd2800000 if is64 else 0x52800000) | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)

def movk(rd: int, imm: int, shift: int = 0, is64: bool = True) -> int:
    return (0xf2800000 if is64 else 0x72800000) | (((shift // 16) & 3) << 21) | ((imm & 0xffff) << 5) | (rd & 31)

def mov_imm64(rd: int, value: int) -> list[int]:
    out = [movz(rd, value & 0xffff)]
    for shift in (16, 32, 48):
        part = (value >> shift) & 0xffff
        if part:
            out.append(movk(rd, part, shift))
    return out

def add_imm(rd: int, rn: int, imm: int, is64: bool = True) -> int:
    return (0x91000000 if is64 else 0x11000000) | ((imm & 0xfff) << 10) | ((rn & 31) << 5) | (rd & 31)

def str_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xb9000000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)

def str_x(rt: int, rn: int, imm: int = 0) -> int:
    return 0xf9000000 | (((imm // 8) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)

def ldr_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xb9400000 | (((imm // 4) & 0xfff) << 10) | ((rn & 31) << 5) | (rt & 31)

def svc(imm: int = 0) -> int:
    return 0xd4000001 | ((imm & 0xffff) << 5)

def brk(imm: int = 0) -> int:
    return 0xd4200000 | ((imm & 0xffff) << 5)

def eret() -> int:
    return 0xd69f03e0

def hlt() -> int:
    return 0xd4400000

def nop() -> int:
    return 0xd503201f

def pad(words: list[int], address: int = VECTOR) -> None:
    while len(words) < (address - LOAD) // 4:
        words.append(nop())

def handler_return() -> list[int]:
    return [eret()]

def handler_skip() -> list[int]:
    return [add_imm(3, 3, 4), eret()]

def handler_disable_timer() -> list[int]:
    w: list[int] = []
    w.extend(mov_imm64(0, EXC))
    w.append(movz(6, 0))
    w.append(str_x(6, 0, 0x10))
    w.append(eret())
    return w

def install_vector(prefix: list[int]) -> None:
    prefix.extend(mov_imm64(0, EXC))
    prefix.extend(mov_imm64(1, VECTOR))
    prefix.append(str_x(1, 0, 0x00))
    prefix.append(movz(2, 0x3, is64=False))
    prefix.append(str_w(2, 0, 0x08))

def cli_handled_brk() -> list[int]:
    w = [brk(0x14), hlt()]
    pad(w); w.extend(handler_return()); return w

def mmio_handled_brk() -> list[int]:
    w: list[int] = []
    install_vector(w)
    w += [brk(0x15), hlt()]
    pad(w); w.extend(handler_return()); return w

def mmio_skip_device_fault() -> list[int]:
    w: list[int] = []
    install_vector(w)
    w.extend(mov_imm64(4, UART))
    w.append(ldr_w(5, 4, 0))
    w.append(hlt())
    pad(w); w.extend(handler_skip()); return w

def mmio_timer_once() -> list[int]:
    w: list[int] = []
    install_vector(w)
    w.append(movz(6, 12))
    w.append(str_x(6, 0, 0x10))
    w += [nop()] * 16
    w.append(hlt())
    pad(w); w.extend(handler_disable_timer()); return w

def handled_invalid() -> list[int]:
    w = [0xffffffff, hlt()]
    pad(w); w.extend(handler_skip()); return w

def handled_svc1() -> list[int]:
    w = [svc(1), hlt()]
    pad(w); w.extend(handler_return()); return w

def unhandled_brk() -> list[int]:
    return [brk(0x22), hlt()]

def eret_without_exception() -> list[int]:
    return [eret(), hlt()]

def vector_fault() -> list[int]:
    w = [brk(0x33), hlt()]
    pad(w); w.append(0xffffffff); return w

def queued_timer_masked() -> list[int]:
    w = [nop(), nop(), hlt()]
    pad(w); w.extend(handler_return()); return w

FIXTURES = {
    'cli_handled_brk.bin': cli_handled_brk,
    'mmio_handled_brk.bin': mmio_handled_brk,
    'mmio_skip_device_fault.bin': mmio_skip_device_fault,
    'mmio_timer_once.bin': mmio_timer_once,
    'handled_invalid.bin': handled_invalid,
    'handled_svc1.bin': handled_svc1,
    'unhandled_brk.bin': unhandled_brk,
    'eret_without_exception.bin': eret_without_exception,
    'vector_fault.bin': vector_fault,
    'queued_timer_masked.bin': queued_timer_masked,
}

def elf_handled_brk(path: Path) -> None:
    text = bytearray(0x1000)
    text[0:8] = words_bytes([brk(0x55), hlt()])
    text[VECTOR - LOAD:VECTOR - LOAD + 4] = words_bytes([eret()])
    emit_elf(path, LOAD, [{'offset': 0x1000, 'vaddr': LOAD, 'data': bytes(text), 'flags': PF_R | PF_X}])

def elf_nonexec_vector(path: Path) -> None:
    emit_elf(path, LOAD, [
        {'offset': 0x1000, 'vaddr': LOAD, 'data': words_bytes([hlt()]), 'flags': PF_R | PF_X},
        {'offset': 0x2000, 'vaddr': 0x3000, 'data': words_bytes([eret()]), 'flags': PF_R | PF_W},
    ])

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', default='tests/v1_4/tmp')
    args = parser.parse_args()
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in FIXTURES.items():
        emit(out / name, fn())
    elf_handled_brk(out / 'elf_handled_brk.elf')
    elf_nonexec_vector(out / 'elf_nonexec_vector.elf')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

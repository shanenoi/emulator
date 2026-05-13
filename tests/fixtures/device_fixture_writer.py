#!/usr/bin/env python3
"""Generate deterministic v1.3 memory-mapped-device test fixtures."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

UART = 0x09000000
TIMER = 0x09010000
RANDOM = 0x09020000
LOAD = 0x1000
PF_X = 1
PF_W = 2
PF_R = 4
PT_LOAD = 1
ET_EXEC = 2
EM_AARCH64 = 183
ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56


def u16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)


def code(*ops: int) -> bytes:
    return b"".join(u32(op) for op in ops)


def emit(path: Path, ops: list[int]) -> None:
    path.write_bytes(code(*ops))


def movz(rd: int, imm16: int, shift: int = 0, *, x: bool = True) -> int:
    return (0xD2800000 if x else 0x52800000) | (((shift // 16) & 3) << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 31)


def movk(rd: int, imm16: int, shift: int = 0, *, x: bool = True) -> int:
    return (0xF2800000 if x else 0x72800000) | (((shift // 16) & 3) << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 31)


def mov_abs(rd: int, value: int) -> list[int]:
    ops = [movz(rd, value & 0xFFFF, 0)]
    for shift in (16, 32, 48):
        part = (value >> shift) & 0xFFFF
        if part:
            ops.append(movk(rd, part, shift))
    return ops


def movw(rd: int, value: int) -> list[int]:
    return [movz(rd, value & 0xFFFF, 0, x=False), movk(rd, (value >> 16) & 0xFFFF, 16, x=False)]


def ldr(rt: int, rn: int, size: int, imm: int = 0) -> int:
    return 0x39000000 | ((size & 3) << 30) | (1 << 22) | (((imm // (1 << size)) & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31)


def str_(rt: int, rn: int, size: int, imm: int = 0) -> int:
    return 0x39000000 | ((size & 3) << 30) | (((imm // (1 << size)) & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31)


def ldr_unscaled(rt: int, rn: int, size: int, offset: int) -> int:
    return 0x38400000 | ((size & 3) << 30) | (((offset & 0x1FF) << 12)) | ((rn & 31) << 5) | (rt & 31)


def str_unscaled(rt: int, rn: int, size: int, offset: int) -> int:
    return 0x38000000 | ((size & 3) << 30) | (((offset & 0x1FF) << 12)) | ((rn & 31) << 5) | (rt & 31)


def ldr_pre(rt: int, rn: int, size: int, offset: int) -> int:
    return 0x38400C00 | ((size & 3) << 30) | (((offset & 0x1FF) << 12)) | ((rn & 31) << 5) | (rt & 31)


def str_post(rt: int, rn: int, size: int, offset: int) -> int:
    return 0x38000400 | ((size & 3) << 30) | (((offset & 0x1FF) << 12)) | ((rn & 31) << 5) | (rt & 31)


def stp(rt: int, rt2: int, rn: int, imm: int = 0) -> int:
    return 0xA9000000 | (((imm // 8) & 0x7F) << 15) | ((rt2 & 31) << 10) | ((rn & 31) << 5) | (rt & 31)


def svc() -> int:
    return 0xD4000001


def hlt() -> int:
    return 0xD4400000


def branch_from(src: int, dst: int) -> int:
    return 0x14000000 | (((dst - src) // 4) & 0x03FFFFFF)


def uart_bytes(data: bytes, *, halt: bool = True) -> list[int]:
    ops = mov_abs(0, UART)
    for byte in data:
        ops.append(movz(1, byte, 0, x=False))
        ops.append(str_(1, 0, 0))
    if halt:
        ops.append(hlt())
    return ops


def syscall_write_byte(byte: int) -> list[int]:
    # Write one byte from RAM address 0x2000 through the teaching syscall.
    return mov_abs(0, 1) + mov_abs(1, 0x2000) + mov_abs(2, 1) + mov_abs(8, 64) + [svc()]


def exit0() -> list[int]:
    return mov_abs(0, 0) + mov_abs(8, 93) + [svc()]


def put(buf: bytearray, offset: int, data: bytes) -> None:
    end = offset + len(data)
    if end > len(buf):
        buf.extend(b"\0" * (end - len(buf)))
    buf[offset:end] = data


def phdr(flags: int, offset: int, vaddr: int, data: bytes, memsz: int | None = None) -> bytes:
    if memsz is None:
        memsz = len(data)
    return b"".join([u32(PT_LOAD), u32(flags), u64(offset), u64(vaddr), u64(vaddr), u64(len(data)), u64(memsz), u64(0x1000)])


def elf(path: Path, entry: int, segments: list[dict[str, object]]) -> None:
    buf = bytearray(ELF64_EHDR_SIZE + ELF64_PHDR_SIZE * len(segments))
    ident = bytearray(16)
    ident[:4] = b"\x7fELF"
    ident[4] = 2
    ident[5] = 1
    ident[6] = 1
    put(buf, 0, bytes(ident))
    put(buf, 16, u16(ET_EXEC))
    put(buf, 18, u16(EM_AARCH64))
    put(buf, 20, u32(1))
    put(buf, 24, u64(entry))
    put(buf, 32, u64(ELF64_EHDR_SIZE))
    put(buf, 52, u16(ELF64_EHDR_SIZE))
    put(buf, 54, u16(ELF64_PHDR_SIZE))
    put(buf, 56, u16(len(segments)))
    for i, seg in enumerate(segments):
        data = bytes(seg.get("data", b""))
        offset = int(seg["offset"])
        put(buf, ELF64_EHDR_SIZE + i * ELF64_PHDR_SIZE, phdr(int(seg["flags"]), offset, int(seg["vaddr"]), data, seg.get("memsz")))
        put(buf, offset, data)
    path.write_bytes(bytes(buf))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("tests/v1_3/tmp"))
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)

    emit(out / "uart_hi.bin", uart_bytes(b"Hi\n"))
    emit(out / "uart_no_svc.bin", uart_bytes(b"M"))
    # UART A, syscall writes B from data at 0x2000, UART C, then exit.
    mixed = uart_bytes(b"A", halt=False) + syscall_write_byte(ord("B")) + uart_bytes(b"C", halt=False) + exit0()
    emit(out / "uart_syscall_order.bin", mixed)
    emit(out / "uart_nul_high.bin", uart_bytes(bytes([0x00, ord("X"), 0xFF])))
    emit(out / "uart_large.bin", uart_bytes((b"0123456789abcdef" * 64)))
    emit(out / "uart_after_hlt.bin", uart_bytes(b"A")[:-1] + [hlt()] + uart_bytes(b"Z", halt=False))
    emit(out / "uart_bad_offset.bin", mov_abs(0, UART) + [movz(1, ord("!"), 0, x=False), str_(1, 0, 0, 0x10), hlt()])
    emit(out / "uart_word_width.bin", mov_abs(0, UART) + [movz(1, 0x4142, 0, x=False), str_(1, 0, 2), hlt()])
    emit(out / "uart_half_width.bin", mov_abs(0, UART) + [movz(1, 0x4142, 0, x=False), str_(1, 0, 1), hlt()])
    emit(out / "uart_cross_register.bin", mov_abs(0, UART + 3) + [movz(1, 0x4142, 0, x=False), str_(1, 0, 1), hlt()])
    emit(out / "uart_status_read.bin", mov_abs(0, UART) + [ldr(1, 0, 2, 4), hlt()])
    emit(out / "uart_data_read.bin", mov_abs(0, UART) + [ldr(1, 0, 0), hlt()])

    emit(out / "timer_read.bin", mov_abs(0, TIMER) + [ldr(1, 0, 2, 0), ldr(2, 0, 2, 4), hlt()])
    emit(out / "timer_reset.bin", mov_abs(0, TIMER) + [ldr(1, 0, 2, 0), str_(31, 0, 2, 8), ldr(2, 0, 2, 0), hlt()])
    emit(out / "timer_invalid.bin", mov_abs(0, TIMER) + [ldr(1, 0, 2, 0x20), hlt()])
    emit(out / "timer_unaligned.bin", mov_abs(0, TIMER + 1) + [ldr(1, 0, 2, 0), hlt()])
    emit(out / "timer_byte.bin", mov_abs(0, TIMER) + [ldr(1, 0, 0, 0), hlt()])

    emit(out / "random_read.bin", mov_abs(0, RANDOM) + [ldr(1, 0, 2, 0), ldr(2, 0, 2, 0), hlt()])
    emit(out / "random_seed.bin", mov_abs(0, RANDOM) + movw(1, 0xABCD1234) + [str_(1, 0, 2, 4), ldr(2, 0, 2, 0), hlt()])
    emit(out / "random_invalid.bin", mov_abs(0, RANDOM) + [ldr(1, 0, 2, 0x20), hlt()])
    emit(out / "random_half.bin", mov_abs(0, RANDOM) + [ldr(1, 0, 1, 0), hlt()])

    emit(out / "edge_device_boundary.bin", mov_abs(0, UART + 0xFFF) + [movz(1, 0x41, 0, x=False), str_(1, 0, 1), hlt()])
    emit(out / "entry_device_branch.bin", [branch_from(LOAD, UART), hlt()])
    emit(out / "stp_device.bin", mov_abs(0, UART) + [stp(1, 2, 0), hlt()])
    emit(out / "pre_index_timer.bin", mov_abs(0, TIMER - 4) + [ldr_pre(1, 0, 2, 4), hlt()])
    emit(out / "post_index_uart.bin", mov_abs(0, UART) + [movz(1, ord("P"), 0, x=False), str_post(1, 0, 0, 4), hlt()])

    elf(out / "uart_elf.elf", LOAD, [
        {"offset": 0x100, "vaddr": LOAD, "flags": PF_R | PF_X, "data": code(*uart_bytes(b"E", halt=True))},
    ])
    elf(out / "uart_constant.elf", LOAD, [
        {"offset": 0x100, "vaddr": LOAD, "flags": PF_R | PF_X, "data": code(*uart_bytes(b"K", halt=True))},
        {"offset": 0x200, "vaddr": 0x3000, "flags": PF_R | PF_W, "data": u64(UART)},
    ])
    elf(out / "overlap_device.elf", LOAD, [
        {"offset": 0x100, "vaddr": LOAD, "flags": PF_R | PF_X, "data": code(hlt())},
        {"offset": 0x200, "vaddr": UART, "flags": PF_R | PF_W, "data": b"BAD"},
    ])
    elf(out / "entry_device.elf", UART, [
        {"offset": 0x100, "vaddr": LOAD, "flags": PF_R | PF_X, "data": code(hlt())},
    ])
    # Data for mixed syscall fixture: raw loader maps entire binary at LOAD; place B at address 0x2000.
    path = out / "uart_syscall_order.bin"
    data = bytearray(path.read_bytes())
    data.extend(b"\0" * (0x2000 - LOAD - len(data)))
    data.append(ord("B"))
    path.write_bytes(bytes(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

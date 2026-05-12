#!/usr/bin/env python3
"""Generate deterministic v1.2 virtual-memory test fixtures."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)


def code(*ops: int) -> bytes:
    return b"".join(u32(op) for op in ops)


def movz_x(rd: int, imm16: int, shift: int = 0) -> int:
    hw = (shift // 16) & 0x3
    return 0xD2800000 | (hw << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)


def movk_x(rd: int, imm16: int, shift: int = 0) -> int:
    hw = (shift // 16) & 0x3
    return 0xF2800000 | (hw << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)


def mov_abs_x(rd: int, value: int) -> bytes:
    lower = value & 0xFFFF
    upper = (value >> 16) & 0xFFFF
    return code(movz_x(rd, lower), movk_x(rd, upper, 16))


def svc0() -> int:
    return 0xD4000001


def hlt0() -> int:
    return 0xD4400000


def branch_from(src: int, dst: int) -> int:
    return 0x14000000 | (((dst - src) // 4) & 0x03FFFFFF)


def ldr_x(rt: int, rn: int, byte_offset: int = 0) -> int:
    return 0xF9400000 | (((byte_offset // 8) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def str_x(rt: int, rn: int, byte_offset: int = 0) -> int:
    return 0xF9000000 | (((byte_offset // 8) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def exit_status(status: int) -> bytes:
    return code(movz_x(0, status), movz_x(8, 93), svc0())


def write_raw_fixtures(out: Path) -> None:
    (out / "simple_raw.bin").write_bytes(exit_status(0))
    (out / "exit7_raw.bin").write_bytes(exit_status(7))
    (out / "write_code_page.bin").write_bytes(
        mov_abs_x(1, 0x1000) + code(movz_x(0, 0x41), str_x(0, 1), hlt0())
    )
    (out / "execute_unmapped.bin").write_bytes(code(branch_from(0x1000, 0x2000), hlt0()))
    (out / "read_unmapped.bin").write_bytes(mov_abs_x(1, 0x9000) + code(ldr_x(0, 1), hlt0()))
    (out / "write_stack_guard.bin").write_bytes(
        mov_abs_x(1, 0xEF000) + code(movz_x(0, 0x44), str_x(0, 1), hlt0())
    )
    (out / "execute_stack.bin").write_bytes(code(branch_from(0x1000, 0xF0000), hlt0()))


ELF64_EHDR_SIZE = 64
ELF64_PHDR_SIZE = 56
PF_X = 1
PF_W = 2
PF_R = 4
PT_LOAD = 1
ET_EXEC = 2
EM_AARCH64 = 183


def put(buf: bytearray, offset: int, data: bytes) -> None:
    end = offset + len(data)
    if end > len(buf):
        buf.extend(b"\0" * (end - len(buf)))
    buf[offset:end] = data


def phdr(p_type: int, flags: int, offset: int, vaddr: int, filesz: int, memsz: int, align: int = 0x1000) -> bytes:
    return b"".join(
        [
            u32(p_type),
            u32(flags),
            u64(offset),
            u64(vaddr),
            u64(vaddr),
            u64(filesz),
            u64(memsz),
            u64(align),
        ]
    )


def elf(path: Path, entry: int, segments: list[dict[str, object]]) -> None:
    buf = bytearray(ELF64_EHDR_SIZE + ELF64_PHDR_SIZE * len(segments))
    ident = bytearray(16)
    ident[0:4] = b"\x7fELF"
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
        put(buf, ELF64_EHDR_SIZE + i * ELF64_PHDR_SIZE, phdr(PT_LOAD, int(seg["flags"]), offset, int(seg["vaddr"]), len(data), int(seg.get("memsz", len(data)))))
        put(buf, offset, data)
    path.write_bytes(bytes(buf))


def write_elf_fixtures(out: Path) -> None:
    text = exit_status(0)
    elf(
        out / "text_data.elf",
        0x1000,
        [
            {"offset": 0x100, "vaddr": 0x1000, "flags": PF_R | PF_X, "data": text},
            {"offset": 0x200, "vaddr": 0x3000, "flags": PF_R | PF_W, "data": b"DATA", "memsz": 12},
        ],
    )
    elf(out / "data_entry.elf", 0x3000, [{"offset": 0x100, "vaddr": 0x3000, "flags": PF_R | PF_W, "data": code(hlt0())}])
    elf(
        out / "zero_permission_data.elf",
        0x1000,
        [
            {"offset": 0x100, "vaddr": 0x1000, "flags": PF_R | PF_X, "data": text},
            {"offset": 0x200, "vaddr": 0x5000, "flags": 0, "data": b"X"},
        ],
    )
    elf(out / "write_text.elf", 0x1000, [{"offset": 0x100, "vaddr": 0x1000, "flags": PF_R | PF_X, "data": mov_abs_x(1, 0x1000) + code(movz_x(0, 0x41), str_x(0, 1), hlt0())}])
    # A zero-memory segment should be accepted as a no-op segment by the loader if present after executable text.
    elf(
        out / "zero_mem_segment.elf",
        0x1000,
        [
            {"offset": 0x100, "vaddr": 0x1000, "flags": PF_R | PF_X, "data": text},
            {"offset": 0x200, "vaddr": 0x5000, "flags": PF_R | PF_W, "data": b"", "memsz": 0},
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("tests/v1_2/tmp"))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_raw_fixtures(args.output_dir)
    write_elf_fixtures(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

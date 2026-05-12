#!/usr/bin/env python3
"""Generate deterministic v1.3 memory-mapped-device raw fixtures."""

from __future__ import annotations

import argparse
from pathlib import Path
from struct import pack

UART_BASE = 0x09000000
TIMER_BASE = 0x09010000
RANDOM_BASE = 0x09020000


def emit(path: Path, words: list[int]) -> None:
    path.write_bytes(b"".join(pack("<I", word) for word in words))


def movz(rd: int, imm16: int, shift: int = 0, *, is64: bool = True) -> int:
    hw = shift // 16
    base = 0xD2800000 if is64 else 0x52800000
    return base | ((hw & 0x3) << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)


def movk(rd: int, imm16: int, shift: int = 0, *, is64: bool = True) -> int:
    hw = shift // 16
    base = 0xF2800000 if is64 else 0x72800000
    return base | ((hw & 0x3) << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)


def mov_imm64(rd: int, value: int) -> list[int]:
    words = [movz(rd, value & 0xFFFF, 0)]
    for shift in (16, 32, 48):
        part = (value >> shift) & 0xFFFF
        if part:
            words.append(movk(rd, part, shift))
    return words


def strb(rt: int, rn: int, imm: int = 0) -> int:
    return 0x39000000 | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def str_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xB9000000 | (((imm // 4) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def ldr_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xB9400000 | (((imm // 4) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def hlt() -> int:
    return 0xD4400000


def uart_hello() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, UART_BASE))
    for byte in b"hello, v1.3 mmio!\n":
        words.append(movz(1, byte, 0, is64=False))
        words.append(strb(1, 0))
    words.append(hlt())
    return words


def timer_read() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, TIMER_BASE))
    words.append(ldr_w(1, 0, 0x00))
    words.append(ldr_w(2, 0, 0x04))
    words.append(str_w(31, 0, 0x08))  # WZR write resets timer.
    words.append(ldr_w(3, 0, 0x00))
    words.append(hlt())
    return words


def random_read() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, RANDOM_BASE))
    words.append(movz(1, 0x1234, 0, is64=False))
    words.append(movk(1, 0xABCD, 16, is64=False))
    words.append(str_w(1, 0, 0x04))
    words.append(ldr_w(2, 0, 0x00))
    words.append(hlt())
    return words


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="examples/v1_3")
    args = parser.parse_args()
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    emit(out / "mmio_uart_hello.bin", uart_hello())
    emit(out / "mmio_timer_read.bin", timer_read())
    emit(out / "mmio_random_read.bin", random_read())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

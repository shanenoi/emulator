#!/usr/bin/env python3
"""Generate deterministic v1.4 exception/trap raw fixtures."""

from __future__ import annotations

import argparse
from pathlib import Path
from struct import pack

LOAD_ADDRESS = 0x1000
EXCEPTION_BASE = 0x09030000
VECTOR = 0x1080


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


def add_imm(rd: int, rn: int, imm: int, *, is64: bool = True) -> int:
    return (0x91000000 if is64 else 0x11000000) | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F)


def str_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xB9000000 | (((imm // 4) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def str_x(rt: int, rn: int, imm: int = 0) -> int:
    return 0xF9000000 | (((imm // 8) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def ldr_w(rt: int, rn: int, imm: int = 0) -> int:
    return 0xB9400000 | (((imm // 4) & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def nop() -> int:
    return 0xD503201F


def hlt() -> int:
    return 0xD4400000


def brk(imm: int = 0) -> int:
    return 0xD4200000 | ((imm & 0xFFFF) << 5)


def eret() -> int:
    return 0xD69F03E0


def pad_to(words: list[int], address: int) -> None:
    target_words = (address - LOAD_ADDRESS) // 4
    while len(words) < target_words:
        words.append(nop())


def handler_return() -> list[int]:
    return [eret()]


def handler_skip_fault() -> list[int]:
    return [add_imm(3, 3, 4), eret()]


def handler_disable_timer() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, EXCEPTION_BASE))
    words.append(movz(6, 0, 0))
    words.append(str_x(6, 0, 0x10))
    words.append(eret())
    return words


def cli_handled_brk() -> list[int]:
    words = [brk(0x14), hlt()]
    pad_to(words, VECTOR)
    words.extend(handler_return())
    return words


def mmio_handled_brk() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, EXCEPTION_BASE))
    words.extend(mov_imm64(1, VECTOR))
    words.append(str_x(1, 0, 0x00))
    words.append(movz(2, 0x3, 0, is64=False))  # vector enable + interrupts enable
    words.append(str_w(2, 0, 0x08))
    words.append(brk(0x15))
    words.append(hlt())
    pad_to(words, VECTOR)
    words.extend(handler_return())
    return words


def mmio_skip_device_fault() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, EXCEPTION_BASE))
    words.extend(mov_imm64(1, VECTOR))
    words.append(str_x(1, 0, 0x00))
    words.append(movz(2, 0x3, 0, is64=False))
    words.append(str_w(2, 0, 0x08))
    words.extend(mov_imm64(4, 0x09000000))  # UART DATA is write-only.
    words.append(ldr_w(5, 4, 0x00))
    words.append(hlt())
    pad_to(words, VECTOR)
    words.extend(handler_skip_fault())
    return words


def mmio_timer_once() -> list[int]:
    words: list[int] = []
    words.extend(mov_imm64(0, EXCEPTION_BASE))
    words.extend(mov_imm64(1, VECTOR))
    words.append(str_x(1, 0, 0x00))
    words.append(movz(2, 0x3, 0, is64=False))
    words.append(str_w(2, 0, 0x08))
    words.append(movz(6, 12, 0))
    words.append(str_x(6, 0, 0x10))
    for _ in range(16):
        words.append(nop())
    words.append(hlt())
    pad_to(words, VECTOR)
    words.extend(handler_disable_timer())
    return words


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="examples/v1_4")
    args = parser.parse_args()
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    emit(out / "cli_handled_brk.bin", cli_handled_brk())
    emit(out / "mmio_handled_brk.bin", mmio_handled_brk())
    emit(out / "mmio_skip_device_fault.bin", mmio_skip_device_fault())
    emit(out / "mmio_timer_once.bin", mmio_timer_once())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Generate deterministic v1.4 exception test fixtures."""
from __future__ import annotations

import argparse
from pathlib import Path
from struct import pack

LOAD = 0x1000
VECTOR = 0x1080
EXC = 0x09030000
UART = 0x09000000

def emit(path: Path, words: list[int]) -> None:
    path.write_bytes(b''.join(pack('<I', w & 0xffffffff) for w in words))

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
    w += [nop()] * 8
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

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', default='tests/v1_4/tmp')
    args = parser.parse_args()
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in FIXTURES.items():
        emit(out / name, fn())
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

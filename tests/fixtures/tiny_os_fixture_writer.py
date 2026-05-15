#!/usr/bin/env python3
"""Generate deterministic raw v1.6 Tiny OS Lab test fixtures."""

from __future__ import annotations

import argparse
import pathlib
import struct

LOAD = 0x1000
HLT = 0xD4400000
NOP = 0xD503201F
B_SELF = 0x14000000
BAD = 0xFFFFFFFF


def insn(*words: int) -> bytes:
    return b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in words)


def brk(imm: int) -> int:
    return 0xD4200000 | ((imm & 0xFFFF) << 5)


def movz_x(reg: int, imm: int) -> int:
    return 0xD2800000 | ((imm & 0xFFFF) << 5) | (reg & 0x1F)


def movk_x(reg: int, imm: int, shift: int) -> int:
    return 0xF2800000 | (((shift // 16) & 0x3) << 21) | ((imm & 0xFFFF) << 5) | (reg & 0x1F)


def load_imm_x(reg: int, value: int) -> list[int]:
    words = [movz_x(reg, value & 0xFFFF)]
    for shift in (16, 32, 48):
        part = (value >> shift) & 0xFFFF
        if part:
            words.append(movk_x(reg, part, shift))
    return words


def sub_imm_x(rd: int, rn: int, imm: int) -> int:
    return 0xD1000000 | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F)


def add_imm_x(rd: int, rn: int, imm: int) -> int:
    return 0x91000000 | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F)


TRAP_START = brk(0x154)
TRAP_SERVICE = brk(0x160)
SVC_CREATE = 1
SVC_YIELD = 2
SVC_EXIT = 3
SVC_SLEEP = 4
SVC_GET_ID = 5
SVC_SEND = 7
SVC_RECV = 8
SVC_CONSOLE = 9
SVC_PANIC = 10


def write_fixture(out_dir: pathlib.Path, name: str, words: list[int], data: bytes = b"") -> None:
    (out_dir / name).write_bytes(insn(*words) + data)


def create(entry: int, arg0: int = 0, flags: int = 0) -> list[int]:
    return [movz_x(8, SVC_CREATE), *load_imm_x(0, entry), movz_x(1, 0), movz_x(2, 0), movz_x(3, arg0), movz_x(4, flags), TRAP_SERVICE]


def exit_with(value: int) -> list[int]:
    return [movz_x(8, SVC_EXIT), movz_x(0, value), TRAP_SERVICE]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)

    task = LOAD + 0x24
    write_fixture(out, "guest_create_task_exit.bin", [*create(task, 7), TRAP_START, HLT, *exit_with(7)])

    a = LOAD + 0x5C
    b = LOAD + 0x70
    c = LOAD + 0x84
    write_fixture(out, "three_guest_tasks_yield.bin", [*create(a), *create(b), *create(c), TRAP_START, HLT,
                                                       movz_x(8, SVC_YIELD), TRAP_SERVICE, *exit_with(1), NOP,
                                                       movz_x(8, SVC_YIELD), TRAP_SERVICE, *exit_with(2), NOP,
                                                       *exit_with(3)])

    # Sender id 0 queues OK to receiver id 1; receiver prints OK.
    sender = LOAD + 0x40
    receiver = LOAD + 0x60
    src = LOAD + 0x90
    write_fixture(out, "mailbox_ping_pong.bin", [*create(sender), *create(receiver), TRAP_START, HLT,
                                                 movz_x(8, SVC_SEND), movz_x(0, 1), *load_imm_x(1, src), movz_x(2, 2), TRAP_SERVICE,
                                                 *exit_with(0),
                                                 add_imm_x(29, 31, 0), movz_x(8, SVC_RECV), sub_imm_x(0, 29, 16), movz_x(1, 2), TRAP_SERVICE,
                                                 movz_x(8, SVC_CONSOLE), sub_imm_x(0, 29, 16), movz_x(1, 2), TRAP_SERVICE,
                                                 *exit_with(0)], b"OK" + b"\0" * 6)

    write_fixture(out, "invalid_task_create_flags.bin", [*create(LOAD + 0x30, 0, 1), HLT])
    write_fixture(out, "unknown_service_then_exit.bin", [movz_x(8, 99), TRAP_SERVICE, HLT])
    write_fixture(out, "task_fault_then_exit.bin", [*create(LOAD + 0x40), *create(LOAD + 0x44), TRAP_START, HLT, BAD, *exit_with(0)])
    write_fixture(out, "panic_service.bin", [movz_x(8, SVC_PANIC), movz_x(0, 0x44), TRAP_SERVICE])
    write_fixture(out, "infinite_task.bin", [*create(LOAD + 0x24), TRAP_START, HLT, B_SELF])
    write_fixture(out, "sleep_deadlock.bin", [*create(LOAD + 0x24), TRAP_START, HLT, movz_x(8, SVC_SLEEP), movz_x(0, 1), TRAP_SERVICE])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

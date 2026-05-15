#!/usr/bin/env python3
"""Generate deterministic raw v1.6 Tiny OS Lab fixtures."""

from __future__ import annotations

import argparse
import pathlib
import struct


LOAD = 0x1000
HLT = 0xD4400000
NOP = 0xD503201F


def insn(*words: int) -> bytes:
    return b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in words)


def brk(imm: int) -> int:
    return 0xD4200000 | ((imm & 0xFFFF) << 5)


def movz_x(reg: int, imm: int) -> int:
    return 0xD2800000 | ((imm & 0xFFFF) << 5) | (reg & 0x1F)


def movk_x(reg: int, imm: int, shift: int) -> int:
    return 0xF2800000 | (((shift // 16) & 0x3) << 21) | ((imm & 0xFFFF) << 5) | (reg & 0x1F)


def sub_imm_x(rd: int, rn: int, imm: int) -> int:
    return 0xD1000000 | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F)


def add_imm_x(rd: int, rn: int, imm: int) -> int:
    return 0x91000000 | ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F)


def load_imm_x(reg: int, value: int) -> list[int]:
    words = [movz_x(reg, value & 0xFFFF)]
    for shift in (16, 32, 48):
        part = (value >> shift) & 0xFFFF
        if part:
            words.append(movk_x(reg, part, shift))
    return words


TRAP_START = brk(0x154)
TRAP_SERVICE = brk(0x160)

SVC_CREATE = 1
SVC_EXIT = 3
SVC_GET_ID = 5
SVC_SEND = 7
SVC_RECV = 8
SVC_CONSOLE = 9


def write_fixture(out_dir: pathlib.Path, name: str, words: list[int], data: bytes = b"") -> None:
    (out_dir / name).write_bytes(insn(*words) + data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)

    # Kernel creates one task with an automatic stack and arg0=7, then starts
    # the scheduler. The task exits with status 7 through the v1.6 service trap.
    task_entry = LOAD + 0x24
    write_fixture(
        out,
        "guest_create_task_exit.bin",
        [
            movz_x(8, SVC_CREATE),
            movz_x(0, task_entry),
            movz_x(1, 0),
            movz_x(2, 0),
            movz_x(3, 7),
            movz_x(4, 0),
            TRAP_SERVICE,
            TRAP_START,
            HLT,
            movz_x(8, SVC_EXIT),
            TRAP_SERVICE,
        ],
    )

    # A task asks for its current task ID and exits with that value. The first
    # guest-created task gets deterministic task ID 0.
    task_entry = LOAD + 0x24
    write_fixture(
        out,
        "guest_get_id.bin",
        [
            movz_x(8, SVC_CREATE),
            movz_x(0, task_entry),
            movz_x(1, 0),
            movz_x(2, 0),
            movz_x(3, 0),
            movz_x(4, 0),
            TRAP_SERVICE,
            TRAP_START,
            HLT,
            movz_x(8, SVC_GET_ID),
            TRAP_SERVICE,
            movz_x(8, SVC_EXIT),
            TRAP_SERVICE,
        ],
    )

    # Kernel creates sender task id 0 and receiver task id 1. Sender queues
    # "OK" to receiver and exits. Receiver reads it, prints it, then exits.
    sender_entry = LOAD + 0x40
    receiver_entry = LOAD + 0x60
    source_addr = LOAD + 0x90
    write_fixture(
        out,
        "mailbox_ping_pong.bin",
        [
            movz_x(8, SVC_CREATE), movz_x(0, sender_entry), movz_x(1, 0), movz_x(2, 0), movz_x(3, 0), movz_x(4, 0), TRAP_SERVICE,
            movz_x(8, SVC_CREATE), movz_x(0, receiver_entry), movz_x(1, 0), movz_x(2, 0), movz_x(3, 0), movz_x(4, 0), TRAP_SERVICE,
            TRAP_START, HLT,
            movz_x(8, SVC_SEND), movz_x(0, 1), movz_x(1, source_addr), movz_x(2, 2), TRAP_SERVICE,
            movz_x(8, SVC_EXIT), movz_x(0, 0), TRAP_SERVICE,
            add_imm_x(29, 31, 0),
            movz_x(8, SVC_RECV), sub_imm_x(0, 29, 16), movz_x(1, 2), TRAP_SERVICE,
            movz_x(8, SVC_CONSOLE), sub_imm_x(0, 29, 16), movz_x(1, 2), TRAP_SERVICE,
            movz_x(8, SVC_EXIT), movz_x(0, 0), TRAP_SERVICE,
        ],
        b"OK" + b"\x00" * 6,
    )

    # Unsupported flags are rejected with BAD_FLAGS; the kernel then exits with
    # status 0 because this fixture is for observing the service return path.
    write_fixture(
        out,
        "invalid_task_create_flags.bin",
        [
            movz_x(8, SVC_CREATE),
            movz_x(0, LOAD + 0x30),
            movz_x(1, 0),
            movz_x(2, 0),
            movz_x(3, 0),
            movz_x(4, 1),
            TRAP_SERVICE,
            HLT,
        ],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

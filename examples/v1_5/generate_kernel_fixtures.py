#!/usr/bin/env python3
"""Generate deterministic raw v1.5 toy-kernel teaching fixtures.

The generated binaries are intentionally tiny and source-derived. They are for
manual smoke testing and future test integration; generated .bin files do not
need to be committed.
"""

from __future__ import annotations

import argparse
import pathlib
import struct


def insn(*words: int) -> bytes:
    return b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in words)


def brk(imm: int) -> int:
    return 0xD4200000 | ((imm & 0xFFFF) << 5)


HLT = 0xD4400000
NOP = 0xD503201F
MOVZ_X0_0 = 0xD2800000
MOVZ_X0_1 = 0xD2800020
MOVZ_X0_2 = 0xD2800040
MOVZ_X0_7 = 0xD28000E0

TRAP_YIELD = brk(0x150)
TRAP_EXIT = brk(0x151)
TRAP_PANIC = brk(0x152)
TRAP_START = brk(0x154)
TRAP_SLEEP = brk(0x155)


def write_fixture(out_dir: pathlib.Path, name: str, words: list[int]) -> None:
    path = out_dir / name
    path.write_bytes(insn(*words))
    print(f"wrote {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", type=pathlib.Path)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    # entry 0x1000 starts tasks, task 0 at 0x1008 exits with status 7
    write_fixture(args.out_dir, "single_task_exit.bin", [TRAP_START, HLT, MOVZ_X0_7, TRAP_EXIT])

    # task 0 at 0x1008 yields once then exits; task 1 at 0x1014 exits.
    write_fixture(
        args.out_dir,
        "two_task_yield.bin",
        [TRAP_START, HLT, TRAP_YIELD, MOVZ_X0_1, TRAP_EXIT, MOVZ_X0_2, TRAP_EXIT],
    )

    # task 0 at 0x1008 sleeps one toy timer tick; task 1 has enough ordinary
    # instructions to let the instruction-count timer wake task 0 again.
    write_fixture(
        args.out_dir,
        "sleep_then_exit.bin",
        [TRAP_START, HLT, MOVZ_X0_1, TRAP_SLEEP, TRAP_EXIT, NOP, NOP, MOVZ_X0_2, TRAP_EXIT],
    )

    # task 0 at 0x1008 faults on an unsupported instruction; task 1 exits.
    write_fixture(args.out_dir, "task_fault_then_exit.bin", [TRAP_START, HLT, 0xFFFFFFFF, MOVZ_X0_0, TRAP_EXIT])

    # kernel panics before starting tasks.
    write_fixture(args.out_dir, "kernel_panic.bin", [MOVZ_X0_7, TRAP_PANIC])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

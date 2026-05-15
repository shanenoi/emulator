#!/usr/bin/env python3
"""Generate deterministic raw v1.5 toy-kernel teaching fixtures."""

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
ERET = 0xD69F03E0
B_SELF = 0x14000000
MOVZ_X0_0 = 0xD2800000
MOVZ_X0_1 = 0xD2800020
MOVZ_X0_2 = 0xD2800040
MOVZ_X0_3 = 0xD2800060
MOVZ_X0_7 = 0xD28000E0
MOVZ_X0_9 = 0xD2800120
MOVZ_X1_4 = 0xD2800081
MOVZ_X2_4 = 0xD2800082
TRAP_YIELD = brk(0x150)
TRAP_EXIT = brk(0x151)
TRAP_PANIC = brk(0x152)
TRAP_CONSOLE = brk(0x153)
TRAP_START = brk(0x154)
TRAP_SLEEP = brk(0x155)
BAD_INSN = 0xFFFFFFFF


def write_fixture(out_dir: pathlib.Path, name: str, words: list[int], data: bytes = b"") -> None:
    path = out_dir / name
    path.write_bytes(insn(*words) + data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    # entry 0x1000 starts tasks, task 0 at 0x1008 exits with status 7.
    write_fixture(args.output_dir, "single_task_exit.bin", [TRAP_START, HLT, MOVZ_X0_7, TRAP_EXIT])

    # task 0 at 0x1008 yields once then exits; task 1 at 0x1014 exits.
    write_fixture(
        args.output_dir,
        "two_task_yield.bin",
        [TRAP_START, HLT, TRAP_YIELD, MOVZ_X0_1, TRAP_EXIT, MOVZ_X0_2, TRAP_EXIT],
    )

    # task 0 sleeps one toy timer tick; task 1 executes ordinary instructions so
    # the deterministic instruction-count timer can wake task 0.
    write_fixture(
        args.output_dir,
        "sleep_then_exit.bin",
        [TRAP_START, HLT, MOVZ_X0_1, TRAP_SLEEP, MOVZ_X0_9, TRAP_EXIT, NOP, NOP, MOVZ_X0_2, TRAP_EXIT],
    )

    # task 0 faults; task 1 exits; the kernel completes with fault status 71.
    write_fixture(args.output_dir, "task_fault_then_exit.bin", [TRAP_START, HLT, BAD_INSN, MOVZ_X0_3, TRAP_EXIT])

    # task 0 executes ERET outside an active exception; task 1 still exits.
    write_fixture(args.output_dir, "eret_task_fault_then_exit.bin", [TRAP_START, HLT, ERET, MOVZ_X0_3, TRAP_EXIT])

    # task 0 and task 1 yield before exiting; task 2 exits immediately.
    write_fixture(
        args.output_dir,
        "three_task_round_robin.bin",
        [TRAP_START, HLT, TRAP_YIELD, MOVZ_X0_1, TRAP_EXIT, NOP, TRAP_YIELD, MOVZ_X0_2, TRAP_EXIT, NOP, MOVZ_X0_3, TRAP_EXIT],
    )

    # kernel panics before starting tasks; the CLI maps this to status 70.
    write_fixture(args.output_dir, "kernel_panic.bin", [MOVZ_X0_7, TRAP_PANIC])

    # kernel writes KERN through the toy console then starts a normal task.
    # MOVZ_X0_1 encodes x0=1? for console we need buffer address, so use raw movz x0, 0x101c.
    movz_x0_101c = 0xD2800000 | (0x101c << 5)
    write_fixture(args.output_dir, "console_write.bin", [movz_x0_101c, MOVZ_X1_4, TRAP_CONSOLE, TRAP_START, HLT, MOVZ_X0_0, TRAP_EXIT], b"KERN")

    # no task can advance time, so sleep deadlocks deterministically.
    write_fixture(args.output_dir, "sleep_deadlock.bin", [TRAP_START, HLT, MOVZ_X0_1, TRAP_SLEEP])

    # task loops forever for instruction-limit coverage.
    write_fixture(args.output_dir, "infinite_task.bin", [TRAP_START, HLT, B_SELF])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Generate deterministic v1.2 virtual-memory example fixtures.

The fixtures intentionally stay tiny and raw so learners can inspect the
permission behavior without needing an external assembler or linker.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def insn(*values: int) -> bytes:
    return b"".join(u32(value) for value in values)


def movz_x(rd: int, imm16: int) -> int:
    return 0xD2800000 | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)


def svc0() -> int:
    return 0xD4000001


def hlt0() -> int:
    return 0xD4400000


def str_unsigned_x(rt: int, rn: int, byte_offset: int = 0) -> int:
    # 64-bit STR (unsigned immediate). The immediate is scaled by 8 bytes.
    imm12 = (byte_offset // 8) & 0xFFF
    return 0xF9000000 | (imm12 << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F)


def branch_absolute_from_0x1000(target: int) -> int:
    pc = 0x1000
    offset = (target - pc) // 4
    return 0x14000000 | (offset & 0x03FFFFFF)


def exit_status(status: int) -> bytes:
    return insn(movz_x(0, status), movz_x(8, 93), svc0())


def build_fixtures(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    # Successful baseline: raw code is mapped readable/executable, then exits 0.
    (output_dir / "simple_raw.bin").write_bytes(exit_status(0))

    # Raw program tries to write into its own r-x code mapping at 0x1000.
    (output_dir / "write_code_page.bin").write_bytes(
        insn(
            movz_x(1, 0x1000),
            movz_x(0, 0x0041),
            str_unsigned_x(0, 1),
            hlt0(),
        )
    )

    # Raw program branches to 0x2000, which is not inside the exact raw mapping.
    (output_dir / "execute_unmapped.bin").write_bytes(insn(branch_absolute_from_0x1000(0x2000), hlt0()))

    # A small text file records the expected inspection shape for docs/manual use.
    (output_dir / "mapping_inspection.txt").write_text(
        "Expected v1.2 inspection highlights:\n"
        "- simple_raw.bin has a raw:program r-x mapping.\n"
        "- loaded programs have a stack rw- mapping.\n"
        "- the page below the stack is shown as stack-guard with --- permissions.\n"
        "- write_code_page.bin fails with a write permission fault.\n"
        "- execute_unmapped.bin fails with an unmapped execute fault.\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default="examples/v1_2", type=Path)
    args = parser.parse_args()
    build_fixtures(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
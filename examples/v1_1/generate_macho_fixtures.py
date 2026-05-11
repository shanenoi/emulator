#!/usr/bin/env python3
"""Generate deterministic tiny arm64 Mach-O fixtures for v1.1 examples."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

MACHO_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64_ALL = 0
MH_EXECUTE = 2
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x02
LC_MAIN = 0x80000028
VM_PROT_READ = 0x1
VM_PROT_WRITE = 0x2
VM_PROT_EXECUTE = 0x4
N_SECT = 0x0E

HEADER_SIZE = 32
SEGMENT_COMMAND_SIZE = 72
SYMTAB_COMMAND_SIZE = 24
MAIN_COMMAND_SIZE = 24
NLIST64_SIZE = 16

TEXT_VMADDR = 0x1000


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value)


def fixed_name(text: str, size: int = 16) -> bytes:
    raw = text.encode("ascii")
    if len(raw) > size:
        raise ValueError(f"name too long: {text}")
    return raw + b"\0" * (size - len(raw))


def insn(*values: int) -> bytes:
    return b"".join(u32(value) for value in values)


# Encodings copied from the existing raw v0.7 fixtures. Keeping them here makes
# Mach-O examples deterministic and independent of clang, ld64, or Xcode.
MOVZ_X0_0 = 0xD2800000
MOVZ_X0_1 = 0xD2800020
MOVZ_X0_7 = 0xD28000E0
MOVZ_X1_0X1020 = 0xD2820401
MOVZ_X2_13 = 0xD28001A2
MOVZ_X8_64 = 0xD2800808
MOVZ_X8_93 = 0xD2800BA8
SVC_0 = 0xD4000001


def with_symbol_table(commands_size: int, text: bytes, symbol_name: bytes) -> tuple[bytes, bytes, bytes]:
    fileoff = HEADER_SIZE + commands_size
    symoff = fileoff + len(text)
    stroff = symoff + NLIST64_SIZE
    string_table = b"\0" + symbol_name + b"\0"
    strx = 1
    nlist = (
        u32(strx)
        + bytes([N_SECT, 1, 0, 0])
        + u64(TEXT_VMADDR)
    )
    symtab = (
        u32(LC_SYMTAB)
        + u32(SYMTAB_COMMAND_SIZE)
        + u32(symoff)
        + u32(1)
        + u32(stroff)
        + u32(len(string_table))
    )
    return symtab, nlist, string_table


def make_macho(text: bytes, output: Path, *, symbol_name: bytes = b"_start", vmsize: int | None = None) -> None:
    ncmds = 3
    sizeofcmds = SEGMENT_COMMAND_SIZE + SYMTAB_COMMAND_SIZE + MAIN_COMMAND_SIZE
    fileoff = HEADER_SIZE + sizeofcmds
    if vmsize is None:
        vmsize = len(text)
    if vmsize < len(text):
        raise ValueError("vmsize must be >= initialized text size")

    segment = (
        u32(LC_SEGMENT_64)
        + u32(SEGMENT_COMMAND_SIZE)
        + fixed_name("__TEXT")
        + u64(TEXT_VMADDR)
        + u64(vmsize)
        + u64(fileoff)
        + u64(len(text))
        + u32(VM_PROT_READ | VM_PROT_EXECUTE)
        + u32(VM_PROT_READ | VM_PROT_EXECUTE)
        + u32(0)
        + u32(0)
    )
    symtab, nlist, string_table = with_symbol_table(sizeofcmds, text, symbol_name)
    main = u32(LC_MAIN) + u32(MAIN_COMMAND_SIZE) + u64(fileoff) + u64(0)
    header = (
        u32(MACHO_MAGIC_64)
        + u32(CPU_TYPE_ARM64)
        + u32(CPU_SUBTYPE_ARM64_ALL)
        + u32(MH_EXECUTE)
        + u32(ncmds)
        + u32(sizeofcmds)
        + u32(0)
        + u32(0)
    )
    image = header + segment + symtab + main + text + nlist + string_table
    output.write_bytes(image)


def build_fixtures(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    make_macho(
        insn(MOVZ_X0_7, MOVZ_X8_93, SVC_0),
        output_dir / "minimal_exit.macho",
        symbol_name=b"_minimal_exit",
    )
    hello_code = insn(
        MOVZ_X0_1,
        MOVZ_X1_0X1020,
        MOVZ_X2_13,
        MOVZ_X8_64,
        SVC_0,
        MOVZ_X0_0,
        MOVZ_X8_93,
        SVC_0,
    )
    hello_text = hello_code + b"\0" * (0x20 - len(hello_code)) + b"hello, v1.1!\n"
    make_macho(hello_text, output_dir / "hello.macho", symbol_name=b"_hello")
    make_macho(
        insn(MOVZ_X0_0, MOVZ_X8_93, SVC_0),
        output_dir / "zero_fill.macho",
        symbol_name=b"_zero_fill",
        vmsize=0x40,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default="examples/v1_1", type=Path)
    args = parser.parse_args()
    build_fixtures(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

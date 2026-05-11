#!/usr/bin/env python3
"""Deterministic Mach-O fixture writer used by v1.1 tests.

The fixtures intentionally model only the teaching profile supported by the
emulator: thin little-endian arm64 MH_EXECUTE images with LC_SEGMENT_64 and
LC_MAIN. Malformed helpers keep byte-level edge cases small and reproducible.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, field
from pathlib import Path

MACHO_MAGIC_64 = 0xFEEDFACF
MACHO_CIGAM_64 = 0xCFFAEDFE
MACHO_MAGIC_32 = 0xFEEDFACE
MACHO_FAT_MAGIC_LE_AS_READ_BY_LOADER = 0xBEBAFECA
CPU_TYPE_ARM64 = 0x0100000C
CPU_TYPE_X86_64 = 0x01000007
MH_EXECUTE = 2
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x02
LC_DYSYMTAB = 0x0B
LC_LOAD_DYLIB = 0x0C
LC_LOAD_DYLINKER = 0x0E
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
LC_MAIN = 0x80000028
LC_UNKNOWN_HARMLESS = 0x2A
VM_PROT_READ = 0x1
VM_PROT_WRITE = 0x2
VM_PROT_EXECUTE = 0x4
N_SECT = 0x0E

HEADER_SIZE = 32
LOAD_COMMAND_SIZE = 8
SEGMENT_COMMAND_SIZE = 72
SECTION_64_SIZE = 80
SYMTAB_COMMAND_SIZE = 24
DYSYMTAB_COMMAND_SIZE = 80
MAIN_COMMAND_SIZE = 24
NLIST64_SIZE = 16
TEXT_VMADDR = 0x1000
DATA_VMADDR = 0x3000

OP_HLT = 0xD4400000
OP_SVC_0 = 0xD4000001
OP_UNSUPPORTED = 0xFFFFFFFF


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)


def fixed_name(text: str, size: int = 16) -> bytes:
    raw = text.encode("ascii")
    if len(raw) > size:
        raise ValueError(f"name too long: {text}")
    return raw + b"\0" * (size - len(raw))


def insn(*ops: int) -> bytes:
    return b"".join(u32(op) for op in ops)


def movz(rd: int, imm16: int, shift: int = 0, is64: bool = True) -> int:
    return (0x80000000 if is64 else 0) | 0x52800000 | (((shift // 16) & 3) << 21) | ((imm16 & 0xFFFF) << 5) | (rd & 31)


def mov_addr(reg: int, address: int) -> list[int]:
    # Enough for the project fixtures, which stay below 64 KiB.
    return [movz(reg, address & 0xFFFF)]


def ldr_unsigned(rt: int, rn: int, imm12_scaled: int, is64: bool = True) -> int:
    return (0xF9400000 if is64 else 0xB9400000) | ((imm12_scaled & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31)


def branch(offset: int) -> int:
    return 0x14000000 | ((offset // 4) & 0x03FFFFFF)


def exit_code(status: int) -> bytes:
    return insn(movz(0, status), movz(8, 93), OP_SVC_0)


def write_stdout(addr: int, length: int, status: int = 0) -> bytes:
    return insn(movz(0, 1), *mov_addr(1, addr), movz(2, length), movz(8, 64), OP_SVC_0, movz(0, status), movz(8, 93), OP_SVC_0)


def write_stderr(addr: int, length: int, status: int = 0) -> bytes:
    return insn(movz(0, 2), *mov_addr(1, addr), movz(2, length), movz(8, 64), OP_SVC_0, movz(0, status), movz(8, 93), OP_SVC_0)


@dataclass
class Segment:
    name: str
    vmaddr: int
    data: bytes
    vmsize: int | None = None
    fileoff: int | None = None
    maxprot: int = VM_PROT_READ | VM_PROT_EXECUTE
    initprot: int = VM_PROT_READ | VM_PROT_EXECUTE
    nsects: int = 0
    cmdsize_extra: bytes = b""

    def mem_size(self) -> int:
        return len(self.data) if self.vmsize is None else self.vmsize


@dataclass
class MachOImage:
    segments: list[Segment]
    entry_segment: int = 0
    entry_delta: int = 0
    cputype: int = CPU_TYPE_ARM64
    filetype: int = MH_EXECUTE
    include_main: bool = True
    duplicate_main: bool = False
    include_symtab: bool = False
    include_dysymtab: bool = False
    dysymtab_indirect_count: int = 0
    dysymtab_reloc_count: int = 0
    unknown_command: bool = False
    unsupported_command: int | None = None
    command_padding: bytes = b""
    override_ncmds: int | None = None
    override_sizeofcmds: int | None = None
    override_magic: int = MACHO_MAGIC_64
    malformed_command: bytes | None = None

    def build(self) -> bytes:
        command_kinds: list[tuple[str, object | None]] = []
        for index, _seg in enumerate(self.segments):
            command_kinds.append(("segment", index))
        if self.unknown_command:
            command_kinds.append(("unknown", None))
        if self.unsupported_command is not None:
            command_kinds.append(("unsupported", None))
        if self.include_symtab:
            command_kinds.append(("symtab", None))
        if self.include_dysymtab:
            command_kinds.append(("dysymtab", None))
        if self.include_main:
            command_kinds.append(("main", None))
            if self.duplicate_main:
                command_kinds.append(("main", None))
        if self.malformed_command is not None:
            command_kinds.append(("malformed", None))
        if self.command_padding:
            command_kinds.append(("padding", None))

        def command_size(kind: str, value: object | None) -> int:
            if kind == "segment":
                seg = self.segments[int(value)]
                return SEGMENT_COMMAND_SIZE + seg.nsects * SECTION_64_SIZE + len(seg.cmdsize_extra)
            if kind == "unknown":
                return 8
            if kind == "unsupported":
                return 24
            if kind == "symtab":
                return SYMTAB_COMMAND_SIZE
            if kind == "dysymtab":
                return DYSYMTAB_COMMAND_SIZE
            if kind == "main":
                return MAIN_COMMAND_SIZE
            if kind == "malformed":
                return len(self.malformed_command or b"")
            if kind == "padding":
                return len(self.command_padding)
            raise AssertionError(kind)

        actual_sizeofcmds = sum(command_size(kind, value) for kind, value in command_kinds)
        file_cursor = HEADER_SIZE + actual_sizeofcmds
        for seg in self.segments:
            if seg.fileoff is None:
                seg.fileoff = file_cursor
            file_cursor = max(file_cursor, seg.fileoff + len(seg.data))

        symoff = file_cursor
        symbol_name = b"_fixture"
        string_table = b"\0" + symbol_name + b"\0"
        nlist = u32(1) + bytes([N_SECT, 1, 0, 0]) + u64(self.segments[self.entry_segment].vmaddr + self.entry_delta)
        if self.include_symtab:
            file_cursor += NLIST64_SIZE + len(string_table)
        indirectsymoff = file_cursor
        if self.include_dysymtab and self.dysymtab_indirect_count > 0:
            file_cursor += self.dysymtab_indirect_count * 4

        final_commands: list[bytes] = []
        for kind, value in command_kinds:
            if kind == "segment":
                seg = self.segments[int(value)]
                cmdsize = SEGMENT_COMMAND_SIZE + seg.nsects * SECTION_64_SIZE + len(seg.cmdsize_extra)
                final_commands.append(
                    u32(LC_SEGMENT_64) + u32(cmdsize) + fixed_name(seg.name) + u64(seg.vmaddr) + u64(seg.mem_size())
                    + u64(seg.fileoff or 0) + u64(len(seg.data)) + u32(seg.maxprot) + u32(seg.initprot)
                    + u32(seg.nsects) + u32(0) + (b"\0" * (seg.nsects * SECTION_64_SIZE)) + seg.cmdsize_extra
                )
            elif kind == "unknown":
                final_commands.append(u32(LC_UNKNOWN_HARMLESS) + u32(8))
            elif kind == "unsupported":
                final_commands.append(u32(int(self.unsupported_command or 0)) + u32(24) + b"\0" * 16)
            elif kind == "symtab":
                final_commands.append(u32(LC_SYMTAB) + u32(SYMTAB_COMMAND_SIZE) + u32(symoff) + u32(1) + u32(symoff + NLIST64_SIZE) + u32(len(string_table)))
            elif kind == "dysymtab":
                fields = [0] * 18
                fields[12] = indirectsymoff
                fields[13] = self.dysymtab_indirect_count
                fields[14] = file_cursor
                fields[15] = self.dysymtab_reloc_count
                fields[16] = file_cursor
                fields[17] = 0
                final_commands.append(u32(LC_DYSYMTAB) + u32(DYSYMTAB_COMMAND_SIZE) + b"".join(u32(v) for v in fields))
            elif kind == "main":
                entryoff = (self.segments[self.entry_segment].fileoff or 0) + self.entry_delta
                final_commands.append(u32(LC_MAIN) + u32(MAIN_COMMAND_SIZE) + u64(entryoff) + u64(0))
            elif kind == "malformed":
                final_commands.append(self.malformed_command or b"")
            elif kind == "padding":
                final_commands.append(self.command_padding)
        sizeofcmds = sum(len(c) for c in final_commands)
        ncmds = len(final_commands)
        if self.override_ncmds is not None:
            ncmds = self.override_ncmds
        if self.override_sizeofcmds is not None:
            sizeofcmds = self.override_sizeofcmds
        header = u32(self.override_magic) + u32(self.cputype) + u32(0) + u32(self.filetype) + u32(ncmds) + u32(sizeofcmds) + u32(0) + u32(0)
        size = max([HEADER_SIZE + sum(len(c) for c in final_commands)] + [(seg.fileoff or 0) + len(seg.data) for seg in self.segments] + ([file_cursor] if self.include_symtab or self.include_dysymtab else []))
        image = bytearray(size)
        image[:HEADER_SIZE] = header
        cursor = HEADER_SIZE
        for command in final_commands:
            image[cursor:cursor + len(command)] = command
            cursor += len(command)
        for seg in self.segments:
            image[seg.fileoff or 0:(seg.fileoff or 0) + len(seg.data)] = seg.data
        if self.include_symtab:
            image[symoff:symoff + NLIST64_SIZE] = nlist
            image[symoff + NLIST64_SIZE:symoff + NLIST64_SIZE + len(string_table)] = string_table
        if self.include_dysymtab and self.dysymtab_indirect_count > 0:
            image[indirectsymoff:indirectsymoff + self.dysymtab_indirect_count * 4] = b"\0" * (self.dysymtab_indirect_count * 4)
        return bytes(image)


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def corrupt_first_symtab_string_index(image: bytes, string_index: int) -> bytes:
    """Return image with the first nlist64 n_strx changed.

    The fixture writer owns these byte layouts, so this tiny parser keeps malformed
    symbol-table fixtures deterministic without hand-maintaining offsets in tests.
    """
    data = bytearray(image)
    ncmds = struct.unpack_from("<I", data, 16)[0]
    cursor = HEADER_SIZE
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, cursor)
        if cmd == LC_SYMTAB:
            symoff = struct.unpack_from("<I", data, cursor + 8)[0]
            struct.pack_into("<I", data, symoff, string_index)
            return bytes(data)
        cursor += cmdsize
    raise ValueError("fixture has no LC_SYMTAB")


def header_mutations(image: bytes) -> list[tuple[str, bytes]]:
    mutations: list[tuple[str, bytes]] = []
    interesting_offsets = [0, 4, 12, 16, 20]
    for offset in interesting_offsets:
        data = bytearray(image)
        data[offset] ^= 0x80
        mutations.append((f"mutated_header_{offset:02d}.macho", bytes(data)))
    return mutations


def valid_minimal(status: int = 0, *, symtab: bool = True, unknown: bool = False) -> MachOImage:
    return MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(status))], include_symtab=symtab, unknown_command=unknown)


def generate_suite(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    hello = b"hello, v1.1!\n"
    err = b"ERRv1.1\n"
    data = b"DATA"
    valid_exit0 = valid_minimal(0).build()
    write(output_dir / "valid_exit0.macho", valid_exit0)
    write(output_dir / "valid_exit42.macho", valid_minimal(42).build())
    write(output_dir / "unknown_harmless.macho", valid_minimal(0, unknown=True).build())
    text = write_stdout(DATA_VMADDR, len(hello))
    write(output_dir / "stdout_data.macho", MachOImage([
        Segment("__TEXT", TEXT_VMADDR, text),
        Segment("__DATA", DATA_VMADDR, hello, maxprot=VM_PROT_READ | VM_PROT_WRITE, initprot=VM_PROT_READ | VM_PROT_WRITE),
    ], include_symtab=True).build())
    write(output_dir / "stderr_data.macho", MachOImage([
        Segment("__TEXT", TEXT_VMADDR, write_stderr(DATA_VMADDR, len(err))),
        Segment("__DATA", DATA_VMADDR, err, maxprot=VM_PROT_READ | VM_PROT_WRITE, initprot=VM_PROT_READ | VM_PROT_WRITE),
    ]).build())
    write(output_dir / "zero_fill_data.macho", MachOImage([
        Segment("__TEXT", TEXT_VMADDR, exit_code(0)),
        Segment("__DATA", DATA_VMADDR, data, vmsize=12, maxprot=VM_PROT_READ | VM_PROT_WRITE, initprot=VM_PROT_READ | VM_PROT_WRITE),
    ]).build())
    write(output_dir / "entry_offset.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, insn(OP_HLT, OP_HLT) + exit_code(0))], entry_delta=8).build())
    write(output_dir / "loop.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, insn(branch(0))) ]).build())
    write(output_dir / "unsupported_instruction.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, insn(OP_UNSUPPORTED))]).build())
    write(output_dir / "no_main.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], include_main=False).build())
    write(output_dir / "duplicate_main.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], duplicate_main=True).build())
    write(output_dir / "misaligned_entry.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, b"\0" + exit_code(0))], entry_delta=1).build())
    write(output_dir / "entry_outside.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], entry_delta=0x100).build())
    write(output_dir / "wrong_cpu.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], cputype=CPU_TYPE_X86_64).build())
    write(output_dir / "wrong_filetype.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], filetype=1).build())
    write(output_dir / "big_endian.macho", u32(MACHO_CIGAM_64) + b"\0" * 28)
    write(output_dir / "macho32.macho", u32(MACHO_MAGIC_32) + b"\0" * 28)
    write(output_dir / "fat.macho", u32(MACHO_FAT_MAGIC_LE_AS_READ_BY_LOADER) + b"\0" * 28)
    for name, cmd in [("dylinker.macho", LC_LOAD_DYLINKER), ("dylib.macho", LC_LOAD_DYLIB), ("dyld_info.macho", LC_DYLD_INFO), ("dyld_info_only.macho", LC_DYLD_INFO_ONLY)]:
        write(output_dir / name, MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], unsupported_command=cmd).build())
    write(output_dir / "relocations.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], include_dysymtab=True, dysymtab_reloc_count=1).build())
    write(output_dir / "bad_symbol_string_index.macho", corrupt_first_symtab_string_index(valid_exit0, 0xFFFF))
    bad = bytearray(valid_minimal(0).build())
    bad[16:20] = u32(1)
    bad[20:24] = u32(0)
    write(output_dir / "ncmds_size_mismatch.macho", bytes(bad))
    write(output_dir / "cmd_too_small.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], malformed_command=u32(LC_UNKNOWN_HARMLESS) + u32(4)).build())
    write(output_dir / "cmd_unaligned.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0))], malformed_command=u32(LC_UNKNOWN_HARMLESS) + u32(9) + b"X").build())
    write(output_dir / "filesize_gt_vmsize.macho", MachOImage([Segment("__TEXT", TEXT_VMADDR, exit_code(0), vmsize=4)]).build())
    file_range_bad = bytearray(valid_minimal(0).build())
    file_range_bad[HEADER_SIZE + 40:HEADER_SIZE + 48] = u64(0xFFF0)
    write(output_dir / "file_range_eof.macho", bytes(file_range_bad))
    write(output_dir / "mem_range_oob.macho", MachOImage([Segment("__TEXT", 0x100000 - 4, b"\0\0\0\0", vmsize=8)]).build())
    write(output_dir / "mem_range_boundary.macho", MachOImage([Segment("__TEXT", 0x100000 - 12, exit_code(0), vmsize=12)]).build())
    write(output_dir / "overlap_1byte.macho", MachOImage([
        Segment("__TEXT", TEXT_VMADDR, exit_code(0)),
        Segment("__DATA", TEXT_VMADDR + len(exit_code(0)) - 1, b"X", maxprot=VM_PROT_READ, initprot=VM_PROT_READ),
    ]).build())
    write(output_dir / "adjacent.macho", MachOImage([
        Segment("__TEXT", TEXT_VMADDR, exit_code(0)),
        Segment("__DATA", TEXT_VMADDR + len(exit_code(0)), b"X", maxprot=VM_PROT_READ, initprot=VM_PROT_READ),
    ]).build())
    write(output_dir / "zero_sized_segment.macho", MachOImage([
        Segment("__ZERO", 0x2000, b"", vmsize=0, maxprot=VM_PROT_READ, initprot=VM_PROT_READ),
        Segment("__TEXT", TEXT_VMADDR, exit_code(0)),
    ], entry_segment=1).build())
    section_bad = bytearray(valid_minimal(0).build())
    section_bad[HEADER_SIZE + 64:HEADER_SIZE + 68] = u32(1)
    write(output_dir / "section_overflow.macho", bytes(section_bad))
    for i in range(0, HEADER_SIZE):
        write(output_dir / f"truncated_header_{i:02d}.macho", valid_minimal(0).build()[:i])
    for name, mutation in header_mutations(valid_exit0):
        write(output_dir / name, mutation)
    write(output_dir / "empty.bin", b"")
    write(output_dir / "partial1.bin", b"\xcf")
    write(output_dir / "partial2.bin", b"\xcf\xfa")
    write(output_dir / "partial3.bin", b"\xcf\xfa\xed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    generate_suite(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

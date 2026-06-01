#include "loader_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ELF_IDENT_SIZE 16u
#define ELF64_EHDR_SIZE 64u
#define ELF64_PHDR_SIZE 56u

#define ELF_EI_CLASS 4u
#define ELF_EI_DATA 5u
#define ELF_EI_VERSION 6u

#define ELF_E_TYPE 16u
#define ELF_E_MACHINE 18u
#define ELF_E_VERSION 20u
#define ELF_E_ENTRY 24u
#define ELF_E_PHOFF 32u
#define ELF_E_EHSIZE 52u
#define ELF_E_PHENTSIZE 54u
#define ELF_E_PHNUM 56u

#define ELF_P_TYPE 0u
#define ELF_P_FLAGS 4u
#define ELF_P_OFFSET 8u
#define ELF_P_VADDR 16u
#define ELF_P_FILESZ 32u
#define ELF_P_MEMSZ 40u


static uint8_t elf_flags_to_permissions(uint32_t flags) {
    uint8_t permissions = 0;
    if ((flags & EMU_ELF_PF_R) != 0) {
        permissions |= EMU_MAP_READ;
    }
    if ((flags & EMU_ELF_PF_W) != 0) {
        permissions |= EMU_MAP_WRITE;
    }
    if ((flags & EMU_ELF_PF_X) != 0) {
        permissions |= EMU_MAP_EXEC;
    }
    return permissions;
}


bool loader_is_elf_magic(const uint8_t *bytes, size_t file_size) {
    return file_size >= 4u && bytes[0] == EMU_ELF_MAGIC0 && bytes[1] == EMU_ELF_MAGIC1 &&
           bytes[2] == EMU_ELF_MAGIC2 && bytes[3] == EMU_ELF_MAGIC3;
}


bool loader_load_elf64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                  char *error, size_t error_size) {
    memset(program, 0, sizeof(*program));
    program->format = EMU_PROGRAM_ELF64;
    program->stack_pointer = emu->memory.size;
    memory_clear_mappings(&emu->memory);

    if (file_size < ELF64_EHDR_SIZE) {
        snprintf(error, error_size, "ELF loader error: ELF header is truncated: file_size=0x%zx", file_size);
        return false;
    }

    if (bytes[ELF_EI_CLASS] != EMU_ELF_CLASS_64) {
        snprintf(error, error_size, "ELF loader error: unsupported ELF class %u; expected ELF64", bytes[ELF_EI_CLASS]);
        return false;
    }
    if (bytes[ELF_EI_DATA] != EMU_ELF_DATA_LSB) {
        snprintf(error, error_size, "ELF loader error: unsupported ELF data encoding %u; expected little-endian",
                 bytes[ELF_EI_DATA]);
        return false;
    }
    if (bytes[ELF_EI_VERSION] != EMU_ELF_VERSION_CURRENT) {
        snprintf(error, error_size, "ELF loader error: unsupported e_ident ELF version %u", bytes[ELF_EI_VERSION]);
        return false;
    }

    uint16_t e_type = loader_read_le16(bytes, ELF_E_TYPE);
    uint16_t e_machine = loader_read_le16(bytes, ELF_E_MACHINE);
    uint32_t e_version = loader_read_le32(bytes, ELF_E_VERSION);
    uint64_t e_entry = loader_read_le64(bytes, ELF_E_ENTRY);
    uint64_t e_phoff = loader_read_le64(bytes, ELF_E_PHOFF);
    uint16_t e_ehsize = loader_read_le16(bytes, ELF_E_EHSIZE);
    uint16_t e_phentsize = loader_read_le16(bytes, ELF_E_PHENTSIZE);
    uint16_t e_phnum = loader_read_le16(bytes, ELF_E_PHNUM);

    if (e_version != EMU_ELF_VERSION_CURRENT) {
        snprintf(error, error_size, "ELF loader error: unsupported ELF version %u", e_version);
        return false;
    }
    if (e_type == EMU_ELF_ET_DYN) {
        snprintf(error, error_size, "ELF loader error: ET_DYN/PIE is unsupported in v0.8");
        return false;
    }
    if (e_type != EMU_ELF_ET_EXEC) {
        snprintf(error, error_size, "ELF loader error: unsupported ELF type %u; expected ET_EXEC", e_type);
        return false;
    }
    if (e_machine != EMU_ELF_EM_AARCH64) {
        snprintf(error, error_size, "ELF loader error: unsupported machine %u; expected AArch64", e_machine);
        return false;
    }
    if (e_ehsize < ELF64_EHDR_SIZE || e_ehsize > file_size) {
        snprintf(error, error_size, "ELF loader error: invalid ELF header size: e_ehsize=0x%x file_size=0x%zx",
                 e_ehsize, file_size);
        return false;
    }
    if (e_phnum == 0) {
        snprintf(error, error_size, "ELF loader error: executable has no program headers");
        return false;
    }
    if (e_phoff == 0) {
        snprintf(error, error_size, "ELF loader error: program-header table offset is zero");
        return false;
    }
    if (e_phentsize != ELF64_PHDR_SIZE) {
        snprintf(error, error_size, "ELF loader error: invalid program-header entry size: %u", e_phentsize);
        return false;
    }

    uint64_t ph_table_size = (uint64_t)e_phnum * (uint64_t)e_phentsize;
    if (e_phnum != 0 && ph_table_size / e_phnum != e_phentsize) {
        snprintf(error, error_size, "ELF loader error: program-header table size overflow");
        return false;
    }
    if (!loader_range_fits_file(e_phoff, ph_table_size, file_size)) {
        snprintf(error, error_size,
                 "ELF loader error: program-header table outside file: phoff=0x%016" PRIx64
                 " size=0x%016" PRIx64 " file_size=0x%zx",
                 e_phoff, ph_table_size, file_size);
        return false;
    }

    for (uint16_t i = 0; i < e_phnum; i++) {
        size_t ph = (size_t)e_phoff + (size_t)i * (size_t)e_phentsize;
        uint32_t p_type = loader_read_le32(bytes, ph + ELF_P_TYPE);
        uint32_t p_flags = loader_read_le32(bytes, ph + ELF_P_FLAGS);
        uint64_t p_offset = loader_read_le64(bytes, ph + ELF_P_OFFSET);
        uint64_t p_vaddr = loader_read_le64(bytes, ph + ELF_P_VADDR);
        uint64_t p_filesz = loader_read_le64(bytes, ph + ELF_P_FILESZ);
        uint64_t p_memsz = loader_read_le64(bytes, ph + ELF_P_MEMSZ);

        if (p_type == EMU_ELF_PT_INTERP) {
            snprintf(error, error_size, "ELF loader error: PT_INTERP/dynamic linker is unsupported in v0.8");
            return false;
        }
        if (p_type != EMU_ELF_PT_LOAD) {
            continue;
        }
        if (p_filesz > p_memsz) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD filesz exceeds memsz: filesz=0x%016" PRIx64
                     " memsz=0x%016" PRIx64,
                     p_filesz, p_memsz);
            return false;
        }
        if (!loader_range_fits_file(p_offset, p_filesz, file_size)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD file range outside file: offset=0x%016" PRIx64
                     " filesz=0x%016" PRIx64 " file_size=0x%zx",
                     p_offset, p_filesz, file_size);
            return false;
        }
        const EmuDeviceRange *overlapped_device = NULL;
        if (loader_range_overlaps_device(&emu->memory, p_vaddr, p_memsz, &overlapped_device)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD memory range overlaps reserved device range: vaddr=0x%016" PRIx64
                     " memsz=0x%016" PRIx64 " device=%s range=0x%016" PRIx64 "-0x%016" PRIx64,
                     p_vaddr, p_memsz, overlapped_device->name, overlapped_device->start,
                     overlapped_device->start + overlapped_device->size);
            return false;
        }
        if (!loader_range_fits_memory(p_vaddr, p_memsz, &emu->memory)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD memory range outside emulator memory: vaddr=0x%016" PRIx64
                     " memsz=0x%016" PRIx64 " memory_size=0x%zx",
                     p_vaddr, p_memsz, emu->memory.size);
            return false;
        }
        if (!loader_record_segment(program, "ELF loader error", "PT_LOAD", NULL, p_vaddr, p_offset, p_memsz, p_filesz,
                            p_flags, 0, error, error_size)) {
            return false;
        }
    }

    if (program->segment_count == 0) {
        snprintf(error, error_size, "ELF loader error: executable has no PT_LOAD segments");
        return false;
    }
    if ((e_entry & 0x3u) != 0) {
        snprintf(error, error_size, "ELF loader error: entry point is not 4-byte aligned: entry=0x%016" PRIx64,
                 e_entry);
        return false;
    }
    const EmuDeviceRange *entry_device = memory_find_device(&emu->memory, e_entry);
    if (entry_device != NULL) {
        snprintf(error, error_size,
                 "ELF loader error: entry point is inside reserved non-executable device range: entry=0x%016" PRIx64
                 " device=%s range=0x%016" PRIx64 "-0x%016" PRIx64,
                 e_entry, entry_device->name, entry_device->start, entry_device->start + entry_device->size);
        return false;
    }
    if (!loader_entry_is_mapped(program, e_entry)) {
        snprintf(error, error_size, "ELF loader error: entry point is not inside a loaded segment: entry=0x%016" PRIx64,
                 e_entry);
        return false;
    }

    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        if (segment->mem_size == 0) {
            continue;
        }
        uint8_t permissions = elf_flags_to_permissions(segment->flags);
        if (e_entry >= segment->vaddr && e_entry < segment->vaddr + segment->mem_size) {
            permissions |= EMU_MAP_EXEC;
        }
        uint64_t map_start = 0;
        uint64_t map_size = 0;
        if (!loader_page_range_for_segment(segment->vaddr, segment->mem_size, &map_start, &map_size)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD page mapping overflow: vaddr=0x%016" PRIx64
                     " memsz=0x%016" PRIx64,
                     segment->vaddr, segment->mem_size);
            return false;
        }
        if (!loader_map_pages(&emu->memory, map_start, map_size, permissions, "elf:PT_LOAD", error, error_size)) {
            char detail[512];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "ELF loader error: %s", detail);
            return false;
        }
    }
    if (!loader_map_stack_for_program(emu, program, "ELF loader error", error, error_size)) {
        return false;
    }

    for (uint16_t i = 0; i < e_phnum; i++) {
        size_t ph = (size_t)e_phoff + (size_t)i * (size_t)e_phentsize;
        uint32_t p_type = loader_read_le32(bytes, ph + ELF_P_TYPE);
        if (p_type != EMU_ELF_PT_LOAD) {
            continue;
        }
        uint64_t p_offset = loader_read_le64(bytes, ph + ELF_P_OFFSET);
        uint64_t p_vaddr = loader_read_le64(bytes, ph + ELF_P_VADDR);
        uint64_t p_filesz = loader_read_le64(bytes, ph + ELF_P_FILESZ);
        uint64_t p_memsz = loader_read_le64(bytes, ph + ELF_P_MEMSZ);

        if (p_filesz > 0) {
            memcpy(&emu->memory.bytes[p_vaddr], &bytes[p_offset], (size_t)p_filesz);
        }
        if (p_memsz > p_filesz) {
            memset(&emu->memory.bytes[p_vaddr + p_filesz], 0, (size_t)(p_memsz - p_filesz));
        }
    }

    program->entry = e_entry;
    cpu_init(&emu->cpu, program->entry, program->stack_pointer);
    return true;
}

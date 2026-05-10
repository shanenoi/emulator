#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
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

static bool read_file(const char *path, uint8_t **out_bytes, size_t *out_size, char *error, size_t error_size) {
    *out_bytes = NULL;
    *out_size = 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "loader error: failed to open '%s': %s", path, strerror(errno));
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        snprintf(error, error_size, "loader error: failed to seek '%s': %s", path, strerror(errno));
        fclose(file);
        return false;
    }

    long file_size_long = ftell(file);
    if (file_size_long < 0) {
        snprintf(error, error_size, "loader error: failed to measure '%s': %s", path, strerror(errno));
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        snprintf(error, error_size, "loader error: failed to rewind '%s': %s", path, strerror(errno));
        fclose(file);
        return false;
    }

    size_t file_size = (size_t)file_size_long;
    if (file_size == 0) {
        snprintf(error, error_size, "loader error: input file is empty: '%s'", path);
        fclose(file);
        return false;
    }

    uint8_t *bytes = malloc(file_size);
    if (bytes == NULL) {
        snprintf(error, error_size, "loader error: failed to allocate 0x%zx bytes for '%s'", file_size, path);
        fclose(file);
        return false;
    }

    size_t bytes_read = fread(bytes, 1, file_size, file);
    if (bytes_read != file_size) {
        snprintf(error, error_size, "loader error: expected 0x%zx bytes but read 0x%zx from '%s'", file_size,
                 bytes_read, path);
        free(bytes);
        fclose(file);
        return false;
    }

    fclose(file);
    *out_bytes = bytes;
    *out_size = file_size;
    return true;
}

static uint16_t read_le16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | (uint16_t)((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_le32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) | ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_le64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8u; i++) {
        value |= ((uint64_t)bytes[offset + i]) << (8u * i);
    }
    return value;
}

static bool checked_u64_add(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool range_fits_file(uint64_t offset, uint64_t length, size_t file_size) {
    uint64_t end = 0;
    return checked_u64_add(offset, length, &end) && end <= (uint64_t)file_size;
}

static bool range_fits_memory(uint64_t address, uint64_t length, const Memory *memory) {
    uint64_t end = 0;
    return checked_u64_add(address, length, &end) && end <= (uint64_t)memory->size;
}

static bool ranges_overlap(uint64_t a_start, uint64_t a_length, uint64_t b_start, uint64_t b_length) {
    uint64_t a_end = a_start + a_length;
    uint64_t b_end = b_start + b_length;
    return a_start < b_end && b_start < a_end;
}

static bool is_elf_magic(const uint8_t *bytes, size_t file_size) {
    return file_size >= 4u && bytes[0] == EMU_ELF_MAGIC0 && bytes[1] == EMU_ELF_MAGIC1 &&
           bytes[2] == EMU_ELF_MAGIC2 && bytes[3] == EMU_ELF_MAGIC3;
}

bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t file_size = 0;
    if (!read_file(path, &bytes, &file_size, error, error_size)) {
        return false;
    }

    if (load_address > memory->size || file_size > memory->size - (size_t)load_address) {
        snprintf(error, error_size,
                 "loader error: file size 0x%zx does not fit at load address 0x%016llx; available=0x%zx",
                 file_size, (unsigned long long)load_address,
                 load_address <= memory->size ? memory->size - (size_t)load_address : 0u);
        free(bytes);
        return false;
    }

    memcpy(&memory->bytes[load_address], bytes, file_size);
    free(bytes);
    return true;
}

static bool record_segment(EmuLoadedProgram *program, uint64_t vaddr, uint64_t mem_size, uint64_t file_size,
                           uint32_t flags, char *error, size_t error_size) {
    if (program->segment_count >= EMU_MAX_ELF_SEGMENTS) {
        snprintf(error, error_size, "ELF loader error: too many PT_LOAD segments; max=%u", EMU_MAX_ELF_SEGMENTS);
        return false;
    }

    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *existing = &program->segments[i];
        if (ranges_overlap(vaddr, mem_size, existing->vaddr, existing->mem_size)) {
            snprintf(error, error_size,
                     "ELF loader error: overlapping PT_LOAD segments: 0x%016" PRIx64 "+0x%016" PRIx64
                     " overlaps 0x%016" PRIx64 "+0x%016" PRIx64,
                     vaddr, mem_size, existing->vaddr, existing->mem_size);
            return false;
        }
    }

    program->segments[program->segment_count].vaddr = vaddr;
    program->segments[program->segment_count].mem_size = mem_size;
    program->segments[program->segment_count].file_size = file_size;
    program->segments[program->segment_count].flags = flags;
    program->segment_count++;
    return true;
}

static bool entry_is_mapped(const EmuLoadedProgram *program, uint64_t entry) {
    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        uint64_t end = segment->vaddr + segment->mem_size;
        if (entry >= segment->vaddr && entry < end) {
            return true;
        }
    }
    return false;
}

static bool load_elf64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                  char *error, size_t error_size) {
    memset(program, 0, sizeof(*program));
    program->format = EMU_PROGRAM_ELF64;
    program->stack_pointer = emu->memory.size;

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

    uint16_t e_type = read_le16(bytes, ELF_E_TYPE);
    uint16_t e_machine = read_le16(bytes, ELF_E_MACHINE);
    uint32_t e_version = read_le32(bytes, ELF_E_VERSION);
    uint64_t e_entry = read_le64(bytes, ELF_E_ENTRY);
    uint64_t e_phoff = read_le64(bytes, ELF_E_PHOFF);
    uint16_t e_ehsize = read_le16(bytes, ELF_E_EHSIZE);
    uint16_t e_phentsize = read_le16(bytes, ELF_E_PHENTSIZE);
    uint16_t e_phnum = read_le16(bytes, ELF_E_PHNUM);

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
    if (!range_fits_file(e_phoff, ph_table_size, file_size)) {
        snprintf(error, error_size,
                 "ELF loader error: program-header table outside file: phoff=0x%016" PRIx64
                 " size=0x%016" PRIx64 " file_size=0x%zx",
                 e_phoff, ph_table_size, file_size);
        return false;
    }

    for (uint16_t i = 0; i < e_phnum; i++) {
        size_t ph = (size_t)e_phoff + (size_t)i * (size_t)e_phentsize;
        uint32_t p_type = read_le32(bytes, ph + ELF_P_TYPE);
        uint32_t p_flags = read_le32(bytes, ph + ELF_P_FLAGS);
        uint64_t p_offset = read_le64(bytes, ph + ELF_P_OFFSET);
        uint64_t p_vaddr = read_le64(bytes, ph + ELF_P_VADDR);
        uint64_t p_filesz = read_le64(bytes, ph + ELF_P_FILESZ);
        uint64_t p_memsz = read_le64(bytes, ph + ELF_P_MEMSZ);

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
        if (!range_fits_file(p_offset, p_filesz, file_size)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD file range outside file: offset=0x%016" PRIx64
                     " filesz=0x%016" PRIx64 " file_size=0x%zx",
                     p_offset, p_filesz, file_size);
            return false;
        }
        if (!range_fits_memory(p_vaddr, p_memsz, &emu->memory)) {
            snprintf(error, error_size,
                     "ELF loader error: PT_LOAD memory range outside emulator memory: vaddr=0x%016" PRIx64
                     " memsz=0x%016" PRIx64 " memory_size=0x%zx",
                     p_vaddr, p_memsz, emu->memory.size);
            return false;
        }
        if (!record_segment(program, p_vaddr, p_memsz, p_filesz, p_flags, error, error_size)) {
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
    if (!entry_is_mapped(program, e_entry)) {
        snprintf(error, error_size, "ELF loader error: entry point is not inside a loaded segment: entry=0x%016" PRIx64,
                 e_entry);
        return false;
    }

    for (uint16_t i = 0; i < e_phnum; i++) {
        size_t ph = (size_t)e_phoff + (size_t)i * (size_t)e_phentsize;
        uint32_t p_type = read_le32(bytes, ph + ELF_P_TYPE);
        if (p_type != EMU_ELF_PT_LOAD) {
            continue;
        }
        uint64_t p_offset = read_le64(bytes, ph + ELF_P_OFFSET);
        uint64_t p_vaddr = read_le64(bytes, ph + ELF_P_VADDR);
        uint64_t p_filesz = read_le64(bytes, ph + ELF_P_FILESZ);
        uint64_t p_memsz = read_le64(bytes, ph + ELF_P_MEMSZ);

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

bool emulator_load_program(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t file_size = 0;
    if (!read_file(path, &bytes, &file_size, error, error_size)) {
        return false;
    }

    bool ok = false;
    if (is_elf_magic(bytes, file_size)) {
        ok = load_elf64_from_bytes(emu, bytes, file_size, program, error, error_size);
    } else {
        memset(program, 0, sizeof(*program));
        program->format = EMU_PROGRAM_RAW;
        program->entry = EMU_LOAD_ADDRESS;
        program->stack_pointer = emu->memory.size;
        if (file_size > emu->memory.size - (size_t)EMU_LOAD_ADDRESS) {
            snprintf(error, error_size,
                     "loader error: file size 0x%zx does not fit at load address 0x%016llx; available=0x%zx",
                     file_size, (unsigned long long)EMU_LOAD_ADDRESS, emu->memory.size - (size_t)EMU_LOAD_ADDRESS);
            ok = false;
        } else {
            memcpy(&emu->memory.bytes[EMU_LOAD_ADDRESS], bytes, file_size);
            cpu_init(&emu->cpu, program->entry, program->stack_pointer);
            ok = true;
        }
    }

    free(bytes);
    return ok;
}

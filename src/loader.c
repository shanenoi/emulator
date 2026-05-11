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

#define MACHO64_HEADER_SIZE 32u
#define MACHO64_MIN_LOAD_COMMAND_SIZE 8u
#define MACHO64_SEGMENT_COMMAND_SIZE 72u
#define MACHO64_SECTION_SIZE 80u
#define MACHO64_MAIN_COMMAND_SIZE 24u
#define MACHO64_SYMTAB_COMMAND_SIZE 24u
#define MACHO64_DYSYMTAB_COMMAND_SIZE 80u

#define MACHO_CPUTYPE 4u
#define MACHO_FILETYPE 12u
#define MACHO_NCMDS 16u
#define MACHO_SIZEOFCMDS 20u

#define MACHO_LC_CMD 0u
#define MACHO_LC_CMDSIZE 4u

#define MACHO_SEG_VMADDR 24u
#define MACHO_SEG_VMSIZE 32u
#define MACHO_SEG_FILEOFF 40u
#define MACHO_SEG_FILESIZE 48u
#define MACHO_SEG_INITPROT 60u
#define MACHO_SEG_NSECTS 64u

#define MACHO_MAIN_ENTRYOFF 8u

#define MACHO_SYMTAB_SYMOFF 8u
#define MACHO_SYMTAB_NSYMS 12u
#define MACHO_SYMTAB_STROFF 16u
#define MACHO_SYMTAB_STRSIZE 20u

#define MACHO_DYSYMTAB_INDIRECTSYM_OFFSET 56u
#define MACHO_DYSYMTAB_NINDIRECTSYMS 60u

#define MACHO_NLIST64_SIZE 16u
#define MACHO_INDIRECT_SYMBOL_SIZE 4u

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
    uint64_t a_end = 0;
    uint64_t b_end = 0;
    if (!checked_u64_add(a_start, a_length, &a_end) || !checked_u64_add(b_start, b_length, &b_end)) {
        return true;
    }
    return a_start < b_end && b_start < a_end;
}

static bool is_elf_magic(const uint8_t *bytes, size_t file_size) {
    return file_size >= 4u && bytes[0] == EMU_ELF_MAGIC0 && bytes[1] == EMU_ELF_MAGIC1 &&
           bytes[2] == EMU_ELF_MAGIC2 && bytes[3] == EMU_ELF_MAGIC3;
}

static bool is_macho_magic(const uint8_t *bytes, size_t file_size) {
    if (file_size < 4u) {
        return false;
    }
    uint32_t magic = read_le32(bytes, 0);
    return magic == EMU_MACHO_MAGIC_64 || magic == EMU_MACHO_MAGIC_32 || magic == EMU_MACHO_CIGAM_64 ||
           magic == EMU_MACHO_CIGAM_32 || magic == EMU_MACHO_FAT_MAGIC || magic == EMU_MACHO_FAT_MAGIC_64;
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

static bool record_segment(EmuLoadedProgram *program, const char *error_prefix, const char *segment_kind,
                           const char *name, uint64_t vaddr, uint64_t file_offset, uint64_t mem_size,
                           uint64_t file_size, uint32_t flags, uint32_t section_count, char *error,
                           size_t error_size) {
    if (program->segment_count >= EMU_MAX_LOAD_SEGMENTS) {
        snprintf(error, error_size, "%s: too many %s segments; max=%u", error_prefix, segment_kind,
                 EMU_MAX_LOAD_SEGMENTS);
        return false;
    }

    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *existing = &program->segments[i];
        if (ranges_overlap(vaddr, mem_size, existing->vaddr, existing->mem_size)) {
            snprintf(error, error_size,
                     "%s: overlapping %s segments: 0x%016" PRIx64 "+0x%016" PRIx64
                     " overlaps 0x%016" PRIx64 "+0x%016" PRIx64,
                     error_prefix, segment_kind, vaddr, mem_size, existing->vaddr, existing->mem_size);
            return false;
        }
    }

    EmuLoadedSegment *segment = &program->segments[program->segment_count];
    if (name != NULL) {
        snprintf(segment->name, sizeof(segment->name), "%s", name);
    }
    segment->vaddr = vaddr;
    segment->file_offset = file_offset;
    segment->mem_size = mem_size;
    segment->file_size = file_size;
    segment->flags = flags;
    segment->section_count = section_count;
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
        if (!record_segment(program, "ELF loader error", "PT_LOAD", NULL, p_vaddr, p_offset, p_memsz, p_filesz,
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

static bool command_range_is_valid(uint64_t offset, uint64_t size, size_t file_size) {
    return size >= MACHO64_MIN_LOAD_COMMAND_SIZE && range_fits_file(offset, size, file_size);
}

static bool macho_command_requires_runtime(uint32_t cmd, const char **reason) {
    switch (cmd) {
    case EMU_MACHO_LC_LOAD_DYLINKER:
        *reason = "LC_LOAD_DYLINKER/dyld is unsupported in v1.1";
        return true;
    case EMU_MACHO_LC_LOAD_DYLIB:
        *reason = "LC_LOAD_DYLIB/shared libraries are unsupported in v1.1";
        return true;
    case EMU_MACHO_LC_DYLD_INFO:
    case EMU_MACHO_LC_DYLD_INFO_ONLY:
        *reason = "LC_DYLD_INFO/rebasing and binding metadata is unsupported in v1.1";
        return true;
    default:
        *reason = NULL;
        return false;
    }
}

static bool resolve_macho_entry_from_lc_main(const EmuLoadedProgram *program, uint64_t entryoff, uint64_t *entry) {
    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        uint64_t file_end = 0;
        if (segment->file_size == 0 || !checked_u64_add(segment->file_offset, segment->file_size, &file_end)) {
            continue;
        }
        if (entryoff >= segment->file_offset && entryoff < file_end) {
            *entry = segment->vaddr + (entryoff - segment->file_offset);
            return true;
        }
    }
    return false;
}

static bool load_macho64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                    char *error, size_t error_size) {
    memset(program, 0, sizeof(*program));
    program->format = EMU_PROGRAM_MACHO64;
    program->stack_pointer = emu->memory.size;

    if (file_size < MACHO64_HEADER_SIZE) {
        snprintf(error, error_size, "Mach-O loader error: Mach-O header is truncated: file_size=0x%zx", file_size);
        return false;
    }

    uint32_t magic = read_le32(bytes, 0);
    if (magic == EMU_MACHO_FAT_MAGIC || magic == EMU_MACHO_FAT_MAGIC_64) {
        snprintf(error, error_size,
                 "Mach-O loader error: fat/universal Mach-O archives are unsupported in v1.1; provide a thin arm64 slice");
        return false;
    }
    if (magic == EMU_MACHO_CIGAM_64) {
        snprintf(error, error_size, "Mach-O loader error: big-endian Mach-O is unsupported in v1.1");
        return false;
    }
    if (magic == EMU_MACHO_MAGIC_32 || magic == EMU_MACHO_CIGAM_32) {
        snprintf(error, error_size, "Mach-O loader error: 32-bit Mach-O is unsupported in v1.1");
        return false;
    }
    if (magic != EMU_MACHO_MAGIC_64) {
        snprintf(error, error_size, "Mach-O loader error: unsupported Mach-O magic 0x%08" PRIx32, magic);
        return false;
    }

    uint32_t cputype = read_le32(bytes, MACHO_CPUTYPE);
    uint32_t filetype = read_le32(bytes, MACHO_FILETYPE);
    uint32_t ncmds = read_le32(bytes, MACHO_NCMDS);
    uint32_t sizeofcmds = read_le32(bytes, MACHO_SIZEOFCMDS);
    program->macho_load_command_count = ncmds;

    if (cputype != EMU_MACHO_CPU_TYPE_ARM64) {
        snprintf(error, error_size, "Mach-O loader error: unsupported CPU type 0x%08" PRIx32 "; expected arm64",
                 cputype);
        return false;
    }
    if (filetype != EMU_MACHO_FILETYPE_EXECUTE) {
        snprintf(error, error_size, "Mach-O loader error: unsupported file type %u; expected MH_EXECUTE", filetype);
        return false;
    }
    if (ncmds == 0) {
        snprintf(error, error_size, "Mach-O loader error: executable has no load commands");
        return false;
    }
    if (!range_fits_file(MACHO64_HEADER_SIZE, sizeofcmds, file_size)) {
        snprintf(error, error_size,
                 "Mach-O loader error: load-command table outside file: offset=0x%08x size=0x%08" PRIx32
                 " file_size=0x%zx",
                 MACHO64_HEADER_SIZE, sizeofcmds, file_size);
        return false;
    }

    bool saw_lc_main = false;
    uint64_t entryoff = 0;
    uint64_t command_offset = MACHO64_HEADER_SIZE;
    uint64_t commands_end = 0;
    if (!checked_u64_add(MACHO64_HEADER_SIZE, sizeofcmds, &commands_end)) {
        snprintf(error, error_size, "Mach-O loader error: load-command table size overflow");
        return false;
    }

    for (uint32_t i = 0; i < ncmds; i++) {
        if (command_offset >= commands_end || !range_fits_file(command_offset, MACHO64_MIN_LOAD_COMMAND_SIZE, file_size)) {
            snprintf(error, error_size, "Mach-O loader error: load command %u is outside the command table", i);
            return false;
        }

        uint32_t cmd = read_le32(bytes, (size_t)command_offset + MACHO_LC_CMD);
        uint32_t cmdsize = read_le32(bytes, (size_t)command_offset + MACHO_LC_CMDSIZE);
        uint64_t next_command_offset = 0;
        if (!checked_u64_add(command_offset, cmdsize, &next_command_offset) ||
            !command_range_is_valid(command_offset, cmdsize, file_size) || next_command_offset > commands_end) {
            snprintf(error, error_size,
                     "Mach-O loader error: invalid load command %u size: offset=0x%016" PRIx64
                     " cmdsize=0x%08" PRIx32 " sizeofcmds=0x%08" PRIx32,
                     i, command_offset, cmdsize, sizeofcmds);
            return false;
        }
        if ((cmdsize & 0x7u) != 0) {
            snprintf(error, error_size,
                     "Mach-O loader error: invalid load command %u alignment: cmdsize=0x%08" PRIx32,
                     i, cmdsize);
            return false;
        }

        const char *unsupported_reason = NULL;
        if (macho_command_requires_runtime(cmd, &unsupported_reason)) {
            snprintf(error, error_size, "Mach-O loader error: %s", unsupported_reason);
            return false;
        }

        if (cmd == EMU_MACHO_LC_SEGMENT_64) {
            if (cmdsize < MACHO64_SEGMENT_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SEGMENT_64 command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }

            char segment_name[17];
            memcpy(segment_name, &bytes[(size_t)command_offset + 8u], 16u);
            segment_name[16] = '\0';
            uint64_t vmaddr = read_le64(bytes, (size_t)command_offset + MACHO_SEG_VMADDR);
            uint64_t vmsize = read_le64(bytes, (size_t)command_offset + MACHO_SEG_VMSIZE);
            uint64_t fileoff = read_le64(bytes, (size_t)command_offset + MACHO_SEG_FILEOFF);
            uint64_t filesize = read_le64(bytes, (size_t)command_offset + MACHO_SEG_FILESIZE);
            uint32_t initprot = read_le32(bytes, (size_t)command_offset + MACHO_SEG_INITPROT);
            uint32_t nsects = read_le32(bytes, (size_t)command_offset + MACHO_SEG_NSECTS);
            uint64_t required_segment_size = MACHO64_SEGMENT_COMMAND_SIZE;
            uint64_t section_table_size = (uint64_t)nsects * MACHO64_SECTION_SIZE;
            if (nsects != 0 && section_table_size / nsects != MACHO64_SECTION_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SEGMENT_64 section table size overflow");
                return false;
            }
            if (!checked_u64_add(MACHO64_SEGMENT_COMMAND_SIZE, section_table_size, &required_segment_size) ||
                cmdsize < required_segment_size) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 section table is truncated: nsects=%" PRIu32
                         " cmdsize=0x%08" PRIx32,
                         nsects, cmdsize);
                return false;
            }

            if (filesize > vmsize) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 filesize exceeds vmsize: filesize=0x%016" PRIx64
                         " vmsize=0x%016" PRIx64,
                         filesize, vmsize);
                return false;
            }
            if (!range_fits_file(fileoff, filesize, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 file range outside file: fileoff=0x%016" PRIx64
                         " filesize=0x%016" PRIx64 " file_size=0x%zx",
                         fileoff, filesize, file_size);
                return false;
            }
            if (!range_fits_memory(vmaddr, vmsize, &emu->memory)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 memory range outside emulator memory: vmaddr=0x%016" PRIx64
                         " vmsize=0x%016" PRIx64 " memory_size=0x%zx",
                         vmaddr, vmsize, emu->memory.size);
                return false;
            }
            if (vmsize > 0 && !record_segment(program, "Mach-O loader error", "LC_SEGMENT_64", segment_name, vmaddr,
                                              fileoff, vmsize, filesize, initprot, nsects, error, error_size)) {
                return false;
            }
        } else if (cmd == EMU_MACHO_LC_MAIN) {
            if (cmdsize < MACHO64_MAIN_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_MAIN command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }
            if (saw_lc_main) {
                snprintf(error, error_size, "Mach-O loader error: duplicate LC_MAIN commands are unsupported in v1.1");
                return false;
            }
            saw_lc_main = true;
            entryoff = read_le64(bytes, (size_t)command_offset + MACHO_MAIN_ENTRYOFF);
        } else if (cmd == EMU_MACHO_LC_SYMTAB) {
            if (cmdsize < MACHO64_SYMTAB_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SYMTAB command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }
            uint32_t symoff = read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_SYMOFF);
            uint32_t nsyms = read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_NSYMS);
            uint32_t stroff = read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_STROFF);
            uint32_t strsize = read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_STRSIZE);
            uint64_t symbol_table_size = (uint64_t)nsyms * MACHO_NLIST64_SIZE;
            if (nsyms != 0 && symbol_table_size / nsyms != MACHO_NLIST64_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SYMTAB symbol table size overflow");
                return false;
            }
            if (!range_fits_file(symoff, symbol_table_size, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SYMTAB symbol table outside file: symoff=0x%08" PRIx32
                         " nsyms=%" PRIu32 " file_size=0x%zx",
                         symoff, nsyms, file_size);
                return false;
            }
            if (!range_fits_file(stroff, strsize, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SYMTAB string table outside file: stroff=0x%08" PRIx32
                         " strsize=0x%08" PRIx32 " file_size=0x%zx",
                         stroff, strsize, file_size);
                return false;
            }
            program->macho_symbol_count = nsyms;
        } else if (cmd == EMU_MACHO_LC_DYSYMTAB) {
            if (cmdsize < MACHO64_DYSYMTAB_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_DYSYMTAB command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }
            uint32_t indirectsymoff = read_le32(bytes, (size_t)command_offset + MACHO_DYSYMTAB_INDIRECTSYM_OFFSET);
            uint32_t nindirectsyms = read_le32(bytes, (size_t)command_offset + MACHO_DYSYMTAB_NINDIRECTSYMS);
            uint32_t nextrel = read_le32(bytes, (size_t)command_offset + 68u);
            uint32_t nlocrel = read_le32(bytes, (size_t)command_offset + 76u);
            if (nextrel > 0 || nlocrel > 0) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_DYSYMTAB relocations are unsupported in v1.1: external=%" PRIu32
                         " local=%" PRIu32,
                         nextrel, nlocrel);
                return false;
            }
            uint64_t indirect_table_size = (uint64_t)nindirectsyms * MACHO_INDIRECT_SYMBOL_SIZE;
            if (nindirectsyms != 0 && indirect_table_size / nindirectsyms != MACHO_INDIRECT_SYMBOL_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_DYSYMTAB indirect symbol table size overflow");
                return false;
            }
            if (nindirectsyms > 0 && !range_fits_file(indirectsymoff, indirect_table_size, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_DYSYMTAB indirect symbol table outside file: indirectsymoff=0x%08"
                         PRIx32 " nindirectsyms=%" PRIu32 " file_size=0x%zx",
                         indirectsymoff, nindirectsyms, file_size);
                return false;
            }
            program->macho_indirect_symbol_count = nindirectsyms;
        }

        command_offset = next_command_offset;
    }

    if (command_offset != commands_end) {
        snprintf(error, error_size,
                 "Mach-O loader error: load-command walk ended at 0x%016" PRIx64 " but expected 0x%016" PRIx64,
                 command_offset, commands_end);
        return false;
    }
    if (program->segment_count == 0) {
        snprintf(error, error_size, "Mach-O loader error: executable has no LC_SEGMENT_64 segments");
        return false;
    }
    if (!saw_lc_main) {
        snprintf(error, error_size, "Mach-O loader error: LC_MAIN is required in v1.1");
        return false;
    }

    uint64_t entry = 0;
    if (!resolve_macho_entry_from_lc_main(program, entryoff, &entry)) {
        snprintf(error, error_size,
                 "Mach-O loader error: LC_MAIN entryoff is not inside a mapped file range: entryoff=0x%016" PRIx64,
                 entryoff);
        return false;
    }
    if ((entry & 0x3u) != 0) {
        snprintf(error, error_size, "Mach-O loader error: entry point is not 4-byte aligned: entry=0x%016" PRIx64,
                 entry);
        return false;
    }
    if (!entry_is_mapped(program, entry)) {
        snprintf(error, error_size, "Mach-O loader error: entry point is not inside a loaded segment: entry=0x%016" PRIx64,
                 entry);
        return false;
    }

    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        if (segment->file_size > 0) {
            memcpy(&emu->memory.bytes[segment->vaddr], &bytes[segment->file_offset], (size_t)segment->file_size);
        }
        if (segment->mem_size > segment->file_size) {
            memset(&emu->memory.bytes[segment->vaddr + segment->file_size], 0,
                   (size_t)(segment->mem_size - segment->file_size));
        }
    }

    program->entry = entry;
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
    } else if (is_macho_magic(bytes, file_size)) {
        ok = load_macho64_from_bytes(emu, bytes, file_size, program, error, error_size);
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

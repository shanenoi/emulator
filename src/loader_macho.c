#include "loader_internal.h"

#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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

#define MACHO_NLIST_N_STRX 0u
#define MACHO_NLIST_N_VALUE 8u


static uint8_t macho_prot_to_permissions(uint32_t initprot) {
    uint8_t permissions = 0;
    if ((initprot & 0x1u) != 0) {
        permissions |= EMU_MAP_READ;
    }
    if ((initprot & 0x2u) != 0) {
        permissions |= EMU_MAP_WRITE;
    }
    if ((initprot & 0x4u) != 0) {
        permissions |= EMU_MAP_EXEC;
    }
    return permissions;
}


static size_t loader_bounded_cstring_length(const uint8_t *bytes, size_t offset, size_t limit) {
    size_t length = 0;
    while (offset + length < limit && bytes[offset + length] != '\0') {
        length++;
    }
    return length;
}


static void record_macho_symbol(EmuLoadedProgram *program, const char *name, size_t name_length, uint64_t address) {
    if (program->macho_recorded_symbol_count >= EMU_MAX_MACHO_SYMBOLS) {
        return;
    }

    EmuMachoSymbol *symbol = &program->macho_symbols[program->macho_recorded_symbol_count];
    size_t copy_length = name_length;
    if (copy_length >= sizeof(symbol->name)) {
        copy_length = sizeof(symbol->name) - 1u;
    }
    memcpy(symbol->name, name, copy_length);
    symbol->name[copy_length] = '\0';
    symbol->address = address;
    program->macho_recorded_symbol_count++;
}


bool loader_is_macho_magic(const uint8_t *bytes, size_t file_size) {
    if (file_size < 4u) {
        return false;
    }
    uint32_t magic = loader_read_le32(bytes, 0);
    return magic == EMU_MACHO_MAGIC_64 || magic == EMU_MACHO_MAGIC_32 || magic == EMU_MACHO_CIGAM_64 ||
           magic == EMU_MACHO_CIGAM_32 || magic == EMU_MACHO_FAT_MAGIC || magic == EMU_MACHO_FAT_MAGIC_64;
}


static bool command_range_is_valid(uint64_t offset, uint64_t size, size_t file_size) {
    return size >= MACHO64_MIN_LOAD_COMMAND_SIZE && loader_range_fits_file(offset, size, file_size);
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
        if (segment->file_size == 0 || !emu_checked_add_u64(segment->file_offset, segment->file_size, &file_end)) {
            continue;
        }
        if (entryoff >= segment->file_offset && entryoff < file_end) {
            *entry = segment->vaddr + (entryoff - segment->file_offset);
            return true;
        }
    }
    return false;
}


bool loader_load_macho64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                    char *error, size_t error_size) {
    memset(program, 0, sizeof(*program));
    program->format = EMU_PROGRAM_MACHO64;
    program->stack_pointer = emu->memory.size;
    memory_clear_mappings(&emu->memory);

    if (file_size < MACHO64_HEADER_SIZE) {
        snprintf(error, error_size, "Mach-O loader error: Mach-O header is truncated: file_size=0x%zx", file_size);
        return false;
    }

    uint32_t magic = loader_read_le32(bytes, 0);
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

    uint32_t cputype = loader_read_le32(bytes, MACHO_CPUTYPE);
    uint32_t filetype = loader_read_le32(bytes, MACHO_FILETYPE);
    uint32_t ncmds = loader_read_le32(bytes, MACHO_NCMDS);
    uint32_t sizeofcmds = loader_read_le32(bytes, MACHO_SIZEOFCMDS);
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
    if (!loader_range_fits_file(MACHO64_HEADER_SIZE, sizeofcmds, file_size)) {
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
    if (!emu_checked_add_u64(MACHO64_HEADER_SIZE, sizeofcmds, &commands_end)) {
        snprintf(error, error_size, "Mach-O loader error: load-command table size overflow");
        return false;
    }

    for (uint32_t i = 0; i < ncmds; i++) {
        if (command_offset >= commands_end || !loader_range_fits_file(command_offset, MACHO64_MIN_LOAD_COMMAND_SIZE, file_size)) {
            snprintf(error, error_size, "Mach-O loader error: load command %u is outside the command table", i);
            return false;
        }

        uint32_t cmd = loader_read_le32(bytes, (size_t)command_offset + MACHO_LC_CMD);
        uint32_t cmdsize = loader_read_le32(bytes, (size_t)command_offset + MACHO_LC_CMDSIZE);
        uint64_t next_command_offset = 0;
        if (!emu_checked_add_u64(command_offset, cmdsize, &next_command_offset) ||
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
            uint64_t vmaddr = loader_read_le64(bytes, (size_t)command_offset + MACHO_SEG_VMADDR);
            uint64_t vmsize = loader_read_le64(bytes, (size_t)command_offset + MACHO_SEG_VMSIZE);
            uint64_t fileoff = loader_read_le64(bytes, (size_t)command_offset + MACHO_SEG_FILEOFF);
            uint64_t filesize = loader_read_le64(bytes, (size_t)command_offset + MACHO_SEG_FILESIZE);
            uint32_t initprot = loader_read_le32(bytes, (size_t)command_offset + MACHO_SEG_INITPROT);
            uint32_t nsects = loader_read_le32(bytes, (size_t)command_offset + MACHO_SEG_NSECTS);
            uint64_t required_segment_size = MACHO64_SEGMENT_COMMAND_SIZE;
            uint64_t section_table_size = (uint64_t)nsects * MACHO64_SECTION_SIZE;
            if (nsects != 0 && section_table_size / nsects != MACHO64_SECTION_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SEGMENT_64 section table size overflow");
                return false;
            }
            if (!emu_checked_add_u64(MACHO64_SEGMENT_COMMAND_SIZE, section_table_size, &required_segment_size) ||
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
            if (!loader_range_fits_file(fileoff, filesize, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 file range outside file: fileoff=0x%016" PRIx64
                         " filesize=0x%016" PRIx64 " file_size=0x%zx",
                         fileoff, filesize, file_size);
                return false;
            }
            const EmuDeviceRange *overlapped_device = NULL;
            if (loader_range_overlaps_device(&emu->memory, vmaddr, vmsize, &overlapped_device)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 memory range overlaps reserved device range: "
                         "vmaddr=0x%016" PRIx64 " vmsize=0x%016" PRIx64
                         " device=%s range=0x%016" PRIx64 "-0x%016" PRIx64,
                         vmaddr, vmsize, overlapped_device->name, overlapped_device->start,
                         overlapped_device->start + overlapped_device->size);
                return false;
            }
            if (!loader_range_fits_memory(vmaddr, vmsize, &emu->memory)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SEGMENT_64 memory range outside emulator memory: vmaddr=0x%016" PRIx64
                         " vmsize=0x%016" PRIx64 " memory_size=0x%zx",
                         vmaddr, vmsize, emu->memory.size);
                return false;
            }
            if (vmsize > 0 && !loader_record_segment(program, "Mach-O loader error", "LC_SEGMENT_64", segment_name, vmaddr,
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
            entryoff = loader_read_le64(bytes, (size_t)command_offset + MACHO_MAIN_ENTRYOFF);
        } else if (cmd == EMU_MACHO_LC_SYMTAB) {
            if (cmdsize < MACHO64_SYMTAB_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SYMTAB command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }
            uint32_t symoff = loader_read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_SYMOFF);
            uint32_t nsyms = loader_read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_NSYMS);
            uint32_t stroff = loader_read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_STROFF);
            uint32_t strsize = loader_read_le32(bytes, (size_t)command_offset + MACHO_SYMTAB_STRSIZE);
            uint64_t symbol_table_size = (uint64_t)nsyms * MACHO_NLIST64_SIZE;
            if (nsyms != 0 && symbol_table_size / nsyms != MACHO_NLIST64_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_SYMTAB symbol table size overflow");
                return false;
            }
            if (!loader_range_fits_file(symoff, symbol_table_size, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SYMTAB symbol table outside file: symoff=0x%08" PRIx32
                         " nsyms=%" PRIu32 " file_size=0x%zx",
                         symoff, nsyms, file_size);
                return false;
            }
            if (!loader_range_fits_file(stroff, strsize, file_size)) {
                snprintf(error, error_size,
                         "Mach-O loader error: LC_SYMTAB string table outside file: stroff=0x%08" PRIx32
                         " strsize=0x%08" PRIx32 " file_size=0x%zx",
                         stroff, strsize, file_size);
                return false;
            }
            program->macho_symbol_count = nsyms;
            program->macho_recorded_symbol_count = 0;
            for (uint32_t symbol_index = 0; symbol_index < nsyms; symbol_index++) {
                uint64_t symbol_offset = (uint64_t)symoff + (uint64_t)symbol_index * MACHO_NLIST64_SIZE;
                uint32_t string_index = loader_read_le32(bytes, (size_t)symbol_offset + MACHO_NLIST_N_STRX);
                uint64_t value = loader_read_le64(bytes, (size_t)symbol_offset + MACHO_NLIST_N_VALUE);
                if (string_index >= strsize) {
                    snprintf(error, error_size,
                             "Mach-O loader error: LC_SYMTAB symbol string index outside string table: symbol=%" PRIu32
                             " n_strx=0x%08" PRIx32 " strsize=0x%08" PRIx32,
                             symbol_index, string_index, strsize);
                    return false;
                }
                size_t name_offset = (size_t)stroff + (size_t)string_index;
                size_t string_limit = (size_t)stroff + (size_t)strsize;
                size_t name_length = loader_bounded_cstring_length(bytes, name_offset, string_limit);
                if (name_offset + name_length >= string_limit) {
                    snprintf(error, error_size,
                             "Mach-O loader error: LC_SYMTAB symbol name is not NUL-terminated inside string table:"
                             " symbol=%" PRIu32 " n_strx=0x%08" PRIx32,
                             symbol_index, string_index);
                    return false;
                }
                record_macho_symbol(program, (const char *)&bytes[name_offset], name_length, value);
            }
        } else if (cmd == EMU_MACHO_LC_DYSYMTAB) {
            if (cmdsize < MACHO64_DYSYMTAB_COMMAND_SIZE) {
                snprintf(error, error_size, "Mach-O loader error: LC_DYSYMTAB command is truncated: cmdsize=0x%08" PRIx32,
                         cmdsize);
                return false;
            }
            uint32_t indirectsymoff = loader_read_le32(bytes, (size_t)command_offset + MACHO_DYSYMTAB_INDIRECTSYM_OFFSET);
            uint32_t nindirectsyms = loader_read_le32(bytes, (size_t)command_offset + MACHO_DYSYMTAB_NINDIRECTSYMS);
            uint32_t nextrel = loader_read_le32(bytes, (size_t)command_offset + 68u);
            uint32_t nlocrel = loader_read_le32(bytes, (size_t)command_offset + 76u);
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
            if (nindirectsyms > 0 && !loader_range_fits_file(indirectsymoff, indirect_table_size, file_size)) {
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
    if (!loader_entry_is_mapped(program, entry)) {
        snprintf(error, error_size, "Mach-O loader error: entry point is not inside a loaded segment: entry=0x%016" PRIx64,
                 entry);
        return false;
    }

    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        if (segment->mem_size == 0) {
            continue;
        }
        uint8_t permissions = macho_prot_to_permissions(segment->flags);
        if (entry >= segment->vaddr && entry < segment->vaddr + segment->mem_size) {
            permissions |= EMU_MAP_EXEC;
        }
        char name[32];
        snprintf(name, sizeof(name), "macho:%s", segment->name[0] == '\0' ? "segment" : segment->name);
        uint64_t map_start = 0;
        uint64_t map_size = 0;
        if (!loader_page_range_for_segment(segment->vaddr, segment->mem_size, &map_start, &map_size)) {
            snprintf(error, error_size,
                     "Mach-O loader error: segment page mapping overflow: vmaddr=0x%016" PRIx64
                     " vmsize=0x%016" PRIx64,
                     segment->vaddr, segment->mem_size);
            return false;
        }
        if (!loader_map_pages(&emu->memory, map_start, map_size, permissions, name, error, error_size)) {
            char detail[512];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "Mach-O loader error: %s", detail);
            return false;
        }
    }
    if (!loader_map_stack_for_program(emu, program, "Mach-O loader error", error, error_size)) {
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

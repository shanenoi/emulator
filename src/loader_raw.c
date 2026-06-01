#include "loader_internal.h"

#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t file_size = 0;
    if (!loader_read_file(path, &bytes, &file_size, error, error_size)) {
        return false;
    }

    if (load_address > memory->size || file_size > memory->size - (size_t)load_address) {
        snprintf(error, error_size,
                 "loader error: file size 0x%zx does not fit at load address 0x%016" PRIx64 "; available=0x%zx",
                 file_size, load_address, load_address <= memory->size ? memory->size - (size_t)load_address : 0u);
        free(bytes);
        return false;
    }

    memcpy(&memory->bytes[load_address], bytes, file_size);
    free(bytes);
    return true;
}

bool loader_load_raw_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                char *error, size_t error_size) {
    memset(program, 0, sizeof(*program));
    program->format = EMU_PROGRAM_RAW;
    program->entry = EMU_LOAD_ADDRESS;
    program->stack_pointer = emu->memory.size;
    memory_clear_mappings(&emu->memory);
    uint64_t raw_instruction_size = (uint64_t)((file_size + 3u) & ~(size_t)3u);
    uint64_t raw_end = 0;
    uint64_t raw_mapping_size = 0;
    if ((size_t)raw_instruction_size < file_size) {
        snprintf(error, error_size, "loader error: raw mapping size overflow for file size 0x%zx", file_size);
        return false;
    }
    if (!emu_checked_add_u64(EMU_LOAD_ADDRESS, raw_instruction_size, &raw_end) ||
        !loader_align_up_to_page(raw_end, &raw_mapping_size) || raw_mapping_size < EMU_LOAD_ADDRESS) {
        snprintf(error, error_size, "loader error: raw page mapping size overflow for file size 0x%zx", file_size);
        return false;
    }
    if (raw_mapping_size - EMU_LOAD_ADDRESS > emu->memory.size - (size_t)EMU_LOAD_ADDRESS) {
        snprintf(error, error_size,
                 "loader error: file size 0x%zx does not fit at load address 0x%016llx; available=0x%zx",
                 file_size, (unsigned long long)EMU_LOAD_ADDRESS, emu->memory.size - (size_t)EMU_LOAD_ADDRESS);
        return false;
    }
    if (!memory_map_range(&emu->memory, EMU_LOAD_ADDRESS, raw_mapping_size - EMU_LOAD_ADDRESS,
                          EMU_MAP_READ | EMU_MAP_EXEC, "raw:program", error, error_size)) {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "loader error: %s", detail);
        return false;
    }
    if (!loader_map_stack_for_program(emu, program, "loader error", error, error_size)) {
        return false;
    }

    memcpy(&emu->memory.bytes[EMU_LOAD_ADDRESS], bytes, file_size);
    cpu_init(&emu->cpu, program->entry, program->stack_pointer);
    return true;
}

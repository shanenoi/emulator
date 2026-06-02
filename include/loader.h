#ifndef EMU_LOADER_H
#define EMU_LOADER_H

#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Emulator Emulator;

typedef enum {
    EMU_PROGRAM_RAW = 0,
    EMU_PROGRAM_ELF64,
    EMU_PROGRAM_MACHO64,
} EmuProgramFormat;

typedef struct {
    char name[17];
    uint64_t vaddr;
    uint64_t file_offset;
    uint64_t mem_size;
    uint64_t file_size;
    uint32_t flags;
    uint32_t section_count;
} EmuLoadedSegment;

typedef struct {
    char name[EMU_MAX_MACHO_SYMBOL_NAME];
    uint64_t address;
} EmuMachoSymbol;

typedef struct {
    EmuProgramFormat format;
    uint64_t entry;
    uint64_t stack_pointer;
    size_t segment_count;
    EmuLoadedSegment segments[EMU_MAX_LOAD_SEGMENTS];
    uint32_t macho_load_command_count;
    uint32_t macho_symbol_count;
    uint32_t macho_indirect_symbol_count;
    uint32_t macho_recorded_symbol_count;
    EmuMachoSymbol macho_symbols[EMU_MAX_MACHO_SYMBOLS];
} EmuLoadedProgram;

/*
 * Legacy raw-only loader retained for v0.1-v0.7 behavior and tests.
 * New v0.8+ call sites should prefer emulator_load_program(), which
 * auto-detects supported ELF64 files and falls back to the raw loader for
 * non-ELF input.
 */
bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size);
bool emulator_load_program(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size);

#endif

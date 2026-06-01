#ifndef EMU_LOADER_INTERNAL_H
#define EMU_LOADER_INTERNAL_H

#include "emulator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint16_t loader_read_le16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)bytes[offset] | (uint16_t)((uint16_t)bytes[offset + 1u] << 8u);
}

static inline uint32_t loader_read_le32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) | ((uint32_t)bytes[offset + 3u] << 24u);
}

static inline uint64_t loader_read_le64(const uint8_t *bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8u; i++) {
        value |= ((uint64_t)bytes[offset + i]) << (8u * i);
    }
    return value;
}

bool loader_read_file(const char *path, uint8_t **out_bytes, size_t *out_size, char *error, size_t error_size);
uint64_t loader_align_down_to_page(uint64_t value);
bool loader_align_up_to_page(uint64_t value, uint64_t *out);
bool loader_page_range_for_segment(uint64_t vaddr, uint64_t mem_size, uint64_t *page_start, uint64_t *page_size);
bool loader_map_stack_for_program(Emulator *emu, EmuLoadedProgram *program, const char *prefix, char *error,
                                  size_t error_size);
bool loader_map_pages(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name,
                      char *error, size_t error_size);
bool loader_range_fits_file(uint64_t offset, uint64_t length, size_t file_size);
bool loader_range_fits_memory(uint64_t address, uint64_t length, const Memory *memory);
bool loader_range_overlaps_device(const Memory *memory, uint64_t address, uint64_t length,
                                  const EmuDeviceRange **out_device);
bool loader_ranges_overlap(uint64_t a_start, uint64_t a_length, uint64_t b_start, uint64_t b_length);
bool loader_record_segment(EmuLoadedProgram *program, const char *error_prefix, const char *segment_kind,
                           const char *name, uint64_t vaddr, uint64_t file_offset, uint64_t mem_size,
                           uint64_t file_size, uint32_t flags, uint32_t section_count, char *error,
                           size_t error_size);
bool loader_entry_is_mapped(const EmuLoadedProgram *program, uint64_t entry);
bool loader_is_elf_magic(const uint8_t *bytes, size_t file_size);
bool loader_load_elf64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                  char *error, size_t error_size);
bool loader_is_macho_magic(const uint8_t *bytes, size_t file_size);
bool loader_load_macho64_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                    char *error, size_t error_size);
bool loader_load_raw_from_bytes(Emulator *emu, const uint8_t *bytes, size_t file_size, EmuLoadedProgram *program,
                                char *error, size_t error_size);

#endif

#include "loader_internal.h"

#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>


uint64_t loader_align_down_to_page(uint64_t value) {
    return value & ~((uint64_t)EMU_PAGE_SIZE - 1ull);
}


bool loader_align_up_to_page(uint64_t value, uint64_t *out) {
    uint64_t mask = (uint64_t)EMU_PAGE_SIZE - 1ull;
    if ((value & mask) == 0) {
        *out = value;
        return true;
    }
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}


bool loader_page_range_for_segment(uint64_t vaddr, uint64_t mem_size, uint64_t *page_start, uint64_t *page_size) {
    uint64_t segment_end = 0;
    uint64_t page_end = 0;
    if (!emu_checked_add_u64(vaddr, mem_size, &segment_end)) {
        return false;
    }
    *page_start = loader_align_down_to_page(vaddr);
    if (!loader_align_up_to_page(segment_end, &page_end) || page_end < *page_start) {
        return false;
    }
    *page_size = page_end - *page_start;
    return true;
}


bool loader_map_pages(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions,
                             const char *name, char *error, size_t error_size) {
    if ((permissions & (EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC)) == 0) {
        snprintf(error, error_size, "memory map error: mapping must have at least one permission");
        return false;
    }

    uint64_t end = 0;
    if (!emu_checked_add_u64(address, length, &end)) {
        snprintf(error, error_size,
                 "memory map error: mapping outside memory: address=0x%016" PRIx64 " length=0x%016" PRIx64,
                 address, length);
        return false;
    }

    for (size_t i = 0; i < memory->mapping_count; i++) {
        EmuMemoryMapping *existing = &memory->mappings[i];
        uint64_t existing_end = existing->start + existing->size;
        bool overlaps = address < existing_end && existing->start < end;
        if (!overlaps) {
            continue;
        }
        if (strcmp(existing->name, name) != 0) {
            break;
        }

        uint64_t merged_start = address < existing->start ? address : existing->start;
        uint64_t merged_end = end > existing_end ? end : existing_end;
        for (size_t j = 0; j < memory->mapping_count; j++) {
            const EmuMemoryMapping *other = &memory->mappings[j];
            uint64_t other_end = other->start + other->size;
            if (j != i && merged_start < other_end && other->start < merged_end) {
                snprintf(error, error_size,
                         "memory map error: overlapping mappings are not allowed: 0x%016" PRIx64
                         "-0x%016" PRIx64 " overlaps 0x%016" PRIx64 "-0x%016" PRIx64 " name=%s",
                         merged_start, merged_end, other->start, other_end,
                         other->name[0] == '\0' ? "mapping" : other->name);
                return false;
            }
        }

        existing->start = merged_start;
        existing->size = merged_end - merged_start;
        existing->permissions |= permissions;
        return true;
    }

    return memory_map_range(memory, address, length, permissions, name, error, error_size);
}


bool loader_range_fits_file(uint64_t offset, uint64_t length, size_t file_size) {
    uint64_t end = 0;
    return emu_checked_add_u64(offset, length, &end) && end <= (uint64_t)file_size;
}


bool loader_range_fits_memory(uint64_t address, uint64_t length, const Memory *memory) {
    uint64_t end = 0;
    return emu_checked_add_u64(address, length, &end) && end <= (uint64_t)memory->size;
}


bool loader_range_overlaps_device(const Memory *memory, uint64_t address, uint64_t length,
                                  const EmuDeviceRange **out_device) {
    uint64_t end = 0;
    if (length == 0 || !emu_checked_add_u64(address, length, &end)) {
        return false;
    }
    for (size_t i = 0; i < memory->devices.range_count; i++) {
        const EmuDeviceRange *device = &memory->devices.ranges[i];
        uint64_t device_end = device->start + device->size;
        if (address < device_end && device->start < end) {
            if (out_device != NULL) {
                *out_device = device;
            }
            return true;
        }
    }
    return false;
}


bool loader_ranges_overlap(uint64_t a_start, uint64_t a_length, uint64_t b_start, uint64_t b_length) {
    uint64_t a_end = 0;
    uint64_t b_end = 0;
    if (!emu_checked_add_u64(a_start, a_length, &a_end) || !emu_checked_add_u64(b_start, b_length, &b_end)) {
        return true;
    }
    return a_start < b_end && b_start < a_end;
}


bool loader_record_segment(EmuLoadedProgram *program, const char *error_prefix, const char *segment_kind,
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
        if (loader_ranges_overlap(vaddr, mem_size, existing->vaddr, existing->mem_size)) {
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


bool loader_entry_is_mapped(const EmuLoadedProgram *program, uint64_t entry) {
    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        uint64_t end = segment->vaddr + segment->mem_size;
        if (entry >= segment->vaddr && entry < end) {
            return true;
        }
    }
    return false;
}

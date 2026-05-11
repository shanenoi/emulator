#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *out = left + right;
    return true;
}

static uint64_t align_down(uint64_t value) {
    return value & ~((uint64_t)EMU_PAGE_SIZE - 1ull);
}

static bool align_up(uint64_t value, uint64_t *out) {
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

static bool check_bounds(const Memory *memory, uint64_t address, uint64_t width, char *error, size_t error_size) {
    if (memory == NULL || memory->bytes == NULL) {
        snprintf(error, error_size, "memory is not initialized");
        return false;
    }

    uint64_t end = 0;
    if (!checked_add_u64(address, width, &end) || address > (uint64_t)memory->size || width > (uint64_t)memory->size ||
        end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "memory access out of bounds: address=0x%016" PRIx64 " width=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, width, memory->size);
        return false;
    }

    return true;
}

static bool check_permission(const Memory *memory, uint64_t address, uint64_t width, uint8_t required,
                             const char *fault_name, char *error, size_t error_size) {
    if (!check_bounds(memory, address, width, error, error_size)) {
        return false;
    }

    if (!memory->permissions_enabled || width == 0) {
        return true;
    }

    uint64_t end = address + width;
    size_t first_page = (size_t)(address / EMU_PAGE_SIZE);
    size_t last_page = (size_t)((end - 1u) / EMU_PAGE_SIZE);
    for (size_t page = first_page; page <= last_page; page++) {
        uint8_t permissions = memory->page_permissions[page];
        if (permissions == 0) {
            snprintf(error, error_size,
                     "memory fault: unmapped access: address=0x%016" PRIx64 " width=0x%016" PRIx64,
                     address, width);
            return false;
        }
        if ((permissions & required) != required) {
            char have[4];
            memory_format_permissions(permissions, have, sizeof(have));
            snprintf(error, error_size,
                     "memory fault: %s permission denied: address=0x%016" PRIx64 " width=0x%016" PRIx64
                     " page=0x%016" PRIx64 " permissions=%s",
                     fault_name, address, width, (uint64_t)page * EMU_PAGE_SIZE, have);
            return false;
        }
    }

    return true;
}

bool memory_init(Memory *memory, size_t size, char *error, size_t error_size) {
    if (memory == NULL) {
        snprintf(error, error_size, "memory pointer is null");
        return false;
    }

    memset(memory, 0, sizeof(*memory));
    memory->bytes = calloc(size, 1);
    memory->size = size;
    if (memory->bytes == NULL) {
        snprintf(error, error_size, "failed to allocate %zu bytes of memory: %s", size, strerror(errno));
        memory->size = 0;
        return false;
    }

    memory->page_count = (size + EMU_PAGE_SIZE - 1u) / EMU_PAGE_SIZE;
    memory->page_permissions = calloc(memory->page_count, sizeof(memory->page_permissions[0]));
    if (memory->page_permissions == NULL) {
        snprintf(error, error_size, "failed to allocate %zu memory page descriptors: %s", memory->page_count,
                 strerror(errno));
        free(memory->bytes);
        memory->bytes = NULL;
        memory->size = 0;
        memory->page_count = 0;
        return false;
    }

    /*
     * Early lessons use Memory directly as a simple flat byte array. Keep that
     * teaching path permissive until a program loader explicitly installs the
     * v1.2 page map.
     */
    memory->permissions_enabled = false;
    return true;
}

void memory_free(Memory *memory) {
    if (memory == NULL) {
        return;
    }

    free(memory->bytes);
    free(memory->page_permissions);
    memory->bytes = NULL;
    memory->page_permissions = NULL;
    memory->size = 0;
    memory->page_count = 0;
    memory->mapping_count = 0;
    memory->permissions_enabled = false;
}

void memory_clear_mappings(Memory *memory) {
    if (memory == NULL) {
        return;
    }
    if (memory->page_permissions != NULL) {
        memset(memory->page_permissions, 0, memory->page_count * sizeof(memory->page_permissions[0]));
    }
    memory->mapping_count = 0;
    memset(memory->mappings, 0, sizeof(memory->mappings));
    memory->permissions_enabled = true;
}

bool memory_map_range(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name,
                      char *error, size_t error_size) {
    if (memory == NULL || memory->bytes == NULL || memory->page_permissions == NULL) {
        snprintf(error, error_size, "memory is not initialized");
        return false;
    }
    if (length == 0) {
        snprintf(error, error_size, "memory map error: zero-length mapping is invalid");
        return false;
    }
    if ((permissions & (EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC)) == 0) {
        snprintf(error, error_size, "memory map error: mapping must have at least one permission");
        return false;
    }

    uint64_t end = 0;
    if (!checked_add_u64(address, length, &end) || end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "memory map error: mapping outside memory: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }

    uint64_t map_start = align_down(address);
    uint64_t map_end = 0;
    if (!align_up(end, &map_end) || map_end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "memory map error: aligned mapping outside memory: address=0x%016" PRIx64 " length=0x%016" PRIx64,
                 address, length);
        return false;
    }

    if (memory->mapping_count >= EMU_MAX_MEMORY_MAPPINGS) {
        snprintf(error, error_size, "memory map error: too many mappings; max=%u", EMU_MAX_MEMORY_MAPPINGS);
        return false;
    }

    size_t first_page = (size_t)(map_start / EMU_PAGE_SIZE);
    size_t page_count = (size_t)((map_end - map_start) / EMU_PAGE_SIZE);
    for (size_t page = 0; page < page_count; page++) {
        memory->page_permissions[first_page + page] |= permissions;
    }

    EmuMemoryMapping *mapping = &memory->mappings[memory->mapping_count++];
    mapping->start = map_start;
    mapping->size = map_end - map_start;
    mapping->permissions = permissions;
    snprintf(mapping->name, sizeof(mapping->name), "%s", name != NULL && name[0] != '\0' ? name : "mapping");
    memory->permissions_enabled = true;
    return true;
}

bool memory_map_stack(Memory *memory, uint64_t stack_top, uint64_t stack_size, char *error, size_t error_size) {
    uint64_t guard_size = (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE;
    if (stack_size == 0 || (stack_size & ((uint64_t)EMU_PAGE_SIZE - 1ull)) != 0) {
        snprintf(error, error_size, "memory map error: stack size must be a nonzero multiple of page size");
        return false;
    }
    if (stack_top < stack_size || stack_top - stack_size < guard_size) {
        snprintf(error, error_size,
                 "memory map error: stack and guard page do not fit: stack_top=0x%016" PRIx64
                 " stack_size=0x%016" PRIx64,
                 stack_top, stack_size);
        return false;
    }
    return memory_map_range(memory, stack_top - stack_size, stack_size, EMU_MAP_READ | EMU_MAP_WRITE, "stack", error,
                            error_size);
}

bool memory_check_read(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return check_permission(memory, address, length, EMU_MAP_READ, "read", error, error_size);
}

bool memory_check_write(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return check_permission(memory, address, length, EMU_MAP_WRITE, "write", error, error_size);
}

bool memory_check_execute(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return check_permission(memory, address, length, EMU_MAP_EXEC, "execute", error, error_size);
}

bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size) {
    if (!memory_check_read(memory, address, 1, error, error_size)) {
        return false;
    }
    *out = memory->bytes[address];
    return true;
}

bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size) {
    if (!memory_check_write(memory, address, 1, error, error_size)) {
        return false;
    }
    memory->bytes[address] = value;
    return true;
}

bool memory_read32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size) {
    if (!memory_check_read(memory, address, 4, error, error_size)) {
        return false;
    }

    *out = (uint32_t)memory->bytes[address] | ((uint32_t)memory->bytes[address + 1] << 8) |
           ((uint32_t)memory->bytes[address + 2] << 16) | ((uint32_t)memory->bytes[address + 3] << 24);
    return true;
}

bool memory_fetch32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size) {
    if (!memory_check_execute(memory, address, 4, error, error_size)) {
        return false;
    }

    *out = (uint32_t)memory->bytes[address] | ((uint32_t)memory->bytes[address + 1] << 8) |
           ((uint32_t)memory->bytes[address + 2] << 16) | ((uint32_t)memory->bytes[address + 3] << 24);
    return true;
}

bool memory_write32(Memory *memory, uint64_t address, uint32_t value, char *error, size_t error_size) {
    if (!memory_check_write(memory, address, 4, error, error_size)) {
        return false;
    }

    memory->bytes[address] = (uint8_t)(value & 0xffu);
    memory->bytes[address + 1] = (uint8_t)((value >> 8) & 0xffu);
    memory->bytes[address + 2] = (uint8_t)((value >> 16) & 0xffu);
    memory->bytes[address + 3] = (uint8_t)((value >> 24) & 0xffu);
    return true;
}

bool memory_read64(const Memory *memory, uint64_t address, uint64_t *out, char *error, size_t error_size) {
    if (!memory_check_read(memory, address, 8, error, error_size)) {
        return false;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        value |= ((uint64_t)memory->bytes[address + i]) << (8u * i);
    }
    *out = value;
    return true;
}

bool memory_write64(Memory *memory, uint64_t address, uint64_t value, char *error, size_t error_size) {
    if (!memory_check_write(memory, address, 8, error, error_size)) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        memory->bytes[address + i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
    return true;
}

void memory_format_permissions(uint8_t permissions, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%c%c%c", (permissions & EMU_MAP_READ) != 0 ? 'r' : '-',
             (permissions & EMU_MAP_WRITE) != 0 ? 'w' : '-',
             (permissions & EMU_MAP_EXEC) != 0 ? 'x' : '-');
}

const EmuMemoryMapping *memory_find_mapping(const Memory *memory, uint64_t address) {
    if (memory == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *mapping = &memory->mappings[i];
        if (address >= mapping->start && address < mapping->start + mapping->size) {
            return mapping;
        }
    }
    return NULL;
}

void memory_print_mappings(const Memory *memory, FILE *stream) {
    fprintf(stream, "mappings: %zu\n", memory != NULL ? memory->mapping_count : 0u);
    if (memory == NULL) {
        return;
    }
    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *mapping = &memory->mappings[i];
        char perms[4];
        memory_format_permissions(mapping->permissions, perms, sizeof(perms));
        fprintf(stream, "  [%zu] %s 0x%016" PRIx64 "-0x%016" PRIx64 " size=0x%016" PRIx64 " name=%s\n", i,
                perms, mapping->start, mapping->start + mapping->size, mapping->size,
                mapping->name[0] == '\0' ? "mapping" : mapping->name);
    }
}

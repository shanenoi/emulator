#include "emulator.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool check_range(const Memory *memory, uint64_t address, size_t width, char *error, size_t error_size) {
    if (memory == NULL || memory->bytes == NULL) {
        snprintf(error, error_size, "memory is not initialized");
        return false;
    }

    if (address > memory->size || width > memory->size || address + width > memory->size || address + width < address) {
        snprintf(error, error_size, "memory access out of bounds: address=0x%016llx width=%zu memory_size=0x%zx",
                 (unsigned long long)address, width, memory->size);
        return false;
    }

    return true;
}

bool memory_init(Memory *memory, size_t size, char *error, size_t error_size) {
    if (memory == NULL) {
        snprintf(error, error_size, "memory pointer is null");
        return false;
    }

    memory->bytes = calloc(size, 1);
    memory->size = size;
    if (memory->bytes == NULL) {
        snprintf(error, error_size, "failed to allocate %zu bytes of memory: %s", size, strerror(errno));
        memory->size = 0;
        return false;
    }

    return true;
}

void memory_free(Memory *memory) {
    if (memory == NULL) {
        return;
    }

    free(memory->bytes);
    memory->bytes = NULL;
    memory->size = 0;
}

bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size) {
    if (!check_range(memory, address, 1, error, error_size)) {
        return false;
    }
    *out = memory->bytes[address];
    return true;
}

bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size) {
    if (!check_range(memory, address, 1, error, error_size)) {
        return false;
    }
    memory->bytes[address] = value;
    return true;
}

bool memory_read32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size) {
    if (!check_range(memory, address, 4, error, error_size)) {
        return false;
    }

    *out = (uint32_t)memory->bytes[address]
         | ((uint32_t)memory->bytes[address + 1] << 8)
         | ((uint32_t)memory->bytes[address + 2] << 16)
         | ((uint32_t)memory->bytes[address + 3] << 24);
    return true;
}

bool memory_write32(Memory *memory, uint64_t address, uint32_t value, char *error, size_t error_size) {
    if (!check_range(memory, address, 4, error, error_size)) {
        return false;
    }

    memory->bytes[address] = (uint8_t)(value & 0xffu);
    memory->bytes[address + 1] = (uint8_t)((value >> 8) & 0xffu);
    memory->bytes[address + 2] = (uint8_t)((value >> 16) & 0xffu);
    memory->bytes[address + 3] = (uint8_t)((value >> 24) & 0xffu);
    return true;
}

bool memory_read64(const Memory *memory, uint64_t address, uint64_t *out, char *error, size_t error_size) {
    if (!check_range(memory, address, 8, error, error_size)) {
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
    if (!check_range(memory, address, 8, error, error_size)) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        memory->bytes[address + i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
    return true;
}
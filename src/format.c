#include "emu_format.h"

#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>

bool emu_format_memory_dump(const Memory *memory, uint64_t address, uint64_t length, FILE *stream, char *error,
                            size_t error_size) {
    uint64_t end = 0;
    if (!emu_checked_add_u64(address, length, &end) || address > (uint64_t)memory->size ||
        length > (uint64_t)memory->size || end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "dump range out of bounds: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }
    if (!memory_check_read(memory, address, length, error, error_size)) {
        char cause[512];
        snprintf(cause, sizeof(cause), "%s", error);
        snprintf(error, error_size,
                 "dump range is not readable: address=0x%016" PRIx64 " length=0x%016" PRIx64 " (%.300s)",
                 address, length, cause);
        return false;
    }

    fprintf(stream, "memory dump address=0x%016" PRIx64 " length=0x%016" PRIx64 "\n", address, length);
    for (uint64_t offset = 0; offset < length; offset += 16u) {
        fprintf(stream, "0x%016" PRIx64 ":", address + offset);
        uint64_t line_len = length - offset < 16u ? length - offset : 16u;
        for (uint64_t i = 0; i < line_len; i++) {
            uint8_t value = 0;
            if (!memory_read8(memory, address + offset + i, &value, error, error_size)) {
                return false;
            }
            fprintf(stream, " %02x", value);
        }
        fprintf(stream, "\n");
    }
    return true;
}

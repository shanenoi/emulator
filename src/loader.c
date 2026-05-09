#include "emulator.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size) {
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

    if (load_address > memory->size || file_size > memory->size - (size_t)load_address) {
        snprintf(error, error_size,
                 "loader error: file size 0x%zx does not fit at load address 0x%016llx; available=0x%zx",
                 file_size, (unsigned long long)load_address,
                 load_address <= memory->size ? memory->size - (size_t)load_address : 0u);
        fclose(file);
        return false;
    }

    size_t bytes_read = fread(&memory->bytes[load_address], 1, file_size, file);
    if (bytes_read != file_size) {
        snprintf(error, error_size, "loader error: expected 0x%zx bytes but read 0x%zx from '%s'", file_size,
                 bytes_read, path);
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}
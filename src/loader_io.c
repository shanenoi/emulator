#define _POSIX_C_SOURCE 200809L

#include "loader_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


bool loader_read_file(const char *path, uint8_t **out_bytes, size_t *out_size, char *error, size_t error_size) {
    *out_bytes = NULL;
    *out_size = 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "loader error: failed to open '%s': %s", path, strerror(errno));
        return false;
    }

    struct stat st;
    if (fstat(fileno(file), &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(error, error_size, "loader error: failed to read '%s': is a directory", path);
        fclose(file);
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

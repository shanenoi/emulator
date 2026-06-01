#include "loader_internal.h"

#include <stdlib.h>

bool emulator_load_program(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size) {
    uint8_t *bytes = NULL;
    size_t file_size = 0;
    if (!loader_read_file(path, &bytes, &file_size, error, error_size)) {
        return false;
    }

    memory_reset_devices(&emu->memory);

    bool ok = false;
    if (loader_is_elf_magic(bytes, file_size)) {
        ok = loader_load_elf64_from_bytes(emu, bytes, file_size, program, error, error_size);
    } else if (loader_is_macho_magic(bytes, file_size)) {
        ok = loader_load_macho64_from_bytes(emu, bytes, file_size, program, error, error_size);
    } else {
        ok = loader_load_raw_from_bytes(emu, bytes, file_size, program, error, error_size);
    }

    free(bytes);
    return ok;
}

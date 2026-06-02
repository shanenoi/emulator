#ifndef EMU_MMIO_H
#define EMU_MMIO_H

#include "memory.h"

#include <stddef.h>
#include <stdint.h>

bool mmio_check_access(const EmuDeviceRange *device, uint64_t address, uint64_t width, uint8_t required,
                       char *error, size_t error_size);

bool mmio_read(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width, uint64_t *out,
               char *error, size_t error_size);
bool mmio_write(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width, uint64_t value,
                char *error, size_t error_size);

#endif

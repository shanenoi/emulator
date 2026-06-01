#ifndef EMU_FORMAT_H
#define EMU_FORMAT_H

#include "emulator.h"

bool emu_format_memory_dump(const Memory *memory, uint64_t address, uint64_t length, FILE *stream, char *error,
                            size_t error_size);

#endif

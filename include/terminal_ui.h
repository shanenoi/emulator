#ifndef TERMINAL_UI_H
#define TERMINAL_UI_H

#include "cli_options.h"
#include "memory.h"

#include <stddef.h>
#include <stdio.h>

typedef struct Emulator Emulator;

void terminal_render_screen_dump(const Memory *memory, const char *border, FILE *stream);
EmuStatus terminal_run_interactive(Emulator *emu, const CliOptions *options, char *error, size_t error_size);
EmuStatus terminal_run_frames(Emulator *emu, const CliOptions *options, char *error, size_t error_size);

#endif

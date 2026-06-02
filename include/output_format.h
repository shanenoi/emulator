#ifndef OUTPUT_FORMAT_H
#define OUTPUT_FORMAT_H

#include "emulator.h"

#include <stdio.h>

void cli_print_program_info(const EmuLoadedProgram *program, const Memory *memory, FILE *stream);
void cli_print_exception_info(const Emulator *emu, FILE *stream);
void cli_print_toy_kernel_info(const Emulator *emu, FILE *stream);
int cli_guest_or_success_status(const Emulator *emu);
int cli_toy_kernel_error_status(const Emulator *emu);

#endif

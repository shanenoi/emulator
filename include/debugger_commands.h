#ifndef DEBUGGER_COMMANDS_H
#define DEBUGGER_COMMANDS_H

#include "emulator.h"

#include <stdio.h>

int debugger_repl(Debugger *debugger, FILE *input, FILE *output, FILE *error_stream);

#endif

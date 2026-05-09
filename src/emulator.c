#include "emulator.h"

#include <stdio.h>
#include <string.h>

bool emulator_init(Emulator *emu, char *error, size_t error_size) {
    memset(emu, 0, sizeof(*emu));
    if (!memory_init(&emu->memory, EMU_MEMORY_SIZE, error, error_size)) {
        return false;
    }
    cpu_init(&emu->cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);
    emu->instruction_limit = EMU_DEFAULT_INSTRUCTION_LIMIT;
    emu->trace_enabled = false;
    emu->trace_stream = stdout;
    return true;
}

void emulator_free(Emulator *emu) {
    memory_free(&emu->memory);
}

EmuStatus emulator_run(Emulator *emu, char *error, size_t error_size) {
    while (!emu->cpu.halted) {
        if (emu->cpu.instructions_executed >= emu->instruction_limit) {
            snprintf(error, error_size, "execution error: instruction limit reached: 0x%016llx",
                     (unsigned long long)emu->instruction_limit);
            return EMU_ERROR;
        }

        if (emu->trace_enabled) {
            FILE *stream = emu->trace_stream != NULL ? emu->trace_stream : stdout;
            fprintf(stream, "trace pc=0x%016llx\n", (unsigned long long)emu->cpu.pc);
        }

        EmuStatus status = cpu_step(&emu->cpu, &emu->memory, error, error_size);
        if (status != EMU_OK) {
            return status;
        }
    }

    return EMU_HALTED;
}

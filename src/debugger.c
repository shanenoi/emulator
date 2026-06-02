#include "emulator.h"

#include "emu_format.h"
#include "emu_util.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEBUGGER_LINE_SIZE 256u

static void debugger_clear_break_stop(Debugger *debugger) {
    debugger->stopped_at_breakpoint = false;
    debugger->stopped_breakpoint_address = 0;
}


static void debugger_print_trace_line(const Debugger *debugger, FILE *stream) {
    uint32_t opcode = 0;
    char formatted[256];
    char error[256];

    if (!cpu_fetch(&debugger->emu.cpu, &debugger->emu.memory, &opcode, error, sizeof(error))) {
        fprintf(stream, "trace pc=0x%016" PRIx64 " <fetch-error: %s>\n", debugger->emu.cpu.pc, error);
        return;
    }

    (void)cpu_format_instruction(opcode, debugger->emu.cpu.pc, formatted, sizeof(formatted));
    fprintf(stream, "trace pc=0x%016" PRIx64 " %s\n", debugger->emu.cpu.pc, formatted);
}

bool debugger_init(Debugger *debugger, const char *path, char *error, size_t error_size) {
    memset(debugger, 0, sizeof(*debugger));
    debugger->path = path;
    if (!emulator_init(&debugger->emu, error, error_size)) {
        return false;
    }
    EmuLoadedProgram program;
    if (!emulator_load_program(&debugger->emu, path, &program, error, error_size)) {
        emulator_free(&debugger->emu);
        return false;
    }
    debugger->loaded = true;
    return true;
}

void debugger_free(Debugger *debugger) {
    emulator_free(&debugger->emu);
    debugger->loaded = false;
}

bool debugger_reset(Debugger *debugger, char *error, size_t error_size) {
    const char *path = debugger->path;
    bool trace_enabled = debugger->emu.trace_enabled;
    FILE *trace_stream = debugger->emu.trace_stream;
    DebugBreakpoint breakpoints[EMU_MAX_BREAKPOINTS];
    size_t breakpoint_count = debugger->breakpoint_count;

    memcpy(breakpoints, debugger->breakpoints, sizeof(breakpoints));
    emulator_free(&debugger->emu);
    memset(&debugger->emu, 0, sizeof(debugger->emu));

    if (!emulator_init(&debugger->emu, error, error_size)) {
        return false;
    }
    debugger->emu.trace_enabled = trace_enabled;
    debugger->emu.trace_stream = trace_stream;

    EmuLoadedProgram program;
    if (!emulator_load_program(&debugger->emu, path, &program, error, error_size)) {
        return false;
    }

    memcpy(debugger->breakpoints, breakpoints, sizeof(debugger->breakpoints));
    debugger->breakpoint_count = breakpoint_count;
    debugger->loaded = true;
    debugger_clear_break_stop(debugger);
    return true;
}

bool debugger_add_breakpoint(Debugger *debugger, uint64_t address, char *error, size_t error_size) {
    if ((address & 0x3ull) != 0) {
        snprintf(error, error_size, "breakpoint address must be 4-byte aligned: 0x%016" PRIx64, address);
        return false;
    }
    if (address > (uint64_t)debugger->emu.memory.size || debugger->emu.memory.size - (size_t)address < sizeof(uint32_t)) {
        snprintf(error, error_size, "breakpoint address outside memory: 0x%016" PRIx64, address);
        return false;
    }
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        if (debugger->breakpoints[i].enabled && debugger->breakpoints[i].address == address) {
            snprintf(error, error_size, "breakpoint already exists at 0x%016" PRIx64, address);
            return false;
        }
    }
    if (debugger->breakpoint_count >= EMU_MAX_BREAKPOINTS) {
        snprintf(error, error_size, "maximum breakpoints reached: %u", (unsigned)EMU_MAX_BREAKPOINTS);
        return false;
    }
    debugger->breakpoints[debugger->breakpoint_count].address = address;
    debugger->breakpoints[debugger->breakpoint_count].enabled = true;
    debugger->breakpoint_count++;
    return true;
}

bool debugger_delete_breakpoint(Debugger *debugger, uint64_t address_or_id, char *error, size_t error_size) {
    size_t index = debugger->breakpoint_count;
    if (address_or_id > 0 && address_or_id <= debugger->breakpoint_count) {
        index = (size_t)address_or_id - 1u;
    } else {
        for (size_t i = 0; i < debugger->breakpoint_count; i++) {
            if (debugger->breakpoints[i].address == address_or_id) {
                index = i;
                break;
            }
        }
    }

    if (index >= debugger->breakpoint_count) {
        snprintf(error, error_size, "breakpoint not found: 0x%016" PRIx64, address_or_id);
        return false;
    }

    for (size_t i = index; i + 1u < debugger->breakpoint_count; i++) {
        debugger->breakpoints[i] = debugger->breakpoints[i + 1u];
    }
    debugger->breakpoint_count--;
    return true;
}

bool debugger_has_breakpoint(const Debugger *debugger, uint64_t address) {
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        if (debugger->breakpoints[i].enabled && debugger->breakpoints[i].address == address) {
            return true;
        }
    }
    return false;
}

void debugger_list_breakpoints(const Debugger *debugger, FILE *stream) {
    if (debugger->breakpoint_count == 0) {
        fprintf(stream, "no breakpoints\n");
        return;
    }
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        fprintf(stream, "%zu: 0x%016" PRIx64 "%s\n", i + 1u, debugger->breakpoints[i].address,
                debugger->breakpoints[i].enabled ? "" : " disabled");
    }
}

EmuStatus debugger_step(Debugger *debugger, char *error, size_t error_size) {
    debugger_clear_break_stop(debugger);
    if (debugger->emu.cpu.halted) {
        snprintf(error, error_size, "program is already halted; use run to reset");
        return EMU_ERROR;
    }
    if (debugger->emu.cpu.instructions_executed >= debugger->emu.instruction_limit) {
        snprintf(error, error_size, "execution error: instruction limit reached: 0x%016" PRIx64,
                 debugger->emu.instruction_limit);
        return EMU_ERROR;
    }
    if (debugger->emu.trace_enabled) {
        FILE *stream = debugger->emu.trace_stream != NULL ? debugger->emu.trace_stream : stdout;
        debugger_print_trace_line(debugger, stream);
    }
    return emulator_step(&debugger->emu, error, error_size);
}

EmuStatus debugger_continue(Debugger *debugger, char *error, size_t error_size) {
    bool skip_current_breakpoint = debugger->stopped_at_breakpoint;
    uint64_t skip_address = debugger->stopped_breakpoint_address;

    while (!debugger->emu.cpu.halted) {
        if (debugger->emu.cpu.instructions_executed >= debugger->emu.instruction_limit) {
            snprintf(error, error_size, "execution error: instruction limit reached: 0x%016" PRIx64,
                     debugger->emu.instruction_limit);
            debugger_clear_break_stop(debugger);
            return EMU_ERROR;
        }

        if (debugger_has_breakpoint(debugger, debugger->emu.cpu.pc) &&
            !(skip_current_breakpoint && debugger->emu.cpu.pc == skip_address)) {
            debugger->stopped_at_breakpoint = true;
            debugger->stopped_breakpoint_address = debugger->emu.cpu.pc;
            snprintf(error, error_size, "breakpoint hit at 0x%016" PRIx64, debugger->emu.cpu.pc);
            return EMU_OK;
        }
        skip_current_breakpoint = false;

        EmuStatus status = debugger_step(debugger, error, error_size);
        if (status != EMU_OK) {
            return status;
        }
    }
    return EMU_HALTED;
}

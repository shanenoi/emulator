#ifndef EMULATOR_H
#define EMULATOR_H

/*
 * Public umbrella API for the teaching emulator.
 * Internal implementation files should prefer narrower subsystem headers
 * such as cpu.h, memory.h, loader.h, exceptions.h, and toy_kernel.h.
 */

#include "cpu.h"
#include "exceptions.h"
#include "loader.h"
#include "memory.h"
#include "toy_kernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Emulator {
    Cpu cpu;
    Memory memory;
    uint64_t instruction_limit;
    bool trace_enabled;
    FILE *trace_stream;
    FILE *stdout_stream;
    FILE *stderr_stream;
    uint8_t guest_exit_code;
    bool guest_exited;
    EmuExceptionController exceptions;
    EmuToyKernel toy_kernel;
} Emulator;

typedef struct {
    uint64_t address;
    bool enabled;
} DebugBreakpoint;

typedef struct {
    Emulator emu;
    const char *path;
    DebugBreakpoint breakpoints[EMU_MAX_BREAKPOINTS];
    size_t breakpoint_count;
    bool loaded;
    bool stopped_at_breakpoint;
    uint64_t stopped_breakpoint_address;
} Debugger;

bool emulator_init(Emulator *emu, char *error, size_t error_size);
void emulator_free(Emulator *emu);
bool emulator_configure_exception_vector(Emulator *emu, uint64_t vector_base, char *error, size_t error_size);
void emulator_clear_exception_vector(Emulator *emu);
bool emulator_exception_return(Emulator *emu, char *error, size_t error_size);
EmuStatus emulator_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                   uint64_t interrupted_pc, uint64_t resume_pc, char *error, size_t error_size);
void emulator_set_interrupts_enabled(Emulator *emu, bool enabled);
bool emulator_queue_timer_interrupt(Emulator *emu);
void emulator_configure_timer_interrupt(Emulator *emu, uint64_t interval);
const EmuExceptionContext *emulator_get_exception_context(const Emulator *emu);
bool emulator_enable_toy_kernel(Emulator *emu, bool with_boot_info, char *error, size_t error_size);
bool emulator_toy_kernel_add_task(Emulator *emu, uint64_t entry, char *error, size_t error_size);
const EmuToyKernel *emulator_get_toy_kernel(const Emulator *emu);
EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size);
EmuStatus emulator_run(Emulator *emu, char *error, size_t error_size);

bool debugger_init(Debugger *debugger, const char *path, char *error, size_t error_size);
void debugger_free(Debugger *debugger);
bool debugger_reset(Debugger *debugger, char *error, size_t error_size);
bool debugger_add_breakpoint(Debugger *debugger, uint64_t address, char *error, size_t error_size);
bool debugger_delete_breakpoint(Debugger *debugger, uint64_t address_or_id, char *error, size_t error_size);
bool debugger_has_breakpoint(const Debugger *debugger, uint64_t address);
void debugger_list_breakpoints(const Debugger *debugger, FILE *stream);
EmuStatus debugger_step(Debugger *debugger, char *error, size_t error_size);
EmuStatus debugger_continue(Debugger *debugger, char *error, size_t error_size);
int debugger_repl(Debugger *debugger, FILE *input, FILE *output, FILE *error_stream);

#endif

#ifndef EMU_EXCEPTIONS_H
#define EMU_EXCEPTIONS_H

#include "cpu.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EMU_EXCEPTION_NONE = 0x00u,
    EMU_EXCEPTION_INVALID_INSTRUCTION = 0x01u,
    EMU_EXCEPTION_BREAKPOINT_OR_TRAP = 0x02u,
    EMU_EXCEPTION_SVC_TRAP = 0x03u,
    EMU_EXCEPTION_FETCH_FAULT = 0x10u,
    EMU_EXCEPTION_READ_FAULT = 0x11u,
    EMU_EXCEPTION_WRITE_FAULT = 0x12u,
    EMU_EXCEPTION_EXEC_PERMISSION_FAULT = 0x13u,
    EMU_EXCEPTION_READ_PERMISSION_FAULT = 0x14u,
    EMU_EXCEPTION_WRITE_PERMISSION_FAULT = 0x15u,
    EMU_EXCEPTION_DEVICE_FAULT = 0x20u,
    EMU_EXCEPTION_DIVIDE_BY_ZERO = 0x30u,
    EMU_EXCEPTION_TIMER_INTERRUPT = 0x40u,
} EmuExceptionCause;

typedef struct {
    EmuExceptionCause cause;
    uint64_t fault_address;
    uint64_t interrupted_pc;
    uint64_t resume_pc;
    EmuFlags flags;
    uint32_t depth;
} EmuExceptionContext;

typedef struct EmuExceptionController {
    bool vector_configured;
    uint64_t vector_base;
    bool active;
    bool interrupts_enabled;
    bool pending_timer_interrupt;
    uint64_t timer_interval;
    uint64_t next_timer_deadline;
    bool timer_deadline_relative_pending;
    EmuExceptionContext context;
} EmuExceptionController;

#endif

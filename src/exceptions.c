#include "emulator_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

const char *exception_cause_name(EmuExceptionCause cause) {
    switch (cause) {
    case EMU_EXCEPTION_NONE:
        return "none";
    case EMU_EXCEPTION_INVALID_INSTRUCTION:
        return "invalid-instruction";
    case EMU_EXCEPTION_BREAKPOINT_OR_TRAP:
        return "breakpoint-or-trap";
    case EMU_EXCEPTION_SVC_TRAP:
        return "svc-trap";
    case EMU_EXCEPTION_FETCH_FAULT:
        return "fetch-fault";
    case EMU_EXCEPTION_READ_FAULT:
        return "read-fault";
    case EMU_EXCEPTION_WRITE_FAULT:
        return "write-fault";
    case EMU_EXCEPTION_EXEC_PERMISSION_FAULT:
        return "exec-permission-fault";
    case EMU_EXCEPTION_READ_PERMISSION_FAULT:
        return "read-permission-fault";
    case EMU_EXCEPTION_WRITE_PERMISSION_FAULT:
        return "write-permission-fault";
    case EMU_EXCEPTION_DEVICE_FAULT:
        return "device-fault";
    case EMU_EXCEPTION_DIVIDE_BY_ZERO:
        return "divide-by-zero";
    case EMU_EXCEPTION_TIMER_INTERRUPT:
        return "timer-interrupt";
    }
    return "unknown";
}

static bool validate_exception_vector(const Emulator *emu, uint64_t vector_base, char *error, size_t error_size) {
    if ((vector_base & 0x3ull) != 0) {
        snprintf(error, error_size, "exception vector must be 4-byte aligned: 0x%016" PRIx64, vector_base);
        return false;
    }
    if (memory_find_device(&emu->memory, vector_base) != NULL) {
        snprintf(error, error_size, "exception vector cannot point at a device range: 0x%016" PRIx64, vector_base);
        return false;
    }
    if (!memory_check_execute(&emu->memory, vector_base, sizeof(uint32_t), error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "exception vector is not executable: 0x%016" PRIx64 " (%s)", vector_base,
                 detail);
        return false;
    }
    return true;
}

bool exception_cause_from_memory_fault(const EmuFault *fault, const EmuDecodedInstruction *instruction,
                                              EmuExceptionCause *cause) {
    if (fault != NULL && fault->kind != EMU_FAULT_NONE) {
        if (fault->kind == EMU_FAULT_DEVICE) {
            *cause = EMU_EXCEPTION_DEVICE_FAULT;
            return true;
        }
        if (fault->kind == EMU_FAULT_PERMISSION) {
            switch (fault->access) {
            case EMU_FAULT_ACCESS_EXECUTE:
                *cause = EMU_EXCEPTION_EXEC_PERMISSION_FAULT;
                return true;
            case EMU_FAULT_ACCESS_WRITE:
                *cause = EMU_EXCEPTION_WRITE_PERMISSION_FAULT;
                return true;
            case EMU_FAULT_ACCESS_READ:
                *cause = EMU_EXCEPTION_READ_PERMISSION_FAULT;
                return true;
            case EMU_FAULT_ACCESS_NONE:
                break;
            }
        }
    }

    switch (instruction->kind) {
    case EMU_INST_LDR:
    case EMU_INST_LDUR:
    case EMU_INST_LDP:
        *cause = EMU_EXCEPTION_READ_FAULT;
        return true;
    case EMU_INST_STR:
    case EMU_INST_STUR:
    case EMU_INST_STP:
        *cause = EMU_EXCEPTION_WRITE_FAULT;
        return true;
    default:
        break;
    }
    return false;
}

EmuExceptionCause fetch_exception_cause_from_memory_fault(const EmuFault *fault) {
    if (fault != NULL && fault->kind == EMU_FAULT_PERMISSION && fault->access == EMU_FAULT_ACCESS_EXECUTE) {
        return EMU_EXCEPTION_EXEC_PERMISSION_FAULT;
    }
    return EMU_EXCEPTION_FETCH_FAULT;
}

bool exception_fault_address_from_instruction(const Cpu *cpu, const Memory *memory,
                                                     const EmuDecodedInstruction *instruction, uint64_t *address) {
    uint64_t writeback_value = 0;
    bool has_writeback = false;
    char ignored[256];

    switch (instruction->kind) {
    case EMU_INST_LDR:
    case EMU_INST_LDUR:
    case EMU_INST_LDP:
    case EMU_INST_STR:
    case EMU_INST_STUR:
    case EMU_INST_STP:
        return cpu_calculate_memory_access(cpu, instruction, memory, address, &writeback_value, &has_writeback,
                                           ignored, sizeof(ignored));
    default:
        break;
    }
    return false;
}

EmuStatus sample_pending_interrupt(Emulator *emu, char *error, size_t error_size) {
    if (!emu->exceptions.interrupts_enabled || emu->exceptions.active || !emu->exceptions.pending_timer_interrupt) {
        return EMU_OK;
    }

    if (emu->toy_kernel.enabled && emu->toy_kernel.tasks_started && !emu->exceptions.vector_configured) {
        emu->exceptions.pending_timer_interrupt = false;
        emu->toy_kernel.timer_ticks++;
        (void)toy_kernel_wake_sleepers(emu);
        if (toy_kernel_running_task(&emu->toy_kernel)) {
            emu->toy_kernel.timer_schedules++;
            return toy_kernel_schedule_after_current(emu, emu->cpu.pc, false, 0, error, error_size);
        }
        return EMU_OK;
    }

    if (emu->toy_kernel.enabled && !emu->toy_kernel.tasks_started && !emu->exceptions.vector_configured) {
        emu->exceptions.pending_timer_interrupt = false;
        emu->toy_kernel.timer_ticks++;
        if (emu->trace_enabled) {
            fprintf(emulator_trace_stream(emu),
                    "trace kernel-timer-before-scheduler tick=0x%016" PRIx64 "\n",
                    emu->toy_kernel.timer_ticks);
        }
        return EMU_OK;
    }

    emu->exceptions.pending_timer_interrupt = false;
    return emulator_raise_exception(emu, EMU_EXCEPTION_TIMER_INTERRUPT, 0, emu->cpu.pc, emu->cpu.pc, error,
                                    error_size);
}

EmuStatus maybe_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                       uint64_t interrupted_pc, uint64_t resume_pc, char *error,
                                       size_t error_size) {
    if (emu->toy_kernel.enabled && emu->toy_kernel.tasks_started && !emu->exceptions.active &&
        !emu->exceptions.vector_configured && toy_kernel_running_task(&emu->toy_kernel)) {
        return toy_kernel_fault_current_task(emu, cause, fault_address, interrupted_pc, resume_pc, error,
                                             error_size);
    }
    if (!emu->exceptions.vector_configured) {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", error != NULL ? error : "");
        if (detail[0] != '\0') {
            snprintf(error, error_size,
                     "unhandled exception: cause=%s(0x%02x) fault_address=0x%016" PRIx64
                     " interrupted_pc=0x%016" PRIx64 " (%s)",
                     exception_cause_name(cause), (unsigned)cause, fault_address, interrupted_pc, detail);
        } else {
            snprintf(error, error_size,
                     "unhandled exception: cause=%s(0x%02x) fault_address=0x%016" PRIx64
                     " interrupted_pc=0x%016" PRIx64,
                     exception_cause_name(cause), (unsigned)cause, fault_address, interrupted_pc);
        }
        (void)resume_pc;
        return EMU_ERROR;
    }
    return emulator_raise_exception(emu, cause, fault_address, interrupted_pc, resume_pc, error, error_size);
}

void update_timer_interrupt_deadline(Emulator *emu) {
    if (emu->exceptions.timer_interval == 0 || emu->exceptions.active) {
        return;
    }
    if (emu->exceptions.timer_deadline_relative_pending) {
        emu->exceptions.next_timer_deadline = emu->cpu.instructions_executed + emu->exceptions.timer_interval;
        emu->exceptions.timer_deadline_relative_pending = false;
    }
    if (emu->cpu.instructions_executed >= emu->exceptions.next_timer_deadline) {
        emu->exceptions.pending_timer_interrupt = true;
        emu->exceptions.next_timer_deadline += emu->exceptions.timer_interval;
    }
}

bool emulator_configure_exception_vector(Emulator *emu, uint64_t vector_base, char *error, size_t error_size) {
    if (!validate_exception_vector(emu, vector_base, error, error_size)) {
        return false;
    }
    emu->exceptions.vector_configured = true;
    emu->exceptions.vector_base = vector_base;
    return true;
}

void emulator_clear_exception_vector(Emulator *emu) {
    emu->exceptions.vector_configured = false;
    emu->exceptions.vector_base = 0;
}

const EmuExceptionContext *emulator_get_exception_context(const Emulator *emu) {
    return &emu->exceptions.context;
}

void emulator_set_interrupts_enabled(Emulator *emu, bool enabled) {
    emu->exceptions.interrupts_enabled = enabled;
}

bool emulator_queue_timer_interrupt(Emulator *emu) {
    emu->exceptions.pending_timer_interrupt = true;
    return true;
}

void emulator_configure_timer_interrupt(Emulator *emu, uint64_t interval) {
    emu->exceptions.timer_interval = interval;
    emu->exceptions.next_timer_deadline = interval == 0 ? 0 : emu->cpu.instructions_executed + interval;
    emu->exceptions.timer_deadline_relative_pending = false;
    emu->exceptions.pending_timer_interrupt = false;
}

EmuStatus emulator_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                   uint64_t interrupted_pc, uint64_t resume_pc, char *error, size_t error_size) {
    if (!emu->exceptions.vector_configured) {
        snprintf(error, error_size,
                 "unhandled exception: cause=%s(0x%02x) fault_address=0x%016" PRIx64
                 " interrupted_pc=0x%016" PRIx64,
                 exception_cause_name(cause), (unsigned)cause, fault_address, interrupted_pc);
        return EMU_ERROR;
    }
    if (emu->exceptions.active) {
        snprintf(error, error_size,
                 "double fault: exception %s(0x%02x) while handling %s(0x%02x) at pc=0x%016" PRIx64,
                 exception_cause_name(cause), (unsigned)cause, exception_cause_name(emu->exceptions.context.cause),
                 (unsigned)emu->exceptions.context.cause, emu->cpu.pc);
        return EMU_ERROR;
    }
    if (!validate_exception_vector(emu, emu->exceptions.vector_base, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "double fault: invalid exception vector during %s(0x%02x): %s",
                 exception_cause_name(cause), (unsigned)cause, detail);
        return EMU_ERROR;
    }

    emu->exceptions.active = true;
    emu->exceptions.interrupts_enabled = false;
    emu->exceptions.context = (EmuExceptionContext){cause, fault_address, interrupted_pc, resume_pc, emu->cpu.flags, 1u};
    cpu_write_register(&emu->cpu, 0, true, (uint64_t)cause);
    cpu_write_register(&emu->cpu, 1, true, fault_address);
    cpu_write_register(&emu->cpu, 2, true, interrupted_pc);
    cpu_write_register(&emu->cpu, 3, true, resume_pc);
    emu->cpu.pc = emu->exceptions.vector_base;
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu),
                "trace exception-enter cause=%s(0x%02x) fault=0x%016" PRIx64
                " interrupted_pc=0x%016" PRIx64 " resume_pc=0x%016" PRIx64
                " vector=0x%016" PRIx64 "\n",
                exception_cause_name(cause), (unsigned)cause, fault_address, interrupted_pc, resume_pc,
                emu->exceptions.vector_base);
    }
    return EMU_OK;
}

bool emulator_exception_return(Emulator *emu, char *error, size_t error_size) {
    if (!emu->exceptions.active) {
        snprintf(error, error_size, "eret without active exception");
        return false;
    }
    uint64_t return_pc = cpu_read_register(&emu->cpu, 3);
    if ((return_pc & 0x3ull) != 0) {
        snprintf(error, error_size, "exception return target is misaligned: 0x%016" PRIx64, return_pc);
        return false;
    }
    if (!memory_check_execute(&emu->memory, return_pc, sizeof(uint32_t), error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "exception return target is not executable: 0x%016" PRIx64 " (%s)",
                 return_pc, detail);
        return false;
    }
    emu->exceptions.context.resume_pc = return_pc;
    emu->cpu.flags = emu->exceptions.context.flags;
    emu->cpu.pc = return_pc;
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu),
                "trace exception-return cause=%s(0x%02x) resume_pc=0x%016" PRIx64 "\n",
                exception_cause_name(emu->exceptions.context.cause), (unsigned)emu->exceptions.context.cause,
                emu->exceptions.context.resume_pc);
    }
    emu->exceptions.active = false;
    emu->exceptions.interrupts_enabled = true;
    emu->exceptions.context.depth = 0;
    emu->exceptions.context.cause = EMU_EXCEPTION_NONE;
    return true;
}

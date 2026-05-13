#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void print_trace_line(const Cpu *cpu, const Memory *memory, FILE *stream) {
    uint32_t opcode = 0;
    char formatted[256];
    char error[256];

    if (!cpu_fetch(cpu, memory, &opcode, error, sizeof(error))) {
        fprintf(stream, "trace pc=0x%016" PRIx64 " <fetch-error: %s>\n", cpu->pc, error);
        return;
    }

    (void)cpu_format_instruction(opcode, cpu->pc, formatted, sizeof(formatted));
    fprintf(stream, "trace pc=0x%016" PRIx64 " %s\n", cpu->pc, formatted);
}

static bool check_syscall_buffer(const Memory *memory, uint64_t address, uint64_t length, char *error,
                                 size_t error_size) {
    if (length == 0) {
        return true;
    }
    if (address > (uint64_t)memory->size || length > (uint64_t)memory->size ||
        address + length > (uint64_t)memory->size || address + length < address) {
        snprintf(error, error_size,
                 "syscall write buffer out of bounds: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }
    if (!memory_check_read(memory, address, length, error, error_size)) {
        snprintf(error, error_size, "syscall write buffer is not readable: address=0x%016" PRIx64
                 " length=0x%016" PRIx64, address, length);
        return false;
    }
    return true;
}

static const char *exception_cause_name(EmuExceptionCause cause) {
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

static bool exception_cause_from_memory_error(const char *error_text, const EmuDecodedInstruction *instruction,
                                              EmuExceptionCause *cause) {
    if (strstr(error_text, "device fault:") != NULL) {
        *cause = EMU_EXCEPTION_DEVICE_FAULT;
        return true;
    }
    if (strstr(error_text, "execute permission") != NULL || strstr(error_text, "non-executable") != NULL) {
        *cause = EMU_EXCEPTION_EXEC_PERMISSION_FAULT;
        return true;
    }
    if (strstr(error_text, "read permission") != NULL) {
        *cause = EMU_EXCEPTION_READ_PERMISSION_FAULT;
        return true;
    }
    if (strstr(error_text, "write permission") != NULL) {
        *cause = EMU_EXCEPTION_WRITE_PERMISSION_FAULT;
        return true;
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

static bool exception_fault_address_from_instruction(const Cpu *cpu, const Memory *memory,
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

static EmuStatus sample_pending_interrupt(Emulator *emu, char *error, size_t error_size) {
    if (!emu->exceptions.interrupts_enabled || emu->exceptions.active || !emu->exceptions.pending_timer_interrupt) {
        return EMU_OK;
    }
    emu->exceptions.pending_timer_interrupt = false;
    return emulator_raise_exception(emu, EMU_EXCEPTION_TIMER_INTERRUPT, 0, emu->cpu.pc, emu->cpu.pc, error,
                                    error_size);
}

static EmuStatus maybe_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                       uint64_t interrupted_pc, uint64_t resume_pc, char *error,
                                       size_t error_size) {
    if (!emu->exceptions.vector_configured) {
        (void)cause;
        (void)fault_address;
        (void)interrupted_pc;
        (void)resume_pc;
        (void)error;
        (void)error_size;
        return EMU_ERROR;
    }
    return emulator_raise_exception(emu, cause, fault_address, interrupted_pc, resume_pc, error, error_size);
}

static void update_timer_interrupt_deadline(Emulator *emu) {
    if (emu->exceptions.timer_interval == 0 || emu->exceptions.active) {
        return;
    }
    if (emu->memory.devices.timer_ticks >= emu->exceptions.next_timer_deadline) {
        emu->exceptions.pending_timer_interrupt = true;
        emu->exceptions.next_timer_deadline += emu->exceptions.timer_interval;
    }
}

bool emulator_init(Emulator *emu, char *error, size_t error_size) {
    memset(emu, 0, sizeof(*emu));
    if (!memory_init(&emu->memory, EMU_MEMORY_SIZE, error, error_size)) {
        return false;
    }
    cpu_init(&emu->cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);
    emu->instruction_limit = EMU_DEFAULT_INSTRUCTION_LIMIT;
    emu->trace_enabled = false;
    emu->trace_stream = stdout;
    emu->stdout_stream = stdout;
    emu->stderr_stream = stderr;
    memory_set_uart_output(&emu->memory, emu->stdout_stream);
    emu->guest_exit_code = 0;
    emu->guest_exited = false;
    emu->exceptions.interrupts_enabled = true;
    return true;
}

void emulator_free(Emulator *emu) {
    memory_free(&emu->memory);
}

EmuStatus emulator_handle_syscall(Emulator *emu, const EmuDecodedInstruction *instruction, char *error,
                                  size_t error_size) {
    if (instruction->imm != 0) {
        snprintf(error, error_size, "unsupported svc immediate: #0x%llx", (unsigned long long)instruction->imm);
        return EMU_ERROR;
    }

    uint64_t syscall_number = cpu_read_register(&emu->cpu, 8);
    switch (syscall_number) {
    case EMU_SYSCALL_WRITE: {
        uint64_t fd = cpu_read_register(&emu->cpu, 0);
        uint64_t address = cpu_read_register(&emu->cpu, 1);
        uint64_t length = cpu_read_register(&emu->cpu, 2);
        FILE *stream = NULL;

        if (fd == 1) {
            stream = emu->stdout_stream != NULL ? emu->stdout_stream : stdout;
        } else if (fd == 2) {
            stream = emu->stderr_stream != NULL ? emu->stderr_stream : stderr;
        } else {
            cpu_write_register(&emu->cpu, 0, true, (uint64_t)EMU_SYSCALL_EBADF);
            emu->cpu.pc += 4;
            return EMU_OK;
        }

        if (!check_syscall_buffer(&emu->memory, address, length, error, error_size)) {
            return EMU_ERROR;
        }

        if (length > 0) {
            size_t written = fwrite(&emu->memory.bytes[address], 1, (size_t)length, stream);
            if (written != (size_t)length || fflush(stream) != 0) {
                cpu_write_register(&emu->cpu, 0, true, (uint64_t)EMU_SYSCALL_EIO);
                emu->cpu.pc += 4;
                return EMU_OK;
            }
        }

        cpu_write_register(&emu->cpu, 0, true, length);
        emu->cpu.pc += 4;
        return EMU_OK;
    }

    case EMU_SYSCALL_EXIT: {
        uint64_t status = cpu_read_register(&emu->cpu, 0);
        emu->guest_exit_code = (uint8_t)(status & 0xffu);
        emu->guest_exited = true;
        emu->cpu.halted = true;
        emu->cpu.pc += 4;
        return EMU_HALTED;
    }

    default:
        cpu_write_register(&emu->cpu, 0, true, (uint64_t)EMU_SYSCALL_ENOSYS);
        emu->cpu.pc += 4;
        return EMU_OK;
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
    emu->exceptions.next_timer_deadline = interval;
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
    return EMU_OK;
}

bool emulator_exception_return(Emulator *emu, char *error, size_t error_size) {
    if (!emu->exceptions.active) {
        snprintf(error, error_size, "eret without active exception");
        return false;
    }
    if ((emu->exceptions.context.resume_pc & 0x3ull) != 0) {
        snprintf(error, error_size, "exception return target is misaligned: 0x%016" PRIx64,
                 emu->exceptions.context.resume_pc);
        return false;
    }
    if (!memory_check_execute(&emu->memory, emu->exceptions.context.resume_pc, sizeof(uint32_t), error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "exception return target is not executable: 0x%016" PRIx64 " (%s)",
                 emu->exceptions.context.resume_pc, detail);
        return false;
    }
    emu->cpu.flags = emu->exceptions.context.flags;
    emu->cpu.pc = emu->exceptions.context.resume_pc;
    emu->exceptions.active = false;
    emu->exceptions.interrupts_enabled = true;
    emu->exceptions.context.depth = 0;
    emu->exceptions.context.cause = EMU_EXCEPTION_NONE;
    return true;
}

EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;
    uint64_t current_pc = emu->cpu.pc;

    if (emu->cpu.halted) {
        return EMU_HALTED;
    }

    if (!cpu_fetch(&emu->cpu, &emu->memory, &opcode, error, error_size)) {
        EmuExceptionCause cause = strstr(error, "execute permission") != NULL ||
                                          strstr(error, "non-executable") != NULL
                                      ? EMU_EXCEPTION_EXEC_PERMISSION_FAULT
                                      : EMU_EXCEPTION_FETCH_FAULT;
        return maybe_raise_exception(emu, cause, current_pc, current_pc, current_pc, error, error_size);
    }

    if (!cpu_decode(opcode, &instruction, error, error_size)) {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "decode error at pc=0x%016" PRIx64 ": %s", current_pc, detail);
        return maybe_raise_exception(emu, EMU_EXCEPTION_INVALID_INSTRUCTION, 0, current_pc, current_pc, error,
                                     error_size);
    }

    EmuStatus status = EMU_OK;
    if (instruction.kind == EMU_INST_SVC) {
        status = emulator_handle_syscall(emu, &instruction, error, error_size);
        if (status == EMU_ERROR) {
            char detail[512];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "execution error at pc=0x%016" PRIx64 " opcode=0x%08x: %s", current_pc,
                     opcode, detail);
            return maybe_raise_exception(emu, EMU_EXCEPTION_SVC_TRAP, instruction.imm, current_pc, current_pc + 4u,
                                         error, error_size);
        }
        emu->cpu.instructions_executed++;
        update_timer_interrupt_deadline(emu);
        if (status == EMU_OK) {
            return sample_pending_interrupt(emu, error, error_size);
        }
        return status;
    }

    if (instruction.kind == EMU_INST_BRK) {
        emu->cpu.instructions_executed++;
        return maybe_raise_exception(emu, EMU_EXCEPTION_BREAKPOINT_OR_TRAP, instruction.imm, current_pc,
                                     current_pc + 4u, error, error_size);
    }

    if (instruction.kind == EMU_INST_ERET) {
        if (!emulator_exception_return(emu, error, error_size)) {
            char detail[512];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "execution error at pc=0x%016" PRIx64 " opcode=0x%08x: %s", current_pc,
                     opcode, detail);
            return EMU_ERROR;
        }
        emu->cpu.instructions_executed++;
        return sample_pending_interrupt(emu, error, error_size);
    }

    status = cpu_step(&emu->cpu, &emu->memory, error, error_size);
    if (status == EMU_ERROR) {
        EmuExceptionCause cause = EMU_EXCEPTION_NONE;
        uint64_t fault_address = 0;
        if (exception_fault_address_from_instruction(&emu->cpu, &emu->memory, &instruction, &fault_address) &&
            exception_cause_from_memory_error(error, &instruction, &cause)) {
            return maybe_raise_exception(emu, cause, fault_address, current_pc, current_pc, error, error_size);
        }
        return EMU_ERROR;
    }
    update_timer_interrupt_deadline(emu);
    if (status == EMU_OK) {
        return sample_pending_interrupt(emu, error, error_size);
    }
    return status;
}

EmuStatus emulator_run(Emulator *emu, char *error, size_t error_size) {
    while (!emu->cpu.halted) {
        if (emu->cpu.instructions_executed >= emu->instruction_limit) {
            uint32_t opcode = 0;
            char fetch_error[256];
            if (cpu_fetch(&emu->cpu, &emu->memory, &opcode, fetch_error, sizeof(fetch_error))) {
                snprintf(error, error_size,
                         "execution error at pc=0x%016" PRIx64 " opcode=0x%08x: instruction limit reached: 0x%016llx",
                         emu->cpu.pc, opcode, (unsigned long long)emu->instruction_limit);
            } else {
                snprintf(error, error_size,
                         "execution error at pc=0x%016" PRIx64 ": instruction limit reached: 0x%016llx (%s)",
                         emu->cpu.pc, (unsigned long long)emu->instruction_limit, fetch_error);
            }
            return EMU_ERROR;
        }

        if (emu->trace_enabled) {
            FILE *stream = emu->trace_stream != NULL ? emu->trace_stream : stdout;
            print_trace_line(&emu->cpu, &emu->memory, stream);
        }

        EmuStatus status = emulator_step(emu, error, error_size);
        if (status != EMU_OK) {
            return status;
        }
    }

    return EMU_HALTED;
}

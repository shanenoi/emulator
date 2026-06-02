#include "emulator_internal.h"

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

FILE *emulator_trace_stream(Emulator *emu) {
    return emu->trace_stream != NULL ? emu->trace_stream : stdout;
}

bool emulator_init(Emulator *emu, char *error, size_t error_size) {
    memset(emu, 0, sizeof(*emu));
    if (!memory_init(&emu->memory, EMU_MEMORY_SIZE, error, error_size)) {
        return false;
    }
    emu->memory.devices.exceptions = &emu->exceptions;
    memory_reset_devices(&emu->memory);
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

EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;
    uint64_t current_pc = emu->cpu.pc;

    if (emu->cpu.halted) {
        return EMU_HALTED;
    }

    memory_clear_last_fault(&emu->memory);
    if (!cpu_fetch_decode(&emu->cpu, &emu->memory, &opcode, &instruction, error, error_size)) {
        const EmuFault *fault = memory_last_fault(&emu->memory);
        if (fault != NULL && fault->kind != EMU_FAULT_NONE) {
            EmuExceptionCause cause = fetch_exception_cause_from_memory_fault(fault);
            return maybe_raise_exception(emu, cause, current_pc, current_pc, current_pc, error, error_size);
        }
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
        if (emu->toy_kernel.enabled && !emu->exceptions.active && !emu->toy_kernel.tasks_started &&
            toy_kernel_is_trap(instruction.imm)) {
            return toy_kernel_handle_kernel_brk(emu, &instruction, current_pc, error, error_size);
        }
        if (emu->toy_kernel.enabled && emu->toy_kernel.tasks_started && !emu->exceptions.active &&
            toy_kernel_is_trap(instruction.imm)) {
            return toy_kernel_handle_brk(emu, &instruction, current_pc, error, error_size);
        }
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
            return maybe_raise_exception(emu, EMU_EXCEPTION_INVALID_INSTRUCTION, 0, current_pc, current_pc, error,
                                         error_size);
        }
        emu->cpu.instructions_executed++;
        return sample_pending_interrupt(emu, error, error_size);
    }

    status = cpu_execute_decoded(&emu->cpu, &emu->memory, opcode, &instruction, error, error_size);
    if (status == EMU_ERROR) {
        EmuExceptionCause cause = EMU_EXCEPTION_NONE;
        uint64_t fault_address = 0;
        if (exception_fault_address_from_instruction(&emu->cpu, &emu->memory, &instruction, &fault_address) &&
            exception_cause_from_memory_fault(memory_last_fault(&emu->memory), &instruction, &cause)) {
            const EmuFault *fault = memory_last_fault(&emu->memory);
            if (fault != NULL && fault->kind != EMU_FAULT_NONE) {
                fault_address = fault->address;
            }
            return maybe_raise_exception(emu, cause, fault_address, current_pc, current_pc, error, error_size);
        }
        if (emu->toy_kernel.enabled && emu->toy_kernel.tasks_started && !emu->exceptions.active &&
            !emu->exceptions.vector_configured && toy_kernel_running_task(&emu->toy_kernel)) {
            return toy_kernel_fault_current_task(emu, EMU_EXCEPTION_INVALID_INSTRUCTION, 0, current_pc, current_pc,
                                                 error, error_size);
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

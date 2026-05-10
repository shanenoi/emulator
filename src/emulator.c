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
    return true;
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
    emu->guest_exit_code = 0;
    emu->guest_exited = false;
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

EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;
    uint64_t current_pc = emu->cpu.pc;

    if (emu->cpu.halted) {
        return EMU_HALTED;
    }

    if (!cpu_fetch(&emu->cpu, &emu->memory, &opcode, error, error_size)) {
        return EMU_ERROR;
    }

    if (!cpu_decode(opcode, &instruction, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "decode error at pc=0x%016" PRIx64 ": %s", current_pc, detail);
        return EMU_ERROR;
    }

    EmuStatus status = EMU_OK;
    if (instruction.kind == EMU_INST_SVC) {
        status = emulator_handle_syscall(emu, &instruction, error, error_size);
        if (status == EMU_ERROR) {
            char detail[256];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "execution error at pc=0x%016" PRIx64 " opcode=0x%08x: %s", current_pc,
                     opcode, detail);
            return EMU_ERROR;
        }
        emu->cpu.instructions_executed++;
        return status;
    }

    return cpu_step(&emu->cpu, &emu->memory, error, error_size);
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

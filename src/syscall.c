#include "emulator.h"

#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>

bool check_syscall_buffer(const Memory *memory, uint64_t address, uint64_t length, char *error,
                                 size_t error_size) {
    if (length == 0) {
        return true;
    }
    uint64_t end = 0;
    if (!emu_checked_add_u64(address, length, &end) || address > (uint64_t)memory->size ||
        length > (uint64_t)memory->size || end > (uint64_t)memory->size) {
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

#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define EMU_TOY_KERNEL_BOOT_INFO_ADDRESS 0x00080000ull

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

static FILE *emulator_trace_stream(Emulator *emu) {
    return emu->trace_stream != NULL ? emu->trace_stream : stdout;
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

static void update_timer_interrupt_deadline(Emulator *emu) {
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

static const char *toy_task_state_name(EmuToyTaskState state) {
    switch (state) {
    case EMU_TOY_TASK_EMPTY:
        return "empty";
    case EMU_TOY_TASK_READY:
        return "ready";
    case EMU_TOY_TASK_RUNNING:
        return "running";
    case EMU_TOY_TASK_BLOCKED:
        return "blocked";
    case EMU_TOY_TASK_EXITED:
        return "exited";
    case EMU_TOY_TASK_FAULTED:
        return "faulted";
    }
    return "unknown";
}

static void toy_task_save_from_cpu(EmuToyTask *task, const Cpu *cpu, uint64_t resume_pc) {
    memcpy(task->x, cpu->x, sizeof(task->x));
    task->sp = cpu->sp;
    task->pc = resume_pc;
    task->flags = cpu->flags;
}

static void toy_task_restore_to_cpu(Cpu *cpu, const EmuToyTask *task) {
    memcpy(cpu->x, task->x, sizeof(cpu->x));
    cpu->sp = task->sp;
    cpu->pc = task->pc;
    cpu->flags = task->flags;
    cpu->halted = false;
}

static bool toy_kernel_validate_entry(const Emulator *emu, uint64_t entry, const char *label, char *error,
                                      size_t error_size) {
    if ((entry & 0x3ull) != 0) {
        snprintf(error, error_size, "%s entry must be 4-byte aligned: 0x%016" PRIx64, label, entry);
        return false;
    }
    if (memory_find_device(&emu->memory, entry) != NULL) {
        snprintf(error, error_size, "%s entry cannot point at a device range: 0x%016" PRIx64, label, entry);
        return false;
    }
    if (!memory_check_execute(&emu->memory, entry, sizeof(uint32_t), error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "%s entry is not executable: 0x%016" PRIx64 " (%s)", label, entry, detail);
        return false;
    }
    return true;
}

static bool toy_kernel_write_boot_info(Emulator *emu, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    const uint8_t *bytes = (const uint8_t *)&kernel->boot_info;
    for (size_t i = 0; i < sizeof(kernel->boot_info); i++) {
        if (!memory_write8(&emu->memory, kernel->boot_info_address + i, bytes[i], error, error_size)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "toy kernel boot-info write failed: %s", detail);
            return false;
        }
    }
    return true;
}

static bool toy_kernel_map_and_seed_boot_info(Emulator *emu, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    kernel->boot_info = (EmuToyKernelBootInfo){
        EMU_TOY_KERNEL_BOOT_INFO_MAGIC,
        EMU_TOY_KERNEL_BOOT_INFO_VERSION,
        (uint32_t)sizeof(EmuToyKernelBootInfo),
        0,
        emu->memory.size,
        kernel->kernel_stack_top >= EMU_STACK_SIZE ? kernel->kernel_stack_top - EMU_STACK_SIZE : 0,
        EMU_STACK_SIZE,
        kernel->task_stack_next,
        EMU_TOY_KERNEL_STACK_SIZE,
        kernel->task_count,
        EMU_DEVICE_UART_BASE,
        EMU_DEVICE_TIMER_BASE,
        EMU_DEVICE_RANDOM_BASE,
        EMU_DEVICE_EXCEPTION_BASE,
    };

    if (!memory_map_range(&emu->memory, kernel->boot_info_address, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE,
                          "kernel:boot-info", error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "toy kernel boot-info map failed: %s", detail);
        return false;
    }
    if (!toy_kernel_write_boot_info(emu, error, error_size)) {
        return false;
    }
    cpu_write_register(&emu->cpu, 0, true, kernel->boot_info_address);
    cpu_write_register(&emu->cpu, 1, true, sizeof(kernel->boot_info));
    return true;
}

static bool toy_kernel_has_runnable_task(const EmuToyKernel *kernel) {
    for (size_t i = 0; i < kernel->task_count; i++) {
        if (kernel->tasks[i].state == EMU_TOY_TASK_READY || kernel->tasks[i].state == EMU_TOY_TASK_RUNNING) {
            return true;
        }
    }
    return false;
}

static bool toy_kernel_pick_next_task(EmuToyKernel *kernel, size_t *out) {
    if (kernel->task_count == 0) {
        return false;
    }
    size_t start = kernel->current_task < kernel->task_count ? kernel->current_task + 1u : 0u;
    for (size_t n = 0; n < kernel->task_count; n++) {
        size_t index = (start + n) % kernel->task_count;
        if (kernel->tasks[index].state == EMU_TOY_TASK_READY) {
            *out = index;
            return true;
        }
    }
    return false;
}

static EmuStatus toy_kernel_switch_to_task(Emulator *emu, size_t index, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (index >= kernel->task_count || kernel->tasks[index].state != EMU_TOY_TASK_READY) {
        snprintf(error, error_size, "toy kernel scheduler selected non-ready task: index=%zu state=%s", index,
                 index < kernel->task_count ? toy_task_state_name(kernel->tasks[index].state) : "out-of-range");
        return EMU_ERROR;
    }
    kernel->current_task = index;
    kernel->tasks[index].state = EMU_TOY_TASK_RUNNING;
    toy_task_restore_to_cpu(&emu->cpu, &kernel->tasks[index]);
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu), "trace kernel-task-switch index=%zu pc=0x%016" PRIx64
                                            " sp=0x%016" PRIx64 "\n",
                index, emu->cpu.pc, emu->cpu.sp);
    }
    return EMU_OK;
}

static EmuStatus toy_kernel_start_tasks_if_needed(Emulator *emu, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!kernel->enabled || kernel->tasks_started || kernel->task_count == 0) {
        return EMU_OK;
    }
    kernel->tasks_started = true;
    kernel->current_task = kernel->task_count;
    size_t next = 0;
    if (!toy_kernel_pick_next_task(kernel, &next)) {
        snprintf(error, error_size, "toy kernel has tasks but none are ready");
        return EMU_ERROR;
    }
    return toy_kernel_switch_to_task(emu, next, error, error_size);
}

static EmuStatus toy_kernel_schedule_after_current(Emulator *emu, uint64_t resume_pc, bool exiting,
                                                   uint64_t exit_code, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    size_t current = kernel->current_task;
    if (current >= kernel->task_count || kernel->tasks[current].state != EMU_TOY_TASK_RUNNING) {
        snprintf(error, error_size, "toy kernel trap without a running task");
        return EMU_ERROR;
    }

    if (exiting) {
        toy_task_save_from_cpu(&kernel->tasks[current], &emu->cpu, resume_pc);
        kernel->tasks[current].state = EMU_TOY_TASK_EXITED;
        kernel->tasks[current].exit_code = exit_code;
    } else {
        toy_task_save_from_cpu(&kernel->tasks[current], &emu->cpu, resume_pc);
        kernel->tasks[current].state = EMU_TOY_TASK_READY;
        kernel->tasks[current].yields++;
    }

    if (!toy_kernel_has_runnable_task(kernel)) {
        emu->guest_exited = true;
        emu->guest_exit_code = 0;
        emu->cpu.halted = true;
        emu->cpu.pc = resume_pc;
        if (emu->trace_enabled) {
            fprintf(emulator_trace_stream(emu), "trace kernel-complete tasks=%zu\n", kernel->task_count);
        }
        return EMU_HALTED;
    }

    size_t next = 0;
    if (!toy_kernel_pick_next_task(kernel, &next)) {
        if (kernel->tasks[current].state == EMU_TOY_TASK_READY) {
            next = current;
        } else {
            snprintf(error, error_size, "toy kernel scheduler found no ready task after task exit");
            return EMU_ERROR;
        }
    }
    return toy_kernel_switch_to_task(emu, next, error, error_size);
}

static EmuStatus toy_kernel_handle_brk(Emulator *emu, const EmuDecodedInstruction *instruction, uint64_t current_pc,
                                       char *error, size_t error_size) {
    switch (instruction->imm) {
    case EMU_TOY_KERNEL_TRAP_YIELD:
        emu->cpu.instructions_executed++;
        return toy_kernel_schedule_after_current(emu, current_pc + 4u, false, 0, error, error_size);
    case EMU_TOY_KERNEL_TRAP_TASK_EXIT:
        emu->cpu.instructions_executed++;
        return toy_kernel_schedule_after_current(emu, current_pc + 4u, true, cpu_read_register(&emu->cpu, 0), error,
                                                 error_size);
    case EMU_TOY_KERNEL_TRAP_PANIC:
        emu->cpu.instructions_executed++;
        emu->toy_kernel.panic = true;
        emu->toy_kernel.panic_code = cpu_read_register(&emu->cpu, 0);
        snprintf(error, error_size, "toy kernel panic: code=0x%016" PRIx64, emu->toy_kernel.panic_code);
        return EMU_ERROR;
    case EMU_TOY_KERNEL_TRAP_CONSOLE_WRITE: {
        uint64_t address = cpu_read_register(&emu->cpu, 0);
        uint64_t length = cpu_read_register(&emu->cpu, 1);
        if (!check_syscall_buffer(&emu->memory, address, length, error, error_size)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "%s", error);
            snprintf(error, error_size, "toy kernel console buffer is invalid: %s", detail);
            return EMU_ERROR;
        }
        if (length > 0) {
            FILE *stream = emu->stdout_stream != NULL ? emu->stdout_stream : stdout;
            if (fwrite(&emu->memory.bytes[address], 1, (size_t)length, stream) != (size_t)length ||
                fflush(stream) != 0) {
                snprintf(error, error_size, "toy kernel console write failed");
                return EMU_ERROR;
            }
        }
        cpu_write_register(&emu->cpu, 0, true, length);
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    default:
        break;
    }
    return EMU_ERROR;
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

bool emulator_enable_toy_kernel(Emulator *emu, bool with_boot_info, char *error, size_t error_size) {
    if (!toy_kernel_validate_entry(emu, emu->cpu.pc, "toy kernel", error, error_size)) {
        return false;
    }

    EmuToyKernel *kernel = &emu->toy_kernel;
    memset(kernel, 0, sizeof(*kernel));
    kernel->enabled = true;
    kernel->boot_info_enabled = with_boot_info;
    kernel->boot_info_address = EMU_TOY_KERNEL_BOOT_INFO_ADDRESS;
    kernel->kernel_entry = emu->cpu.pc;
    kernel->kernel_stack_top = emu->cpu.sp;
    kernel->task_stack_next = emu->cpu.sp - EMU_STACK_SIZE - EMU_PAGE_SIZE;

    for (uint8_t i = 0; i < 31u; i++) {
        cpu_write_register(&emu->cpu, i, true, 0);
    }
    emu->cpu.flags = (EmuFlags){false, false, false, false};

    if (with_boot_info && !toy_kernel_map_and_seed_boot_info(emu, error, error_size)) {
        return false;
    }
    return true;
}

bool emulator_toy_kernel_add_task(Emulator *emu, uint64_t entry, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!kernel->enabled) {
        snprintf(error, error_size, "toy kernel profile is not enabled");
        return false;
    }
    if (kernel->tasks_started) {
        snprintf(error, error_size, "cannot add toy kernel task after scheduling has started");
        return false;
    }
    if (kernel->task_count >= EMU_TOY_KERNEL_MAX_TASKS) {
        snprintf(error, error_size, "too many toy kernel tasks: max=%u", EMU_TOY_KERNEL_MAX_TASKS);
        return false;
    }
    if (!toy_kernel_validate_entry(emu, entry, "toy task", error, error_size)) {
        return false;
    }
    uint64_t stack_top = kernel->task_stack_next;
    if (stack_top < EMU_TOY_KERNEL_STACK_SIZE + EMU_PAGE_SIZE) {
        snprintf(error, error_size, "toy kernel task stacks do not fit in guest RAM");
        return false;
    }
    if (!memory_map_stack(&emu->memory, stack_top, EMU_TOY_KERNEL_STACK_SIZE, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "toy kernel task stack map failed: %s", detail);
        return false;
    }

    size_t index = kernel->task_count++;
    EmuToyTask *task = &kernel->tasks[index];
    memset(task, 0, sizeof(*task));
    task->state = EMU_TOY_TASK_READY;
    task->entry = entry;
    task->pc = entry;
    task->sp = stack_top;
    task->stack_base = stack_top - EMU_TOY_KERNEL_STACK_SIZE;
    task->stack_size = EMU_TOY_KERNEL_STACK_SIZE;
    kernel->task_stack_next = task->stack_base - EMU_PAGE_SIZE;
    if (kernel->boot_info_enabled) {
        kernel->boot_info.task_count = kernel->task_count;
        kernel->boot_info.task_stack_base = kernel->task_stack_next;
        if (!toy_kernel_write_boot_info(emu, error, error_size)) {
            return false;
        }
    }
    return true;
}

const EmuToyKernel *emulator_get_toy_kernel(const Emulator *emu) {
    return &emu->toy_kernel;
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

EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;
    uint64_t current_pc = emu->cpu.pc;

    if (emu->cpu.halted) {
        return EMU_HALTED;
    }

    EmuStatus kernel_start_status = toy_kernel_start_tasks_if_needed(emu, error, error_size);
    if (kernel_start_status != EMU_OK) {
        return kernel_start_status;
    }
    current_pc = emu->cpu.pc;

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
        if (emu->toy_kernel.enabled && emu->toy_kernel.tasks_started && !emu->exceptions.active &&
            (instruction.imm == EMU_TOY_KERNEL_TRAP_YIELD || instruction.imm == EMU_TOY_KERNEL_TRAP_TASK_EXIT ||
             instruction.imm == EMU_TOY_KERNEL_TRAP_PANIC || instruction.imm == EMU_TOY_KERNEL_TRAP_CONSOLE_WRITE)) {
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

        EmuStatus kernel_start_status = toy_kernel_start_tasks_if_needed(emu, error, error_size);
        if (kernel_start_status != EMU_OK) {
            return kernel_start_status;
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

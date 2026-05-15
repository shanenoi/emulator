#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define EMU_TOY_KERNEL_BOOT_INFO_ADDRESS 0x00080000ull

static bool checked_add_u64_local(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *out = left + right;
    return true;
}

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

static EmuStatus toy_kernel_schedule_after_current(Emulator *emu, uint64_t resume_pc, bool exiting,
                                                   uint64_t exit_code, char *error, size_t error_size);
static EmuStatus toy_kernel_fault_current_task(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                               uint64_t interrupted_pc, uint64_t resume_pc, char *error,
                                               size_t error_size);
static bool toy_kernel_running_task(const EmuToyKernel *kernel);
static void toy_kernel_refresh_descriptor(Emulator *emu, size_t index);

static bool toy_kernel_wake_sleepers(Emulator *emu) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    bool woke_any = false;
    for (size_t i = 0; i < kernel->task_count; i++) {
        EmuToyTask *task = &kernel->tasks[i];
        if (task->state == EMU_TOY_TASK_BLOCKED && task->wake_tick <= kernel->timer_ticks) {
            task->state = EMU_TOY_TASK_READY;
            task->wake_tick = 0;
            toy_kernel_refresh_descriptor(emu, i);
            woke_any = true;
        }
    }
    return woke_any;
}

static bool toy_kernel_running_task(const EmuToyKernel *kernel) {
    return kernel->enabled && kernel->tasks_started && kernel->current_task < kernel->task_count &&
           kernel->tasks[kernel->current_task].state == EMU_TOY_TASK_RUNNING;
}

static EmuStatus sample_pending_interrupt(Emulator *emu, char *error, size_t error_size) {
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

    emu->exceptions.pending_timer_interrupt = false;
    return emulator_raise_exception(emu, EMU_EXCEPTION_TIMER_INTERRUPT, 0, emu->cpu.pc, emu->cpu.pc, error,
                                    error_size);
}

static EmuStatus maybe_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
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

static uint64_t toy_service_error(int64_t value) {
    return (uint64_t)value;
}

static void toy_kernel_refresh_descriptor(Emulator *emu, size_t index) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!kernel->boot_info_enabled || kernel->descriptor_table_address == 0 || index >= EMU_TOY_KERNEL_MAX_TASKS) {
        return;
    }
    const EmuToyTask *task = &kernel->tasks[index];
    EmuToyTaskDescriptor descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.magic = task->state == EMU_TOY_TASK_EMPTY ? 0 : EMU_TOY_KERNEL_DESCRIPTOR_MAGIC;
    descriptor.version = EMU_TOY_KERNEL_DESCRIPTOR_VERSION;
    descriptor.descriptor_size = (uint32_t)sizeof(descriptor);
    descriptor.task_id = task->task_id;
    descriptor.state = (uint64_t)task->state;
    descriptor.entry_pc = task->entry;
    descriptor.saved_pc = task->pc;
    descriptor.saved_sp = task->sp;
    descriptor.stack_base = task->stack_base;
    descriptor.stack_size = task->stack_size;
    descriptor.exit_code = task->exit_code;
    descriptor.fault_cause = (uint64_t)task->fault_cause;
    descriptor.fault_address = task->fault_address;
    descriptor.wake_tick = task->wake_tick;
    descriptor.switch_count = task->switch_count;
    memcpy(descriptor.name, task->name, sizeof(descriptor.name));

    uint64_t address = kernel->descriptor_table_address + index * sizeof(descriptor);
    const uint8_t *bytes = (const uint8_t *)&descriptor;
    char ignored[256];
    for (size_t i = 0; i < sizeof(descriptor); i++) {
        (void)memory_write8(&emu->memory, address + i, bytes[i], ignored, sizeof(ignored));
    }
}

static void toy_kernel_refresh_all_descriptors(Emulator *emu) {
    for (size_t i = 0; i < EMU_TOY_KERNEL_MAX_TASKS; i++) {
        toy_kernel_refresh_descriptor(emu, i);
    }
}

static bool toy_kernel_range_overlaps(uint64_t base, uint64_t size, uint64_t other_base, uint64_t other_size) {
    uint64_t end = 0;
    uint64_t other_end = 0;
    if (size == 0 || other_size == 0 || !checked_add_u64_local(base, size, &end) ||
        !checked_add_u64_local(other_base, other_size, &other_end)) {
        return false;
    }
    return base < other_end && other_base < end;
}

static bool toy_kernel_stack_overlaps_live_task(const EmuToyKernel *kernel, uint64_t stack_base, uint64_t stack_size) {
    for (size_t i = 0; i < kernel->task_count; i++) {
        const EmuToyTask *task = &kernel->tasks[i];
        if (task->state != EMU_TOY_TASK_EMPTY && task->stack_size != 0 &&
            toy_kernel_range_overlaps(stack_base, stack_size, task->stack_base, task->stack_size)) {
            return true;
        }
    }
    return false;
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

static bool toy_kernel_map_descriptor_table(Emulator *emu, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    kernel->descriptor_table_address = EMU_TOY_KERNEL_DESCRIPTOR_TABLE_ADDRESS;
    if (!memory_map_range(&emu->memory, kernel->descriptor_table_address, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE,
                          "kernel:task-descriptors", error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "toy kernel descriptor table map failed: %s", detail);
        return false;
    }
    toy_kernel_refresh_all_descriptors(emu);
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
        kernel->descriptor_table_address,
        EMU_TOY_KERNEL_MAX_TASKS,
        sizeof(EmuToyTaskDescriptor),
        EMU_TOY_KERNEL_TRAP_SERVICE,
        EMU_TOY_KERNEL_MAILBOX_SLOTS,
        EMU_TOY_KERNEL_MAILBOX_MESSAGE_SIZE,
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

static bool toy_kernel_has_blocked_task(const EmuToyKernel *kernel) {
    for (size_t i = 0; i < kernel->task_count; i++) {
        if (kernel->tasks[i].state == EMU_TOY_TASK_BLOCKED) {
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

static EmuStatus toy_kernel_complete(Emulator *emu, uint8_t status, uint64_t pc) {
    emu->toy_kernel.completed = true;
    emu->guest_exited = true;
    emu->guest_exit_code = status;
    emu->cpu.halted = true;
    emu->cpu.pc = pc;
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu), "trace kernel-complete tasks=%zu status=%u\n",
                emu->toy_kernel.task_count, (unsigned)status);
    }
    return EMU_HALTED;
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
    kernel->tasks[index].switch_count++;
    toy_kernel_refresh_descriptor(emu, index);
    toy_task_restore_to_cpu(&emu->cpu, &kernel->tasks[index]);
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu), "trace kernel-task-switch index=%zu pc=0x%016" PRIx64
                                            " sp=0x%016" PRIx64 "\n",
                index, emu->cpu.pc, emu->cpu.sp);
    }
    return EMU_OK;
}

static EmuStatus toy_kernel_start_scheduler(Emulator *emu, uint64_t resume_pc, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!kernel->enabled) {
        snprintf(error, error_size, "toy kernel profile is not enabled");
        return EMU_ERROR;
    }
    if (kernel->tasks_started) {
        snprintf(error, error_size, "toy kernel scheduler has already started");
        return EMU_ERROR;
    }
    if (kernel->task_count == 0) {
        return toy_kernel_complete(emu, 0, resume_pc);
    }
    kernel->tasks_started = true;
    kernel->current_task = kernel->task_count;
    size_t next = 0;
    if (!toy_kernel_pick_next_task(kernel, &next)) {
        snprintf(error, error_size, "toy kernel has tasks but none are ready");
        return EMU_ERROR;
    }
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu), "trace kernel-start-tasks count=%zu\n", kernel->task_count);
    }
    return toy_kernel_switch_to_task(emu, next, error, error_size);
}

static bool toy_kernel_stack_is_valid(const Emulator *emu, uint64_t stack_base, uint64_t stack_size,
                                      char *error, size_t error_size) {
    uint64_t stack_end = 0;
    if (stack_base == 0 || stack_size == 0 || (stack_base & 0xfull) != 0 || (stack_size & 0xfull) != 0 ||
        !checked_add_u64_local(stack_base, stack_size, &stack_end)) {
        snprintf(error, error_size, "bad stack: base=0x%016" PRIx64 " size=0x%016" PRIx64, stack_base,
                 stack_size);
        return false;
    }
    if (memory_find_device(&emu->memory, stack_base) != NULL || memory_find_device(&emu->memory, stack_end - 1u) != NULL) {
        snprintf(error, error_size, "bad stack: stack overlaps device range");
        return false;
    }
    if (!memory_check_write(&emu->memory, stack_base, stack_size, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "bad stack: stack is not writable: %.180s", detail);
        return false;
    }
    if (emu->toy_kernel.boot_info_enabled &&
        toy_kernel_range_overlaps(stack_base, stack_size, emu->toy_kernel.boot_info_address, EMU_PAGE_SIZE)) {
        snprintf(error, error_size, "bad stack: stack overlaps boot-info");
        return false;
    }
    if (emu->toy_kernel.boot_info_enabled &&
        toy_kernel_range_overlaps(stack_base, stack_size, emu->toy_kernel.descriptor_table_address, EMU_PAGE_SIZE)) {
        snprintf(error, error_size, "bad stack: stack overlaps descriptor table");
        return false;
    }
    if (toy_kernel_stack_overlaps_live_task(&emu->toy_kernel, stack_base, stack_size)) {
        snprintf(error, error_size, "bad stack: stack overlaps another live task stack");
        return false;
    }
    return true;
}

static bool toy_kernel_init_task(Emulator *emu, uint64_t entry, uint64_t stack_base, uint64_t stack_size,
                                 uint64_t arg0, bool guest_created, const char *name, char *error,
                                 size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!kernel->enabled) {
        snprintf(error, error_size, "toy kernel profile is not enabled");
        return false;
    }
    if (kernel->task_count >= EMU_TOY_KERNEL_MAX_TASKS) {
        snprintf(error, error_size, "too many toy kernel tasks: max=%u", EMU_TOY_KERNEL_MAX_TASKS);
        return false;
    }
    if (!toy_kernel_validate_entry(emu, entry, "toy task", error, error_size)) {
        return false;
    }
    if (!toy_kernel_stack_is_valid(emu, stack_base, stack_size, error, error_size)) {
        return false;
    }

    size_t index = kernel->task_count++;
    EmuToyTask *task = &kernel->tasks[index];
    memset(task, 0, sizeof(*task));
    task->state = EMU_TOY_TASK_READY;
    task->entry = entry;
    task->pc = entry;
    task->sp = stack_base + stack_size;
    task->stack_base = stack_base;
    task->stack_size = stack_size;
    task->task_id = kernel->next_task_id++;
    task->guest_created = guest_created;
    task->x[0] = arg0;
    snprintf(task->name, sizeof(task->name), "%s", name != NULL && name[0] != '\0' ? name :
             (guest_created ? "guest-task" : "host-task"));

    if (kernel->boot_info_enabled) {
        kernel->boot_info.task_count = kernel->task_count;
        if (kernel->task_count == 1u || task->stack_base < kernel->boot_info.task_stack_base) {
            kernel->boot_info.task_stack_base = task->stack_base;
        }
        (void)toy_kernel_write_boot_info(emu, error, error_size);
        toy_kernel_refresh_descriptor(emu, index);
    }
    return true;
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
    toy_kernel_refresh_descriptor(emu, current);

    if (!toy_kernel_has_runnable_task(kernel)) {
        if (toy_kernel_has_blocked_task(kernel)) {
            snprintf(error, error_size, "toy kernel deadlock: no ready tasks and at least one task is blocked");
            return EMU_ERROR;
        }
        uint8_t status = 0;
        for (size_t i = 0; i < kernel->task_count; i++) {
            if (kernel->tasks[i].state == EMU_TOY_TASK_FAULTED) {
                status = 71;
                break;
            }
        }
        return toy_kernel_complete(emu, status, resume_pc);
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

static EmuStatus toy_kernel_sleep_current(Emulator *emu, uint64_t resume_pc, uint64_t ticks, char *error,
                                          size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    size_t current = kernel->current_task;
    if (!toy_kernel_running_task(kernel)) {
        snprintf(error, error_size, "toy kernel sleep without a running task");
        return EMU_ERROR;
    }
    if (ticks == 0) {
        ticks = 1;
    }
    toy_task_save_from_cpu(&kernel->tasks[current], &emu->cpu, resume_pc);
    kernel->tasks[current].state = EMU_TOY_TASK_BLOCKED;
    kernel->tasks[current].wake_tick = kernel->timer_ticks + ticks;
    toy_kernel_refresh_descriptor(emu, current);
    size_t next = 0;
    if (toy_kernel_pick_next_task(kernel, &next)) {
        return toy_kernel_switch_to_task(emu, next, error, error_size);
    }
    emu->cpu.pc = resume_pc;
    emu->cpu.halted = true;
    snprintf(error, error_size, "toy kernel deadlock: all tasks are blocked at timer tick 0x%016" PRIx64,
             kernel->timer_ticks);
    return EMU_ERROR;
}

static EmuStatus toy_kernel_fault_current_task(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                               uint64_t interrupted_pc, uint64_t resume_pc, char *error,
                                               size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    if (!toy_kernel_running_task(kernel)) {
        return EMU_ERROR;
    }
    size_t current = kernel->current_task;
    toy_task_save_from_cpu(&kernel->tasks[current], &emu->cpu, resume_pc);
    kernel->tasks[current].state = EMU_TOY_TASK_FAULTED;
    kernel->tasks[current].fault_cause = cause;
    kernel->tasks[current].fault_address = fault_address;
    toy_kernel_refresh_descriptor(emu, current);
    if (emu->trace_enabled) {
        fprintf(emulator_trace_stream(emu),
                "trace kernel-task-fault index=%zu cause=%s(0x%02x) fault=0x%016" PRIx64
                " interrupted_pc=0x%016" PRIx64 "\n",
                current, exception_cause_name(cause), (unsigned)cause, fault_address, interrupted_pc);
    }
    size_t next = 0;
    if (toy_kernel_pick_next_task(kernel, &next)) {
        return toy_kernel_switch_to_task(emu, next, error, error_size);
    }
    snprintf(error, error_size,
             "toy kernel all runnable tasks finished or faulted; last fault task=%zu cause=%s(0x%02x)"
             " fault_address=0x%016" PRIx64,
             current, exception_cause_name(cause), (unsigned)cause, fault_address);
    return toy_kernel_complete(emu, 71, resume_pc);
}

static bool toy_kernel_find_task_by_id(const EmuToyKernel *kernel, uint64_t task_id, size_t *out) {
    for (size_t i = 0; i < kernel->task_count; i++) {
        if (kernel->tasks[i].state != EMU_TOY_TASK_EMPTY && kernel->tasks[i].task_id == task_id) {
            *out = i;
            return true;
        }
    }
    return false;
}

static void toy_kernel_copy_guest_name(Emulator *emu, uint64_t address, uint64_t length, char out[EMU_TOY_KERNEL_TASK_NAME_SIZE]) {
    memset(out, 0, EMU_TOY_KERNEL_TASK_NAME_SIZE);
    if (address == 0 || length == 0) {
        return;
    }
    uint64_t max = length < EMU_TOY_KERNEL_TASK_NAME_SIZE - 1u ? length : EMU_TOY_KERNEL_TASK_NAME_SIZE - 1u;
    char ignored[256];
    for (uint64_t i = 0; i < max; i++) {
        uint8_t value = 0;
        if (!memory_read8(&emu->memory, address + i, &value, ignored, sizeof(ignored))) {
            break;
        }
        if (value == 0) {
            break;
        }
        out[i] = (char)value;
    }
}

static bool toy_kernel_autostack(Emulator *emu, uint64_t *stack_base, uint64_t *stack_size, char *error,
                                 size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
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
    *stack_base = stack_top - EMU_TOY_KERNEL_STACK_SIZE;
    *stack_size = EMU_TOY_KERNEL_STACK_SIZE;
    kernel->task_stack_next = *stack_base - EMU_PAGE_SIZE;
    return true;
}

static EmuStatus toy_kernel_handle_service(Emulator *emu, uint64_t current_pc, char *error, size_t error_size) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    uint64_t service = cpu_read_register(&emu->cpu, 8);
    bool in_task = toy_kernel_running_task(kernel);

    switch (service) {
    case EMU_TOY_SERVICE_TASK_CREATE: {
        uint64_t entry = cpu_read_register(&emu->cpu, 0);
        uint64_t stack_base = cpu_read_register(&emu->cpu, 1);
        uint64_t stack_size = cpu_read_register(&emu->cpu, 2);
        uint64_t arg0 = cpu_read_register(&emu->cpu, 3);
        uint64_t flags = cpu_read_register(&emu->cpu, 4);
        uint64_t name_ptr = cpu_read_register(&emu->cpu, 5);
        uint64_t name_len = cpu_read_register(&emu->cpu, 6);
        char name[EMU_TOY_KERNEL_TASK_NAME_SIZE];

        if (flags != 0) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_FLAGS));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        if (kernel->task_count >= EMU_TOY_KERNEL_MAX_TASKS) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_NO_SLOT));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        if (!toy_kernel_validate_entry(emu, entry, "toy task", error, error_size)) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ENTRY));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        if (stack_base == 0 && stack_size == 0 && !toy_kernel_autostack(emu, &stack_base, &stack_size, error, error_size)) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_STACK));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        toy_kernel_copy_guest_name(emu, name_ptr, name_len, name);
        if (!toy_kernel_init_task(emu, entry, stack_base, stack_size, arg0, true,
                                  name[0] == '\0' ? "guest-task" : name, error, error_size)) {
            int64_t code = strstr(error, "entry") != NULL ? EMU_TOY_SERVICE_ERR_BAD_ENTRY
                                                           : EMU_TOY_SERVICE_ERR_BAD_STACK;
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(code));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        const EmuToyTask *created = &kernel->tasks[kernel->task_count - 1u];
        if (emu->trace_enabled) {
            fprintf(emulator_trace_stream(emu),
                    "trace kernel-service create-task id=%" PRIu64 " index=%zu entry=0x%016" PRIx64 "\n",
                    created->task_id, kernel->task_count - 1u, entry);
        }
        cpu_write_register(&emu->cpu, 0, true, created->task_id);
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    case EMU_TOY_SERVICE_TASK_YIELD:
        if (!in_task) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        emu->cpu.instructions_executed++;
        return toy_kernel_schedule_after_current(emu, current_pc + 4u, false, 0, error, error_size);
    case EMU_TOY_SERVICE_TASK_EXIT:
        if (!in_task) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        emu->cpu.instructions_executed++;
        return toy_kernel_schedule_after_current(emu, current_pc + 4u, true, cpu_read_register(&emu->cpu, 0), error,
                                                 error_size);
    case EMU_TOY_SERVICE_TASK_SLEEP:
        if (!in_task) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
            emu->cpu.pc = current_pc + 4u;
            emu->cpu.instructions_executed++;
            return EMU_OK;
        }
        emu->cpu.instructions_executed++;
        return toy_kernel_sleep_current(emu, current_pc + 4u, cpu_read_register(&emu->cpu, 0), error, error_size);
    case EMU_TOY_SERVICE_GET_ID:
        cpu_write_register(&emu->cpu, 0, true,
                           in_task ? kernel->tasks[kernel->current_task].task_id
                                   : toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    case EMU_TOY_SERVICE_GET_INFO: {
        uint64_t task_id = cpu_read_register(&emu->cpu, 0);
        size_t index = 0;
        if (!toy_kernel_find_task_by_id(kernel, task_id, &index)) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_NOT_FOUND));
        } else {
            toy_kernel_refresh_descriptor(emu, index);
            cpu_write_register(&emu->cpu, 0, true, kernel->descriptor_table_address + index * sizeof(EmuToyTaskDescriptor));
        }
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    case EMU_TOY_SERVICE_SEND: {
        uint64_t dst_id = cpu_read_register(&emu->cpu, 0);
        uint64_t src = cpu_read_register(&emu->cpu, 1);
        uint64_t len = cpu_read_register(&emu->cpu, 2);
        size_t dst = 0;
        if (len > EMU_TOY_KERNEL_MAILBOX_MESSAGE_SIZE || !toy_kernel_find_task_by_id(kernel, dst_id, &dst) ||
            kernel->tasks[dst].state == EMU_TOY_TASK_EXITED || kernel->tasks[dst].state == EMU_TOY_TASK_FAULTED) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
        } else if (kernel->tasks[dst].mailbox_count >= EMU_TOY_KERNEL_MAILBOX_SLOTS) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_WOULD_BLOCK));
        } else if (len > 0 && !memory_check_read(&emu->memory, src, len, error, error_size)) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
        } else {
            EmuToyTask *task = &kernel->tasks[dst];
            size_t slot = (task->mailbox_head + task->mailbox_count) % EMU_TOY_KERNEL_MAILBOX_SLOTS;
            EmuToyMailboxMessage *message = &task->mailbox[slot];
            memset(message, 0, sizeof(*message));
            message->used = true;
            message->sender_task_id = in_task ? kernel->tasks[kernel->current_task].task_id : UINT64_MAX;
            message->length = len;
            for (uint64_t i = 0; i < len; i++) {
                (void)memory_read8(&emu->memory, src + i, &message->bytes[i], error, error_size);
            }
            task->mailbox_count++;
            cpu_write_register(&emu->cpu, 0, true, EMU_TOY_SERVICE_OK);
        }
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    case EMU_TOY_SERVICE_RECV: {
        uint64_t dst = cpu_read_register(&emu->cpu, 0);
        uint64_t max_len = cpu_read_register(&emu->cpu, 1);
        if (!in_task) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
        } else {
            EmuToyTask *task = &kernel->tasks[kernel->current_task];
            if (task->mailbox_count == 0) {
                cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_WOULD_BLOCK));
            } else {
                EmuToyMailboxMessage *message = &task->mailbox[task->mailbox_head];
                uint64_t copy_len = message->length < max_len ? message->length : max_len;
                if (copy_len > 0 && !memory_check_write(&emu->memory, dst, copy_len, error, error_size)) {
                    cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
                } else {
                    for (uint64_t i = 0; i < copy_len; i++) {
                        (void)memory_write8(&emu->memory, dst + i, message->bytes[i], error, error_size);
                    }
                    task->mailbox_head = (task->mailbox_head + 1u) % EMU_TOY_KERNEL_MAILBOX_SLOTS;
                    task->mailbox_count--;
                    cpu_write_register(&emu->cpu, 0, true, copy_len);
                    cpu_write_register(&emu->cpu, 1, true, message->sender_task_id);
                }
            }
        }
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    case EMU_TOY_SERVICE_CONSOLE_WRITE: {
        uint64_t address = cpu_read_register(&emu->cpu, 0);
        uint64_t length = cpu_read_register(&emu->cpu, 1);
        if (!check_syscall_buffer(&emu->memory, address, length, error, error_size)) {
            cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_BAD_ARGUMENT));
        } else {
            FILE *stream = emu->stdout_stream != NULL ? emu->stdout_stream : stdout;
            if (length > 0 && (fwrite(&emu->memory.bytes[address], 1, (size_t)length, stream) != (size_t)length ||
                               fflush(stream) != 0)) {
                snprintf(error, error_size, "toy kernel console write failed");
                return EMU_ERROR;
            }
            cpu_write_register(&emu->cpu, 0, true, length);
        }
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
    case EMU_TOY_SERVICE_KERNEL_PANIC:
        emu->cpu.instructions_executed++;
        emu->toy_kernel.panic = true;
        emu->toy_kernel.panic_code = cpu_read_register(&emu->cpu, 0);
        snprintf(error, error_size, "toy kernel panic: code=0x%016" PRIx64, emu->toy_kernel.panic_code);
        return EMU_ERROR;
    default:
        cpu_write_register(&emu->cpu, 0, true, toy_service_error(EMU_TOY_SERVICE_ERR_UNKNOWN));
        emu->cpu.pc = current_pc + 4u;
        emu->cpu.instructions_executed++;
        return EMU_OK;
    }
}

static EmuStatus toy_kernel_handle_brk(Emulator *emu, const EmuDecodedInstruction *instruction, uint64_t current_pc,
                                       char *error, size_t error_size) {
    switch (instruction->imm) {
    case EMU_TOY_KERNEL_TRAP_SERVICE:
        return toy_kernel_handle_service(emu, current_pc, error, error_size);
    case EMU_TOY_KERNEL_TRAP_START_TASKS:
        emu->cpu.instructions_executed++;
        return toy_kernel_start_scheduler(emu, current_pc + 4u, error, error_size);
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
        update_timer_interrupt_deadline(emu);
        return sample_pending_interrupt(emu, error, error_size);
    }
    case EMU_TOY_KERNEL_TRAP_SLEEP: {
        emu->cpu.instructions_executed++;
        return toy_kernel_sleep_current(emu, current_pc + 4u, cpu_read_register(&emu->cpu, 0), error, error_size);
    }
    default:
        break;
    }
    snprintf(error, error_size, "unsupported toy-kernel task trap: #0x%llx",
             (unsigned long long)instruction->imm);
    return EMU_ERROR;
}

static bool toy_kernel_is_trap(uint64_t imm) {
    return imm == EMU_TOY_KERNEL_TRAP_YIELD || imm == EMU_TOY_KERNEL_TRAP_TASK_EXIT ||
           imm == EMU_TOY_KERNEL_TRAP_PANIC || imm == EMU_TOY_KERNEL_TRAP_CONSOLE_WRITE ||
           imm == EMU_TOY_KERNEL_TRAP_START_TASKS || imm == EMU_TOY_KERNEL_TRAP_SLEEP ||
           imm == EMU_TOY_KERNEL_TRAP_SERVICE;
}

static EmuStatus toy_kernel_handle_kernel_brk(Emulator *emu, const EmuDecodedInstruction *instruction,
                                              uint64_t current_pc, char *error, size_t error_size) {
    switch (instruction->imm) {
    case EMU_TOY_KERNEL_TRAP_SERVICE:
        return toy_kernel_handle_service(emu, current_pc, error, error_size);
    case EMU_TOY_KERNEL_TRAP_START_TASKS:
        emu->cpu.instructions_executed++;
        return toy_kernel_start_scheduler(emu, current_pc + 4u, error, error_size);
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
    snprintf(error, error_size, "unsupported toy-kernel boot trap: #0x%llx",
             (unsigned long long)instruction->imm);
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
    kernel->descriptor_table_address = EMU_TOY_KERNEL_DESCRIPTOR_TABLE_ADDRESS;
    kernel->kernel_entry = emu->cpu.pc;
    kernel->kernel_stack_top = emu->cpu.sp;
    kernel->task_stack_next = emu->cpu.sp - EMU_STACK_SIZE - EMU_PAGE_SIZE;
    kernel->next_task_id = 0;

    for (uint8_t i = 0; i < 31u; i++) {
        cpu_write_register(&emu->cpu, i, true, 0);
    }
    emu->cpu.flags = (EmuFlags){false, false, false, false};

    if (with_boot_info) {
        if (!toy_kernel_map_descriptor_table(emu, error, error_size)) {
            return false;
        }
        if (!toy_kernel_map_and_seed_boot_info(emu, error, error_size)) {
            return false;
        }
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
    if (!toy_kernel_init_task(emu, entry, stack_top - EMU_TOY_KERNEL_STACK_SIZE, EMU_TOY_KERNEL_STACK_SIZE, 0,
                              false, "host-task", error, error_size)) {
        return false;
    }
    kernel->task_stack_next = stack_top - EMU_TOY_KERNEL_STACK_SIZE - EMU_PAGE_SIZE;
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

    status = cpu_step(&emu->cpu, &emu->memory, error, error_size);
    if (status == EMU_ERROR) {
        EmuExceptionCause cause = EMU_EXCEPTION_NONE;
        uint64_t fault_address = 0;
        if (exception_fault_address_from_instruction(&emu->cpu, &emu->memory, &instruction, &fault_address) &&
            exception_cause_from_memory_error(error, &instruction, &cause)) {
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

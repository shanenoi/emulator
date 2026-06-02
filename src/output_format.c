#include "output_format.h"

#include <inttypes.h>
#include <stdio.h>

static const char *program_format_name(EmuProgramFormat format) {
    switch (format) {
    case EMU_PROGRAM_RAW:
        return "raw";
    case EMU_PROGRAM_ELF64:
        return "elf64";
    case EMU_PROGRAM_MACHO64:
        return "macho64";
    default:
        return "unknown";
    }
}

void cli_print_program_info(const EmuLoadedProgram *program, const Memory *memory, FILE *stream) {
    fprintf(stream, "format: %s\n", program_format_name(program->format));
    fprintf(stream, "entry: 0x%016" PRIx64 "\n", program->entry);
    fprintf(stream, "stack_pointer: 0x%016" PRIx64 "\n", program->stack_pointer);
    fprintf(stream, "segments: %zu\n", program->segment_count);
    for (size_t i = 0; i < program->segment_count; i++) {
        const EmuLoadedSegment *segment = &program->segments[i];
        fprintf(stream,
                "  [%zu] name=%s vaddr=0x%016" PRIx64 " mem_size=0x%016" PRIx64
                " file_offset=0x%016" PRIx64 " file_size=0x%016" PRIx64 " flags=0x%08" PRIx32
                " sections=%" PRIu32 "\n",
                i, segment->name[0] == '\0' ? "-" : segment->name, segment->vaddr, segment->mem_size,
                segment->file_offset, segment->file_size, segment->flags, segment->section_count);
    }
    if (program->format == EMU_PROGRAM_MACHO64) {
        fprintf(stream, "mach_o_load_commands: %" PRIu32 "\n", program->macho_load_command_count);
        fprintf(stream, "mach_o_symbols: %" PRIu32 "\n", program->macho_symbol_count);
        fprintf(stream, "mach_o_indirect_symbols: %" PRIu32 "\n", program->macho_indirect_symbol_count);
        for (uint32_t i = 0; i < program->macho_recorded_symbol_count; i++) {
            fprintf(stream, "  symbol[%" PRIu32 "]: name=%s address=0x%016" PRIx64 "\n", i,
                    program->macho_symbols[i].name[0] == '\0' ? "-" : program->macho_symbols[i].name,
                    program->macho_symbols[i].address);
        }
    }
    memory_print_mappings(memory, stream);
}

int cli_guest_or_success_status(const Emulator *emu) {
    return emu->guest_exited ? (int)emu->guest_exit_code : 0;
}
static const char *toy_task_state_name_for_cli(EmuToyTaskState state) {
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

void cli_print_toy_kernel_info(const Emulator *emu, FILE *stream) {
    const EmuToyKernel *kernel = emulator_get_toy_kernel(emu);
    fprintf(stream, "toy_kernel_enabled: %s\n", kernel->enabled ? "yes" : "no");
    if (!kernel->enabled) {
        return;
    }
    fprintf(stream, "toy_kernel_boot_info: %s\n", kernel->boot_info_enabled ? "yes" : "no");
    fprintf(stream, "toy_kernel_boot_info_address: 0x%016" PRIx64 "\n", kernel->boot_info_address);
    fprintf(stream, "toy_kernel_descriptor_table: 0x%016" PRIx64 "\n", kernel->descriptor_table_address);
    fprintf(stream, "toy_kernel_descriptor_size: %zu\n", sizeof(EmuToyTaskDescriptor));
    fprintf(stream, "toy_kernel_service_trap: 0x%03x\n", EMU_TOY_KERNEL_TRAP_SERVICE);
    fprintf(stream, "toy_kernel_supported_services: 0x%016" PRIx64 "\n", (uint64_t)EMU_TOY_SERVICE_SUPPORTED_MASK);
    fprintf(stream, "toy_kernel_mailbox: slots=%u message_size=%u\n", EMU_TOY_KERNEL_MAILBOX_SLOTS,
            EMU_TOY_KERNEL_MAILBOX_MESSAGE_SIZE);
    fprintf(stream, "toy_kernel_service_calls: 0x%016" PRIx64 "\n", kernel->service_calls);
    fprintf(stream, "toy_kernel_last_service: id=0x%016" PRIx64 " status=%" PRId64 "\n",
            kernel->last_service_id, kernel->last_service_status);
    fprintf(stream, "toy_kernel_mailbox_ops: sends=0x%016" PRIx64 " recvs=0x%016" PRIx64 "\n",
            kernel->mailbox_sends, kernel->mailbox_recvs);
    fprintf(stream, "toy_kernel_next_task_id: 0x%016" PRIx64 "\n", kernel->next_task_id);
    fprintf(stream, "toy_kernel_entry: 0x%016" PRIx64 "\n", kernel->kernel_entry);
    fprintf(stream, "toy_kernel_stack_top: 0x%016" PRIx64 "\n", kernel->kernel_stack_top);
    fprintf(stream, "toy_kernel_task_count: %zu\n", kernel->task_count);
    fprintf(stream, "toy_kernel_current_task: %zu\n", kernel->current_task);
    fprintf(stream, "toy_kernel_tasks_started: %s\n", kernel->tasks_started ? "yes" : "no");
    fprintf(stream, "toy_kernel_completed: %s\n", kernel->completed ? "yes" : "no");
    fprintf(stream, "toy_kernel_timer_ticks: 0x%016" PRIx64 "\n", kernel->timer_ticks);
    fprintf(stream, "toy_kernel_timer_schedules: 0x%016" PRIx64 "\n", kernel->timer_schedules);
    fprintf(stream, "toy_kernel_panic: %s\n", kernel->panic ? "yes" : "no");
    fprintf(stream, "toy_kernel_panic_code: 0x%016" PRIx64 "\n", kernel->panic_code);
    for (size_t i = 0; i < kernel->task_count; i++) {
        const EmuToyTask *task = &kernel->tasks[i];
        fprintf(stream,
                "  task[%zu]: state=%s id=0x%016" PRIx64 " name=%s origin=%s entry=0x%016" PRIx64 " pc=0x%016" PRIx64
                " sp=0x%016" PRIx64 " stack=0x%016" PRIx64 "+0x%016" PRIx64
                " exit=0x%016" PRIx64 " yields=0x%016" PRIx64
                " wake=0x%016" PRIx64 " switches=0x%016" PRIx64
                " mailbox=%zu/%u fault=0x%02x/0x%016" PRIx64 "\n",
                i, toy_task_state_name_for_cli(task->state), task->task_id, task->name[0] == '\0' ? "-" : task->name,
                task->guest_created ? "guest" : "host", task->entry,
                task->pc, task->sp, task->stack_base, task->stack_size, task->exit_code, task->yields,
                task->wake_tick, task->switch_count, task->mailbox_count, EMU_TOY_KERNEL_MAILBOX_SLOTS,
                (unsigned)task->fault_cause, task->fault_address);
    }
}

int cli_toy_kernel_error_status(const Emulator *emu) {
    const EmuToyKernel *kernel = emulator_get_toy_kernel(emu);
    if (!kernel->enabled) {
        return 1;
    }
    if (kernel->panic) {
        return 70;
    }
    for (size_t i = 0; i < kernel->task_count; i++) {
        if (kernel->tasks[i].state == EMU_TOY_TASK_FAULTED) {
            return 71;
        }
    }
    return 1;
}

void cli_print_exception_info(const Emulator *emu, FILE *stream) {
    const EmuExceptionContext *context = emulator_get_exception_context(emu);
    fprintf(stream, "exception_vector_configured: %s\n", emu->exceptions.vector_configured ? "yes" : "no");
    fprintf(stream, "exception_vector_base: 0x%016" PRIx64 "\n", emu->exceptions.vector_base);
    fprintf(stream, "exception_active: %s\n", emu->exceptions.active ? "yes" : "no");
    fprintf(stream, "interrupts_enabled: %s\n", emu->exceptions.interrupts_enabled ? "yes" : "no");
    fprintf(stream, "timer_interrupt_interval: 0x%016" PRIx64 "\n", emu->exceptions.timer_interval);
    fprintf(stream, "next_timer_deadline: 0x%016" PRIx64 "\n", emu->exceptions.next_timer_deadline);
    fprintf(stream, "pending_timer_interrupt: %s\n", emu->exceptions.pending_timer_interrupt ? "yes" : "no");
    fprintf(stream, "exception_cause: 0x%02x\n", (unsigned)context->cause);
    fprintf(stream, "exception_fault_address: 0x%016" PRIx64 "\n", context->fault_address);
    fprintf(stream, "exception_interrupted_pc: 0x%016" PRIx64 "\n", context->interrupted_pc);
    fprintf(stream, "exception_resume_pc: 0x%016" PRIx64 "\n", context->resume_pc);
    fprintf(stream, "exception_depth: %u\n", context->depth);
}

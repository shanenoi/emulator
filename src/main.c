#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "usage: emulator run <program> [options]\n");
    fprintf(stream, "       emulator trace <program> [options]\n");
    fprintf(stream, "       emulator regs <program> [options]\n");
    fprintf(stream, "       emulator dump <program> <address> <length> [options]\n");
    fprintf(stream, "       emulator info <program> [options]\n");
    fprintf(stream, "       emulator debug <program> [options]\n");
    fprintf(stream, "       emulator help\n");
    fprintf(stream, "\n");
    fprintf(stream, "<program> may be a raw little-endian AArch64 binary loaded at 0x%llx\n",
            (unsigned long long)EMU_LOAD_ADDRESS);
    fprintf(stream, "or a supported little-endian AArch64 ELF64 ET_EXEC file,\n");
    fprintf(stream, "or a supported little-endian arm64 Mach-O MH_EXECUTE file.\n");
    fprintf(stream, "v1.3 registers fixed MMIO teaching devices: UART 0x09000000, timer 0x09010000, random 0x09020000.\n");
    fprintf(stream, "v1.4 adds an exception-controller MMIO device at 0x09030000.\n");
    fprintf(stream, "v1.5 adds an opt-in toy-kernel profile with cooperative BRK traps.\n");
    fprintf(stream, "v1.6 adds guest-managed task services through BRK #0x160 with x8 service IDs.\n");
    fprintf(stream, "options: --exception-vector <address>  enable v1.4 vector before running\n");
    fprintf(stream, "         --timer-interrupt <interval> deterministic instruction-count timer interrupt\n");
    fprintf(stream, "         --queue-timer              queue one timer interrupt before running\n");
    fprintf(stream, "         --interrupts on|off        set initial interrupt mask\n");
    fprintf(stream, "         --kernel                   enable v1.5 toy-kernel boot profile\n");
    fprintf(stream, "         --kernel-boot-info         pass a guest-readable boot-info block in x0/x1\n");
    fprintf(stream, "         --kernel-task <address>    add a cooperative task entry point; may repeat\n");
    fprintf(stream, "toy-kernel service IDs: 1=create, 2=yield, 3=exit, 4=sleep, 5=get-id, 6=get-info, 7=send, 8=recv, 9=console, 10=panic.\n");
    fprintf(stream, "info and debugger maps show RAM mappings and MMIO device ranges.\n");
    fprintf(stream, "dump inspects ordinary readable RAM; CPU loads/stores are what trigger device behavior.\n");
    fprintf(stream, "dump <address> and <length> accept decimal or 0x-prefixed hexadecimal values.\n");
    fprintf(stream, "\n");
    fprintf(stream, "Older raw-only lessons also describe the same commands as:\n");
    fprintf(stream, "usage: emulator run <raw-binary>\n");
    fprintf(stream, "       emulator trace <raw-binary>\n");
    fprintf(stream, "       emulator regs <raw-binary>\n");
    fprintf(stream, "       emulator dump <raw-binary> <address> <length>\n");
    fprintf(stream, "       emulator info <raw-binary>\n");
    fprintf(stream, "       emulator debug <raw-binary>\n");
}

typedef struct {
    bool has_exception_vector;
    uint64_t exception_vector;
    bool has_timer_interval;
    uint64_t timer_interval;
    bool queue_timer;
    bool has_interrupts_enabled;
    bool interrupts_enabled;
    bool kernel_enabled;
    bool kernel_boot_info;
    uint64_t kernel_tasks[EMU_TOY_KERNEL_MAX_TASKS];
    size_t kernel_task_count;
} CliOptions;

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

static void print_program_info(const EmuLoadedProgram *program, const Memory *memory, FILE *stream) {
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

static int guest_or_success_status(const Emulator *emu) {
    return emu->guest_exited ? (int)emu->guest_exit_code : 0;
}

static bool parse_u64(const char *text, uint64_t *out) {
    if (text == NULL || text[0] == '\0' || text[0] == '-' || text[0] == '+') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool parse_cli_options(int argc, char **argv, int start, CliOptions *options, char *error, size_t error_size) {
    memset(options, 0, sizeof(*options));
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], "--exception-vector") == 0) {
            if (i + 1 >= argc || !parse_u64(argv[i + 1], &options->exception_vector)) {
                snprintf(error, error_size, "invalid or missing --exception-vector address");
                return false;
            }
            options->has_exception_vector = true;
            i++;
        } else if (strcmp(argv[i], "--timer-interrupt") == 0) {
            if (i + 1 >= argc || !parse_u64(argv[i + 1], &options->timer_interval)) {
                snprintf(error, error_size, "invalid or missing --timer-interrupt interval");
                return false;
            }
            options->has_timer_interval = true;
            i++;
        } else if (strcmp(argv[i], "--queue-timer") == 0) {
            options->queue_timer = true;
        } else if (strcmp(argv[i], "--interrupts") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing --interrupts value; expected on or off");
                return false;
            }
            if (strcmp(argv[i + 1], "on") == 0) {
                options->interrupts_enabled = true;
            } else if (strcmp(argv[i + 1], "off") == 0) {
                options->interrupts_enabled = false;
            } else {
                snprintf(error, error_size, "invalid --interrupts value: %s", argv[i + 1]);
                return false;
            }
            options->has_interrupts_enabled = true;
            i++;
        } else if (strcmp(argv[i], "--kernel") == 0) {
            options->kernel_enabled = true;
        } else if (strcmp(argv[i], "--kernel-boot-info") == 0) {
            options->kernel_enabled = true;
            options->kernel_boot_info = true;
        } else if (strcmp(argv[i], "--kernel-task") == 0) {
            if (options->kernel_task_count >= EMU_TOY_KERNEL_MAX_TASKS) {
                snprintf(error, error_size, "too many --kernel-task entries: max=%u", EMU_TOY_KERNEL_MAX_TASKS);
                return false;
            }
            if (i + 1 >= argc || !parse_u64(argv[i + 1], &options->kernel_tasks[options->kernel_task_count])) {
                snprintf(error, error_size, "invalid or missing --kernel-task address");
                return false;
            }
            options->kernel_enabled = true;
            options->kernel_task_count++;
            i++;
        } else {
            snprintf(error, error_size, "unknown option: %s", argv[i]);
            return false;
        }
    }
    return true;
}

static bool apply_cli_options(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    if (options->has_exception_vector &&
        !emulator_configure_exception_vector(emu, options->exception_vector, error, error_size)) {
        return false;
    }
    if (options->has_timer_interval) {
        emulator_configure_timer_interrupt(emu, options->timer_interval);
    }
    if (options->has_interrupts_enabled) {
        emulator_set_interrupts_enabled(emu, options->interrupts_enabled);
    }
    if (options->queue_timer) {
        emulator_queue_timer_interrupt(emu);
    }
    if (options->kernel_enabled) {
        if (!emulator_enable_toy_kernel(emu, options->kernel_boot_info, error, error_size)) {
            return false;
        }
        for (size_t i = 0; i < options->kernel_task_count; i++) {
            if (!emulator_toy_kernel_add_task(emu, options->kernel_tasks[i], error, error_size)) {
                return false;
            }
        }
    }
    return true;
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

static void print_toy_kernel_info(const Emulator *emu, FILE *stream) {
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

static int toy_kernel_error_status(const Emulator *emu) {
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

static void print_exception_info(const Emulator *emu, FILE *stream) {
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

static bool dump_memory(const Memory *memory, uint64_t address, uint64_t length, FILE *stream, char *error,
                        size_t error_size) {
    if (address > (uint64_t)memory->size || length > (uint64_t)memory->size ||
        address + length > (uint64_t)memory->size || address + length < address) {
        snprintf(error, error_size,
                 "dump range out of bounds: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }
    if (!memory_check_read(memory, address, length, error, error_size)) {
        char cause[512];
        snprintf(cause, sizeof(cause), "%s", error);
        snprintf(error, error_size,
                 "dump range is not readable: address=0x%016" PRIx64 " length=0x%016" PRIx64 " (%.300s)",
                 address, length, cause);
        return false;
    }

    fprintf(stream, "memory dump address=0x%016" PRIx64 " length=0x%016" PRIx64 "\n", address, length);
    for (uint64_t offset = 0; offset < length; offset += 16u) {
        fprintf(stream, "0x%016" PRIx64 ":", address + offset);
        uint64_t line_len = length - offset < 16u ? length - offset : 16u;
        for (uint64_t i = 0; i < line_len; i++) {
            uint8_t value = 0;
            if (!memory_read8(memory, address + offset + i, &value, error, error_size)) {
                return false;
            }
            fprintf(stream, " %02x", value);
        }
        fprintf(stream, "\n");
    }
    return true;
}

int main(int argc, char **argv) {
    char error[512];
    Emulator emu;

    if (argc == 2 && (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "run") != 0 && strcmp(argv[1], "trace") != 0 &&
        strcmp(argv[1], "regs") != 0 && strcmp(argv[1], "dump") != 0 && strcmp(argv[1], "info") != 0 &&
        strcmp(argv[1], "debug") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        print_usage(stderr);
        return 2;
    }

    if (argc < 3) {
        print_usage(stderr);
        return 2;
    }

    int option_start = 3;
    if (strcmp(argv[1], "dump") == 0) {
        option_start = 5;
    }
    if (argc < option_start) {
        print_usage(stderr);
        return 2;
    }

    CliOptions options;
    if (!parse_cli_options(argc, argv, option_start, &options, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "debug") == 0) {
        if (argc < 3) {
            print_usage(stderr);
            return 2;
        }
        Debugger debugger;
        if (!debugger_init(&debugger, argv[2], error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            return 1;
        }
        if (!apply_cli_options(&debugger.emu, &options, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            debugger_free(&debugger);
            return 1;
        }
        int status = debugger_repl(&debugger, stdin, stdout, stderr);
        debugger_free(&debugger);
        return status;
    }

    bool trace_enabled = false;
    bool dump_enabled = false;
    bool regs_only = false;
    uint64_t dump_address = 0;
    uint64_t dump_length = 0;
    if (strcmp(argv[1], "trace") == 0) {
        if (argc < 3) {
            print_usage(stderr);
            return 2;
        }
        trace_enabled = true;
    } else if (strcmp(argv[1], "regs") == 0) {
        if (argc < 3) {
            print_usage(stderr);
            return 2;
        }
        regs_only = true;
    } else if (strcmp(argv[1], "dump") == 0) {
        if (argc < 5) {
            print_usage(stderr);
            return 2;
        }
        dump_enabled = true;
        if (!parse_u64(argv[3], &dump_address) || !parse_u64(argv[4], &dump_length)) {
            fprintf(stderr, "error: invalid dump address or length\n");
            print_usage(stderr);
            return 2;
        }
    } else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            print_usage(stderr);
            return 2;
        }
    } else if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        print_usage(stderr);
        return 2;
    } else if (argc < 3) {
        print_usage(stderr);
        return 2;
    }

    if (!emulator_init(&emu, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    emu.trace_enabled = trace_enabled;
    emu.trace_stream = stdout;

    EmuLoadedProgram program;
    if (!emulator_load_program(&emu, argv[2], &program, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }

    if (!apply_cli_options(&emu, &options, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        print_program_info(&program, &emu.memory, stdout);
        print_exception_info(&emu, stdout);
        print_toy_kernel_info(&emu, stdout);
        emulator_free(&emu);
        return 0;
    }

    EmuStatus status = emulator_run(&emu, error, sizeof(error));
    if (status == EMU_HALTED) {
        if (!regs_only && !emu.guest_exited) {
            printf("halted\n");
        }
        if (!emu.guest_exited || regs_only) {
            cpu_dump(&emu.cpu, stdout);
        }
        if (dump_enabled && !dump_memory(&emu.memory, dump_address, dump_length, stdout, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            emulator_free(&emu);
            return 1;
        }
        int cli_status = guest_or_success_status(&emu);
        emulator_free(&emu);
        return cli_status;
    }

    fprintf(stderr, "error: %s\n", error);
    cpu_dump(&emu.cpu, stderr);
    int cli_error_status = toy_kernel_error_status(&emu);
    emulator_free(&emu);
    return cli_error_status;
}

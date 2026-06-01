#define _POSIX_C_SOURCE 200809L

#include "emulator.h"

#include "emu_format.h"
#include "emu_util.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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
    fprintf(stream, "Keyboard input MMIO is available at 0x09040000; --input and --input-file queue bytes deterministically.\n");
    fprintf(stream, "Terminal/screen MMIO is available at 0x09050000; --screen-dump renders the final buffer.\n");
    fprintf(stream, "Frame tick MMIO is available at 0x09060000; --frames runs deterministic frame slices.\n");
    fprintf(stream, "v1.5 adds an opt-in toy-kernel profile with cooperative BRK traps.\n");
    fprintf(stream, "v1.6 adds guest-managed task services through BRK #0x160 with x8 service IDs.\n");
    fprintf(stream, "options: --exception-vector <address>  enable v1.4 vector before running\n");
    fprintf(stream, "         --timer-interrupt <interval> deterministic instruction-count timer interrupt\n");
    fprintf(stream, "         --queue-timer              queue one timer interrupt before running\n");
    fprintf(stream, "         --interrupts on|off        set initial interrupt mask\n");
    fprintf(stream, "         --kernel                   enable v1.5 toy-kernel boot profile\n");
    fprintf(stream, "         --kernel-boot-info         pass a guest-readable boot-info block in x0/x1\n");
    fprintf(stream, "         --kernel-task <address>    add a cooperative task entry point; may repeat\n");
    fprintf(stream, "         --input <text>             queue scripted keyboard bytes before running\n");
    fprintf(stream, "         --input-file <path>        queue scripted keyboard bytes from a file\n");
    fprintf(stream, "         --screen-size <WxH>        configure terminal screen size, default 80x25\n");
    fprintf(stream, "         --screen-dump             print final terminal screen after execution\n");
    fprintf(stream, "         --screen-border <unicode|ascii|none> choose screen dump border\n");
    fprintf(stream, "         --interactive             run with host keyboard polling and live screen redraw\n");
    fprintf(stream, "         --fps <N>                 interactive frames per second, default %u\n",
            EMU_INTERACTIVE_DEFAULT_FPS);
    fprintf(stream, "         --instructions-per-frame <N> guest instructions per interactive/deterministic frame, default %llu\n",
            (unsigned long long)EMU_INTERACTIVE_DEFAULT_INSTRUCTIONS_PER_FRAME);
    fprintf(stream, "         --frames <N>              run deterministic non-interactive frame slices\n");
    fprintf(stream, "         --quit-key <ctrl-c|esc>   host-only interactive quit key, default ctrl-c\n");
    fprintf(stream, "toy-kernel service IDs: 1=TASK_CREATE, 2=TASK_YIELD, 3=TASK_EXIT, 4=TASK_SLEEP, 5=TASK_GET_ID, 6=TASK_GET_INFO, 7=TASK_SEND, 8=TASK_RECV, 9=CONSOLE_WRITE, 10=KERNEL_PANIC.\n");
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
    const char *input_text;
    const char *input_file;
    bool has_screen_size;
    uint32_t screen_width;
    uint32_t screen_height;
    bool screen_dump;
    const char *screen_border;
    bool interactive;
    bool has_frames;
    uint64_t frames;
    bool has_fps;
    uint32_t fps;
    uint64_t instructions_per_frame;
    const char *quit_key;
} CliOptions;

static bool queue_input_file(Memory *memory, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(error, error_size, "failed to open --input-file %s: %s", path, strerror(errno));
        return false;
    }
    uint8_t buffer[256];
    while (!feof(file)) {
        size_t n = fread(buffer, 1, sizeof(buffer), file);
        if (n > 0) {
            (void)memory_keyboard_enqueue_bytes(memory, buffer, n);
        }
        if (ferror(file)) {
            snprintf(error, error_size, "failed to read --input-file %s", path);
            fclose(file);
            return false;
        }
    }
    fclose(file);
    return true;
}

static bool apply_cli_input(Memory *memory, const CliOptions *options, char *error, size_t error_size) {
    if (options->input_text != NULL) {
        (void)memory_keyboard_enqueue_bytes(memory, (const uint8_t *)options->input_text, strlen(options->input_text));
    }
    if (options->input_file != NULL && !queue_input_file(memory, options->input_file, error, error_size)) {
        return false;
    }
    return true;
}

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

static bool parse_screen_size(const char *text, uint32_t *width, uint32_t *height) {
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    const char *separator = strchr(text, 'x');
    if (separator == NULL) {
        separator = strchr(text, 'X');
    }
    if (separator == NULL || separator == text || separator[1] == '\0') {
        return false;
    }

    char width_text[16];
    size_t width_len = (size_t)(separator - text);
    if (width_len == 0 || width_len >= sizeof(width_text)) {
        return false;
    }
    memcpy(width_text, text, width_len);
    width_text[width_len] = '\0';

    uint64_t parsed_width = 0;
    uint64_t parsed_height = 0;
    if (!emu_parse_u64_strict(width_text, &parsed_width) || !emu_parse_u64_strict(separator + 1, &parsed_height)) {
        return false;
    }
    if (parsed_width < EMU_TERM_MIN_WIDTH || parsed_width > EMU_TERM_MAX_WIDTH ||
        parsed_height < EMU_TERM_MIN_HEIGHT || parsed_height > EMU_TERM_MAX_HEIGHT) {
        return false;
    }
    *width = (uint32_t)parsed_width;
    *height = (uint32_t)parsed_height;
    return true;
}

static bool parse_u32_strict(const char *text, uint32_t *out) {
    uint64_t value = 0;
    if (!emu_parse_u64_strict(text, &value) || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_cli_options(int argc, char **argv, int start, CliOptions *options, char *error, size_t error_size) {
    memset(options, 0, sizeof(*options));
    options->fps = EMU_INTERACTIVE_DEFAULT_FPS;
    options->instructions_per_frame = EMU_INTERACTIVE_DEFAULT_INSTRUCTIONS_PER_FRAME;
    options->quit_key = "ctrl-c";
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], "--exception-vector") == 0) {
            if (i + 1 >= argc || !emu_parse_u64_strict(argv[i + 1], &options->exception_vector)) {
                snprintf(error, error_size, "invalid or missing --exception-vector address");
                return false;
            }
            options->has_exception_vector = true;
            i++;
        } else if (strcmp(argv[i], "--timer-interrupt") == 0) {
            if (i + 1 >= argc || !emu_parse_u64_strict(argv[i + 1], &options->timer_interval)) {
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
            if (i + 1 >= argc || !emu_parse_u64_strict(argv[i + 1], &options->kernel_tasks[options->kernel_task_count])) {
                snprintf(error, error_size, "invalid or missing --kernel-task address");
                return false;
            }
            options->kernel_enabled = true;
            options->kernel_task_count++;
            i++;
        } else if (strcmp(argv[i], "--input") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing --input value");
                return false;
            }
            options->input_text = argv[++i];
        } else if (strcmp(argv[i], "--input-file") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing --input-file path");
                return false;
            }
            options->input_file = argv[++i];
        } else if (strcmp(argv[i], "--screen-size") == 0) {
            if (i + 1 >= argc || !parse_screen_size(argv[i + 1], &options->screen_width, &options->screen_height)) {
                snprintf(error, error_size, "invalid --screen-size value: expected WIDTHxHEIGHT with width %u..%u and height %u..%u",
                         EMU_TERM_MIN_WIDTH, EMU_TERM_MAX_WIDTH, EMU_TERM_MIN_HEIGHT, EMU_TERM_MAX_HEIGHT);
                return false;
            }
            options->has_screen_size = true;
            i++;
        } else if (strcmp(argv[i], "--screen-dump") == 0) {
            options->screen_dump = true;
        } else if (strcmp(argv[i], "--screen-border") == 0) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "missing --screen-border value; expected unicode, ascii, or none");
                return false;
            }
            if (strcmp(argv[i + 1], "unicode") != 0 && strcmp(argv[i + 1], "ascii") != 0 &&
                strcmp(argv[i + 1], "none") != 0) {
                snprintf(error, error_size, "invalid --screen-border value: %s", argv[i + 1]);
                return false;
            }
            options->screen_border = argv[++i];
        } else if (strcmp(argv[i], "--interactive") == 0) {
            options->interactive = true;
        } else if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc || !emu_parse_u64_strict(argv[i + 1], &options->frames) || options->frames == 0) {
                snprintf(error, error_size, "invalid --frames value: expected positive integer");
                return false;
            }
            options->has_frames = true;
            i++;
        } else if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 >= argc || !parse_u32_strict(argv[i + 1], &options->fps) || options->fps == 0) {
                snprintf(error, error_size, "invalid --fps value: expected positive integer");
                return false;
            }
            options->has_fps = true;
            i++;
        } else if (strcmp(argv[i], "--instructions-per-frame") == 0) {
            if (i + 1 >= argc || !emu_parse_u64_strict(argv[i + 1], &options->instructions_per_frame) ||
                options->instructions_per_frame == 0) {
                snprintf(error, error_size, "invalid --instructions-per-frame value: expected positive integer");
                return false;
            }
            i++;
        } else if (strcmp(argv[i], "--quit-key") == 0) {
            if (i + 1 >= argc || (strcmp(argv[i + 1], "ctrl-c") != 0 && strcmp(argv[i + 1], "esc") != 0)) {
                snprintf(error, error_size, "invalid --quit-key value: expected ctrl-c or esc");
                return false;
            }
            options->quit_key = argv[++i];
        } else {
            snprintf(error, error_size, "unknown option: %s", argv[i]);
            return false;
        }
    }
    return true;
}

static bool apply_cli_options(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    if (options->has_screen_size &&
        !memory_terminal_configure(&emu->memory, options->screen_width, options->screen_height, error, error_size)) {
        return false;
    }
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

static void print_repeated(FILE *stream, const char *text, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        fputs(text, stream);
    }
}

static void render_screen_dump(const Memory *memory, const char *border, FILE *stream) {
    uint32_t width = memory_terminal_width(memory);
    uint32_t height = memory_terminal_height(memory);
    const uint8_t *cells = memory_terminal_cells(memory);
    const char *style = border != NULL ? border : "unicode";

    if (strcmp(style, "none") != 0) {
        if (strcmp(style, "ascii") == 0) {
            fputc('+', stream);
            print_repeated(stream, "-", width);
            fputs("+\n", stream);
        } else {
            fputs("┌", stream);
            print_repeated(stream, "─", width);
            fputs("┐\n", stream);
        }
    }

    for (uint32_t y = 0; y < height; y++) {
        if (strcmp(style, "none") != 0) {
            fputs(strcmp(style, "ascii") == 0 ? "|" : "│", stream);
        }
        for (uint32_t x = 0; x < width; x++) {
            fputc((int)cells[(size_t)y * width + x], stream);
        }
        if (strcmp(style, "none") != 0) {
            fputs(strcmp(style, "ascii") == 0 ? "|" : "│", stream);
        }
        fputc('\n', stream);
    }

    if (strcmp(style, "none") != 0) {
        if (strcmp(style, "ascii") == 0) {
            fputc('+', stream);
            print_repeated(stream, "-", width);
            fputs("+\n", stream);
        } else {
            fputs("└", stream);
            print_repeated(stream, "─", width);
            fputs("┘\n", stream);
        }
    }
}

typedef struct {
    struct termios original_termios;
    int original_flags;
    bool active;
} InteractiveTerminalState;

static void interactive_terminal_restore(InteractiveTerminalState *state) {
    if (!state->active) {
        return;
    }
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
    (void)fcntl(STDIN_FILENO, F_SETFL, state->original_flags);
    state->active = false;
}

static bool interactive_terminal_enter(InteractiveTerminalState *state, char *error, size_t error_size) {
    memset(state, 0, sizeof(*state));
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        snprintf(error, error_size, "--interactive requires stdin and stdout to be TTYs");
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &state->original_termios) != 0) {
        snprintf(error, error_size, "failed to read terminal settings: %s", strerror(errno));
        return false;
    }
    state->original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (state->original_flags < 0) {
        snprintf(error, error_size, "failed to read terminal flags: %s", strerror(errno));
        return false;
    }

    struct termios raw = state->original_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        snprintf(error, error_size, "failed to enable raw terminal mode: %s", strerror(errno));
        return false;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, state->original_flags | O_NONBLOCK) != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
        snprintf(error, error_size, "failed to enable nonblocking terminal input: %s", strerror(errno));
        return false;
    }
    state->active = true;
    return true;
}

static bool poll_stdin_byte(uint8_t *out, char *error, size_t error_size) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval timeout = {0, 0};
    int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    if (ready < 0) {
        if (errno == EINTR) {
            return false;
        }
        snprintf(error, error_size, "failed to poll terminal input: %s", strerror(errno));
        return false;
    }
    if (ready == 0 || !FD_ISSET(STDIN_FILENO, &readfds)) {
        return false;
    }
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) {
        return true;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        snprintf(error, error_size, "failed to read terminal input: %s", strerror(errno));
    }
    return false;
}

typedef struct {
    uint8_t escape_buffer[3];
    size_t escape_count;
} InteractiveInputNormalizer;

static bool interactive_normalize_byte(InteractiveInputNormalizer *normalizer, uint8_t byte, const CliOptions *options,
                                       uint8_t *out, bool *has_output, bool *host_quit) {
    *has_output = false;
    *host_quit = false;

    if (normalizer->escape_count > 0) {
        normalizer->escape_buffer[normalizer->escape_count++] = byte;
        if (normalizer->escape_count == 2 && normalizer->escape_buffer[1] != '[') {
            if (strcmp(options->quit_key, "esc") == 0 && normalizer->escape_buffer[0] == EMU_KEY_ESC) {
                *host_quit = true;
                normalizer->escape_count = 0;
                return true;
            }
            *out = normalizer->escape_buffer[0];
            *has_output = true;
            normalizer->escape_count = 0;
            return true;
        }
        if (normalizer->escape_count < 3) {
            return true;
        }
        switch (normalizer->escape_buffer[2]) {
        case 'A':
            *out = EMU_KEY_UP;
            *has_output = true;
            break;
        case 'B':
            *out = EMU_KEY_DOWN;
            *has_output = true;
            break;
        case 'D':
            *out = EMU_KEY_LEFT;
            *has_output = true;
            break;
        case 'C':
            *out = EMU_KEY_RIGHT;
            *has_output = true;
            break;
        default:
            if (strcmp(options->quit_key, "esc") == 0) {
                *host_quit = true;
            } else {
                *out = normalizer->escape_buffer[0];
                *has_output = true;
            }
            break;
        }
        normalizer->escape_count = 0;
        return true;
    }

    if (byte == 3 && strcmp(options->quit_key, "ctrl-c") == 0) {
        *host_quit = true;
        return true;
    }
    if (byte == EMU_KEY_ESC) {
        normalizer->escape_buffer[0] = byte;
        normalizer->escape_count = 1;
        return true;
    }
    if (byte == '\r') {
        byte = '\n';
    }
    *out = byte;
    *has_output = true;
    return true;
}

static void interactive_render_screen(Memory *memory, const CliOptions *options) {
    char ignored[128];
    fputs("\033[H\033[2J", stdout);
    render_screen_dump(memory, options->screen_border, stdout);
    fflush(stdout);
    (void)memory_write32(memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_CLEAR_DIRTY,
                         ignored, sizeof(ignored));
}

static void sleep_frame(uint32_t fps) {
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = (long)(1000000000ull / fps);
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static EmuStatus emulator_run_interactive(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    InteractiveTerminalState terminal;
    if (!interactive_terminal_enter(&terminal, error, error_size)) {
        return EMU_ERROR;
    }

    InteractiveInputNormalizer normalizer = {0};
    bool user_quit = false;
    EmuStatus status = EMU_OK;
    interactive_render_screen(&emu->memory, options);

    while (!user_quit) {
        uint8_t raw = 0;
        while (poll_stdin_byte(&raw, error, error_size)) {
            uint8_t normalized = 0;
            bool has_output = false;
            bool host_quit = false;
            if (!interactive_normalize_byte(&normalizer, raw, options, &normalized, &has_output, &host_quit)) {
                status = EMU_ERROR;
                goto done;
            }
            if (host_quit) {
                user_quit = true;
                break;
            }
            if (has_output) {
                (void)memory_keyboard_enqueue(&emu->memory, normalized);
            }
        }
        if (error[0] != '\0') {
            status = EMU_ERROR;
            goto done;
        }
        if (user_quit) {
            break;
        }

        for (uint64_t i = 0; i < options->instructions_per_frame; i++) {
            status = emulator_step(emu, error, error_size);
            if (status != EMU_OK) {
                goto done;
            }
        }
        memory_advance_frame(&emu->memory);

        if (memory_terminal_dirty(&emu->memory)) {
            interactive_render_screen(&emu->memory, options);
        }
        sleep_frame(options->fps);
    }
    status = EMU_HALTED;

done:
    if (memory_terminal_dirty(&emu->memory)) {
        interactive_render_screen(&emu->memory, options);
    }
    interactive_terminal_restore(&terminal);
    fputc('\n', stdout);
    if (user_quit && status == EMU_HALTED) {
        snprintf(error, error_size, "interactive quit requested");
    }
    return status;
}

static EmuStatus emulator_run_frames(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    EmuStatus status = EMU_OK;
    for (uint64_t frame = 0; frame < options->frames; frame++) {
        for (uint64_t i = 0; i < options->instructions_per_frame; i++) {
            if (emu->cpu.instructions_executed >= emu->instruction_limit) {
                snprintf(error, error_size, "instruction limit reached: 0x%016llx",
                         (unsigned long long)emu->instruction_limit);
                return EMU_ERROR;
            }
            status = emulator_step(emu, error, error_size);
            if (status != EMU_OK) {
                return status;
            }
        }
        memory_advance_frame(&emu->memory);
    }
    return EMU_HALTED;
}

int main(int argc, char **argv) {
    char error[512];
    error[0] = '\0';
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
    if (options.interactive && strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: --interactive is only supported with the run command\n");
        print_usage(stderr);
        return 2;
    }
    if (options.has_frames && strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: --frames is only supported with the run command\n");
        print_usage(stderr);
        return 2;
    }
    if (options.has_frames && options.interactive) {
        fprintf(stderr, "error: --frames cannot be combined with --interactive\n");
        print_usage(stderr);
        return 2;
    }
    if (options.has_fps && !options.interactive) {
        fprintf(stderr, "error: --fps is only supported with --interactive\n");
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
        if (!apply_cli_input(&debugger.emu.memory, &options, error, sizeof(error))) {
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
        if (!emu_parse_u64_strict(argv[3], &dump_address) || !emu_parse_u64_strict(argv[4], &dump_length)) {
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
    if (!apply_cli_input(&emu.memory, &options, error, sizeof(error))) {
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

    error[0] = '\0';
    EmuStatus status = EMU_OK;
    if (options.interactive) {
        status = emulator_run_interactive(&emu, &options, error, sizeof(error));
    } else if (options.has_frames) {
        status = emulator_run_frames(&emu, &options, error, sizeof(error));
    } else {
        status = emulator_run(&emu, error, sizeof(error));
    }
    if (status == EMU_HALTED) {
        if (!regs_only && !emu.guest_exited && !options.interactive) {
            printf("halted\n");
        }
        if ((!emu.guest_exited || regs_only) && !options.interactive) {
            cpu_dump(&emu.cpu, stdout);
        }
        if (dump_enabled && !emu_format_memory_dump(&emu.memory, dump_address, dump_length, stdout, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            emulator_free(&emu);
            return 1;
        }
        if (options.screen_dump) {
            render_screen_dump(&emu.memory, options.screen_border, stdout);
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

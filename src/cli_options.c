#define _POSIX_C_SOURCE 200809L

#include "cli_options.h"

#include "emu_util.h"
#include "emulator.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void cli_print_usage(FILE *stream) {
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

bool cli_apply_input(Memory *memory, const CliOptions *options, char *error, size_t error_size) {
    if (options->input_text != NULL) {
        (void)memory_keyboard_enqueue_bytes(memory, (const uint8_t *)options->input_text, strlen(options->input_text));
    }
    if (options->input_file != NULL && !queue_input_file(memory, options->input_file, error, error_size)) {
        return false;
    }
    return true;
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

bool cli_parse_options(int argc, char **argv, int start, CliOptions *options, char *error, size_t error_size) {
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

bool cli_apply_options(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
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


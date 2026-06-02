#include "cli_run.h"

#include "cli_options.h"
#include "debugger_commands.h"
#include "emu_format.h"
#include "emu_util.h"
#include "emulator.h"
#include "output_format.h"
#include "terminal_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int cli_run_command(int argc, char **argv) {
    char error[512];
    error[0] = '\0';
    Emulator emu;

    if (argc == 2 && (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        cli_print_usage(stdout);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "run") != 0 && strcmp(argv[1], "trace") != 0 &&
        strcmp(argv[1], "regs") != 0 && strcmp(argv[1], "dump") != 0 && strcmp(argv[1], "info") != 0 &&
        strcmp(argv[1], "debug") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        cli_print_usage(stderr);
        return 2;
    }

    if (argc < 3) {
        cli_print_usage(stderr);
        return 2;
    }

    int option_start = 3;
    if (strcmp(argv[1], "dump") == 0) {
        option_start = 5;
    }
    if (argc < option_start) {
        cli_print_usage(stderr);
        return 2;
    }

    CliOptions options;
    if (!cli_parse_options(argc, argv, option_start, &options, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        cli_print_usage(stderr);
        return 2;
    }
    if (options.interactive && strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: --interactive is only supported with the run command\n");
        cli_print_usage(stderr);
        return 2;
    }
    if (options.has_frames && strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: --frames is only supported with the run command\n");
        cli_print_usage(stderr);
        return 2;
    }
    if (options.has_frames && options.interactive) {
        fprintf(stderr, "error: --frames cannot be combined with --interactive\n");
        cli_print_usage(stderr);
        return 2;
    }
    if (options.has_fps && !options.interactive) {
        fprintf(stderr, "error: --fps is only supported with --interactive\n");
        cli_print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "debug") == 0) {
        if (argc < 3) {
            cli_print_usage(stderr);
            return 2;
        }
        Debugger debugger;
        if (!debugger_init(&debugger, argv[2], error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            return 1;
        }
        if (!cli_apply_options(&debugger.emu, &options, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            debugger_free(&debugger);
            return 1;
        }
        if (!cli_apply_input(&debugger.emu.memory, &options, error, sizeof(error))) {
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
            cli_print_usage(stderr);
            return 2;
        }
        trace_enabled = true;
    } else if (strcmp(argv[1], "regs") == 0) {
        if (argc < 3) {
            cli_print_usage(stderr);
            return 2;
        }
        regs_only = true;
    } else if (strcmp(argv[1], "dump") == 0) {
        if (argc < 5) {
            cli_print_usage(stderr);
            return 2;
        }
        dump_enabled = true;
        if (!emu_parse_u64_strict(argv[3], &dump_address) || !emu_parse_u64_strict(argv[4], &dump_length)) {
            fprintf(stderr, "error: invalid dump address or length\n");
            cli_print_usage(stderr);
            return 2;
        }
    } else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            cli_print_usage(stderr);
            return 2;
        }
    } else if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        cli_print_usage(stderr);
        return 2;
    } else if (argc < 3) {
        cli_print_usage(stderr);
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

    if (!cli_apply_options(&emu, &options, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }
    if (!cli_apply_input(&emu.memory, &options, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        cli_print_program_info(&program, &emu.memory, stdout);
        cli_print_exception_info(&emu, stdout);
        cli_print_toy_kernel_info(&emu, stdout);
        emulator_free(&emu);
        return 0;
    }

    error[0] = '\0';
    EmuStatus status = EMU_OK;
    if (options.interactive) {
        status = terminal_run_interactive(&emu, &options, error, sizeof(error));
    } else if (options.has_frames) {
        status = terminal_run_frames(&emu, &options, error, sizeof(error));
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
            terminal_render_screen_dump(&emu.memory, options.screen_border, stdout);
        }
        int cli_status = cli_guest_or_success_status(&emu);
        emulator_free(&emu);
        return cli_status;
    }

    fprintf(stderr, "error: %s\n", error);
    cpu_dump(&emu.cpu, stderr);
    int cli_error_status = cli_toy_kernel_error_status(&emu);
    emulator_free(&emu);
    return cli_error_status;
}

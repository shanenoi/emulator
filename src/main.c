#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "usage: emulator run <program>\n");
    fprintf(stream, "       emulator trace <program>\n");
    fprintf(stream, "       emulator regs <program>\n");
    fprintf(stream, "       emulator dump <program> <address> <length>\n");
    fprintf(stream, "       emulator info <program>\n");
    fprintf(stream, "       emulator debug <program>\n");
    fprintf(stream, "       emulator help\n");
    fprintf(stream, "\n");
    fprintf(stream, "<program> may be a raw little-endian AArch64 binary loaded at 0x%llx\n",
            (unsigned long long)EMU_LOAD_ADDRESS);
    fprintf(stream, "or a supported little-endian AArch64 ELF64 ET_EXEC file,\n");
    fprintf(stream, "or a supported little-endian arm64 Mach-O MH_EXECUTE file.\n");
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

    if (argc != 3 && argc != 5) {
        print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "debug") == 0) {
        if (argc != 3) {
            print_usage(stderr);
            return 2;
        }
        Debugger debugger;
        if (!debugger_init(&debugger, argv[2], error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
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
        if (argc != 3) {
            print_usage(stderr);
            return 2;
        }
        trace_enabled = true;
    } else if (strcmp(argv[1], "regs") == 0) {
        if (argc != 3) {
            print_usage(stderr);
            return 2;
        }
        regs_only = true;
    } else if (strcmp(argv[1], "dump") == 0) {
        if (argc != 5) {
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
        if (argc != 3) {
            print_usage(stderr);
            return 2;
        }
    } else if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        print_usage(stderr);
        return 2;
    } else if (argc != 3) {
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

    if (strcmp(argv[1], "info") == 0) {
        print_program_info(&program, &emu.memory, stdout);
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
    emulator_free(&emu);
    return 1;
}

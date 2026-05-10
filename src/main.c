#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "usage: emulator run <raw-binary>\n");
    fprintf(stream, "       emulator trace <raw-binary>\n");
    fprintf(stream, "       emulator dump <raw-binary> <address> <length>\n");
    fprintf(stream, "\n");
    fprintf(stream, "v0.4 supports raw little-endian AArch64 binaries loaded at 0x%llx.\n",
            (unsigned long long)EMU_LOAD_ADDRESS);
}

static bool parse_u64(const char *text, uint64_t *out) {
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
        snprintf(error, error_size, "dump range out of bounds: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx", address, length, memory->size);
        return false;
    }

    fprintf(stream, "memory dump address=0x%016" PRIx64 " length=0x%016" PRIx64 "\n", address, length);
    for (uint64_t offset = 0; offset < length; offset += 16u) {
        fprintf(stream, "0x%016" PRIx64 ":", address + offset);
        uint64_t line_len = length - offset < 16u ? length - offset : 16u;
        for (uint64_t i = 0; i < line_len; i++) {
            fprintf(stream, " %02x", memory->bytes[address + offset + i]);
        }
        fprintf(stream, "\n");
    }
    return true;
}

int main(int argc, char **argv) {
    char error[512];
    Emulator emu;

    if (argc != 3 && argc != 5) {
        print_usage(stderr);
        return 2;
    }

    bool trace_enabled = false;
    bool dump_enabled = false;
    uint64_t dump_address = 0;
    uint64_t dump_length = 0;
    if (strcmp(argv[1], "trace") == 0) {
        if (argc != 3) {
            print_usage(stderr);
            return 2;
        }
        trace_enabled = true;
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

    if (!load_raw_binary(&emu.memory, argv[2], EMU_LOAD_ADDRESS, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }

    EmuStatus status = emulator_run(&emu, error, sizeof(error));
    if (status == EMU_HALTED) {
        printf("halted\n");
        cpu_dump(&emu.cpu, stdout);
        if (dump_enabled && !dump_memory(&emu.memory, dump_address, dump_length, stdout, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error);
            emulator_free(&emu);
            return 1;
        }
        emulator_free(&emu);
        return 0;
    }

    fprintf(stderr, "error: %s\n", error);
    cpu_dump(&emu.cpu, stderr);
    emulator_free(&emu);
    return 1;
}

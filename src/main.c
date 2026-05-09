#include "emulator.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream, "usage: emulator run <raw-binary>\n");
    fprintf(stream, "\n");
    fprintf(stream, "v0.1 supports raw little-endian AArch64 binaries loaded at 0x%llx.\n",
            (unsigned long long)EMU_LOAD_ADDRESS);
}

int main(int argc, char **argv) {
    char error[512];
    Emulator emu;

    if (argc != 3) {
        print_usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "error: unknown command: %s\n", argv[1]);
        print_usage(stderr);
        return 2;
    }

    if (!emulator_init(&emu, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }

    if (!load_raw_binary(&emu.memory, argv[2], EMU_LOAD_ADDRESS, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        emulator_free(&emu);
        return 1;
    }

    EmuStatus status = emulator_run(&emu, error, sizeof(error));
    if (status == EMU_HALTED) {
        printf("halted\n");
        cpu_dump(&emu.cpu, stdout);
        emulator_free(&emu);
        return 0;
    }

    fprintf(stderr, "error: %s\n", error);
    cpu_dump(&emu.cpu, stderr);
    emulator_free(&emu);
    return 1;
}

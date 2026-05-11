#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TMP "tests/v1_1/tmp/"

static int tests_run = 0;
static int tests_failed = 0;

static void fail_at(const char *file, int line, const char *expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    tests_failed++;
}

#define EXPECT_TRUE(expr) do { tests_run++; if (!(expr)) fail_at(__FILE__, __LINE__, #expr); } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_U64_EQ(actual, expected) do { \
    tests_run++; \
    uint64_t actual_value__ = (uint64_t)(actual); \
    uint64_t expected_value__ = (uint64_t)(expected); \
    if (actual_value__ != expected_value__) { \
        fprintf(stderr, "FAIL %s:%d: %s == 0x%016" PRIx64 ", expected 0x%016" PRIx64 "\n", \
                __FILE__, __LINE__, #actual, actual_value__, expected_value__); \
        tests_failed++; \
    } \
} while (0)
#define EXPECT_SIZE_EQ(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static void init_emulator_or_die(Emulator *emu) {
    char error[512];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        tests_failed++;
    }
}

static bool load_fixture(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size) {
    memset(error, 0, error_size);
    memset(program, 0xa5, sizeof(*program));
    return emulator_load_program(emu, path, program, error, error_size);
}

static void test_valid_macho_metadata_and_mapping(void) {
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];
    init_emulator_or_die(&emu);

    EXPECT_TRUE(load_fixture(&emu, TMP "stdout_data.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_MACHO64);
    EXPECT_U64_EQ(program.entry, 0x1000u);
    EXPECT_U64_EQ(emu.cpu.pc, 0x1000u);
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    EXPECT_TRUE(strcmp(program.segments[0].name, "__TEXT") == 0);
    EXPECT_TRUE(strcmp(program.segments[1].name, "__DATA") == 0);
    EXPECT_U64_EQ(program.segments[0].vaddr, 0x1000u);
    EXPECT_U64_EQ(program.segments[1].vaddr, 0x3000u);
    EXPECT_TRUE(memcmp(&emu.memory.bytes[0x3000], "hello, v1.1!\n", 13u) == 0);
    EXPECT_U64_EQ(program.macho_symbol_count, 1u);
    EXPECT_U64_EQ(program.macho_recorded_symbol_count, 1u);
    EXPECT_TRUE(strcmp(program.macho_symbols[0].name, "_fixture") == 0);
    EXPECT_U64_EQ(program.macho_symbols[0].address, 0x1000u);

    emulator_free(&emu);
}

static void test_adjacent_and_zero_sized_segments(void) {
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];
    init_emulator_or_die(&emu);

    EXPECT_TRUE(load_fixture(&emu, TMP "adjacent.macho", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    EXPECT_U64_EQ(program.segments[0].vaddr + program.segments[0].mem_size, program.segments[1].vaddr);
    EXPECT_U64_EQ(program.segments[1].file_size, 1u);
    EXPECT_U64_EQ(emu.memory.bytes[program.segments[1].vaddr], 'X');
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "zero_sized_segment.macho", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 1u);
    EXPECT_TRUE(strcmp(program.segments[0].name, "__TEXT") == 0);
    EXPECT_U64_EQ(program.entry, 0x1000u);
    emulator_free(&emu);
}

static void test_zero_fill_and_boundary_segment(void) {
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];
    init_emulator_or_die(&emu);

    EXPECT_TRUE(load_fixture(&emu, TMP "zero_fill_data.macho", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    EXPECT_U64_EQ(program.segments[1].file_size, 4u);
    EXPECT_U64_EQ(program.segments[1].mem_size, 12u);
    EXPECT_TRUE(memcmp(&emu.memory.bytes[0x3000], "DATA", 4u) == 0);
    for (size_t i = 4; i < 12; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[0x3000 + i], 0u);
    }
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "mem_range_boundary.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.entry, 0xffff4u);
    emulator_free(&emu);
}

static void test_entry_and_segment_validation_errors(void) {
    struct Case {
        const char *path;
        const char *needle;
    } cases[] = {
        {TMP "no_main.macho", "LC_MAIN is required"},
        {TMP "duplicate_main.macho", "duplicate LC_MAIN"},
        {TMP "misaligned_entry.macho", "4-byte aligned"},
        {TMP "entry_outside.macho", "entryoff is not inside"},
        {TMP "filesize_gt_vmsize.macho", "filesize exceeds vmsize"},
        {TMP "file_range_eof.macho", "file range outside file"},
        {TMP "mem_range_oob.macho", "memory range outside"},
        {TMP "overlap_1byte.macho", "overlapping"},
        {TMP "section_overflow.macho", "section table is truncated"},
        {TMP "bad_symbol_string_index.macho", "symbol string index outside string table"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Emulator emu;
        EmuLoadedProgram program;
        char error[512];
        init_emulator_or_die(&emu);
        emu.cpu.pc = 0xdeadbeefu;
        emu.memory.bytes[0x1000] = 0xccu;
        EXPECT_FALSE(load_fixture(&emu, cases[i].path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, cases[i].needle);
        EXPECT_U64_EQ(emu.cpu.pc, 0xdeadbeefu);
        EXPECT_U64_EQ(emu.memory.bytes[0x1000], 0xccu);
        emulator_free(&emu);
    }
}

static void test_header_and_load_command_errors(void) {
    struct Case {
        const char *path;
        const char *needle;
    } cases[] = {
        {TMP "wrong_cpu.macho", "unsupported CPU type"},
        {TMP "wrong_filetype.macho", "unsupported file type"},
        {TMP "big_endian.macho", "big-endian Mach-O"},
        {TMP "macho32.macho", "32-bit Mach-O"},
        {TMP "fat.macho", "fat/universal"},
        {TMP "ncmds_size_mismatch.macho", "outside the command table"},
        {TMP "cmd_too_small.macho", "invalid load command"},
        {TMP "cmd_unaligned.macho", "alignment"},
        {TMP "dylinker.macho", "dyld is unsupported"},
        {TMP "dylib.macho", "shared libraries are unsupported"},
        {TMP "dyld_info.macho", "rebasing and binding metadata is unsupported"},
        {TMP "dyld_info_only.macho", "rebasing and binding metadata is unsupported"},
        {TMP "relocations.macho", "relocations are unsupported"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Emulator emu;
        EmuLoadedProgram program;
        char error[512];
        init_emulator_or_die(&emu);
        EXPECT_FALSE(load_fixture(&emu, cases[i].path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, cases[i].needle);
        emulator_free(&emu);
    }
}

static void test_truncated_header_and_raw_detection(void) {
    for (int i = 4; i < 32; i++) {
        char path[128];
        snprintf(path, sizeof(path), TMP "truncated_header_%02d.macho", i);
        Emulator emu;
        EmuLoadedProgram program;
        char error[512];
        init_emulator_or_die(&emu);
        EXPECT_FALSE(load_fixture(&emu, path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, "Mach-O header is truncated");
        emulator_free(&emu);
    }

    for (int i = 1; i <= 3; i++) {
        char path[128];
        snprintf(path, sizeof(path), TMP "partial%d.bin", i);
        Emulator emu;
        EmuLoadedProgram program;
        char error[512];
        init_emulator_or_die(&emu);
        EXPECT_TRUE(load_fixture(&emu, path, &program, error, sizeof(error)));
        EXPECT_U64_EQ(program.format, EMU_PROGRAM_RAW);
        EXPECT_U64_EQ(program.entry, EMU_LOAD_ADDRESS);
        emulator_free(&emu);
    }
}

static void test_execution_paths(void) {
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "valid_exit42.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 42u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "unknown_harmless.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_MACHO64);
    emulator_free(&emu);
}

int main(void) {
    test_valid_macho_metadata_and_mapping();
    test_adjacent_and_zero_sized_segments();
    test_zero_fill_and_boundary_segment();
    test_entry_and_segment_validation_errors();
    test_header_and_load_command_errors();
    test_truncated_header_and_raw_detection();
    test_execution_paths();

    if (tests_failed != 0) {
        fprintf(stderr, "v1.1 unit tests failed: %d/%d assertions failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v1.1 unit tests passed (%d assertions)\n", tests_run);
    return 0;
}

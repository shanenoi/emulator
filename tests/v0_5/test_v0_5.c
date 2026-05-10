#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_TMP_DIR "tests/v0_5/tmp"

#define OP_NOP 0xd503201fu
#define OP_HLT 0xd4400000u
#define OP_UNSUPPORTED 0xffffffffu
#define OP_MOVZ_X0_2 0xd2800040u
#define OP_MOVZ_X1_3 0xd2800061u
#define OP_ADD_X2_X0_3 0x91000c02u
#define OP_B_SELF 0x14000000u

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
#define EXPECT_STATUS(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static void reset_error(char *error, size_t error_size) {
    memset(error, 0, error_size);
}

static void ensure_tmp_dir(void) {
    if (mkdir(TEST_TMP_DIR, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "failed to create %s: %s\n", TEST_TMP_DIR, strerror(errno));
        exit(1);
    }
}

static void write_u32_le(FILE *file, uint32_t value) {
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8u) & 0xffu), file);
    fputc((int)((value >> 16u) & 0xffu), file);
    fputc((int)((value >> 24u) & 0xffu), file);
}

static void write_program_file(const char *path, const uint32_t *opcodes, size_t opcode_count) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    for (size_t i = 0; i < opcode_count; i++) {
        write_u32_le(file, opcodes[i]);
    }
    fclose(file);
}

static const char *add_program_path(void) {
    static const char *path = TEST_TMP_DIR "/add.bin";
    const uint32_t program[] = {OP_MOVZ_X0_2, OP_MOVZ_X1_3, OP_ADD_X2_X0_3, OP_HLT};
    write_program_file(path, program, sizeof(program) / sizeof(program[0]));
    return path;
}

static const char *unsupported_program_path(void) {
    static const char *path = TEST_TMP_DIR "/unsupported.bin";
    const uint32_t program[] = {OP_UNSUPPORTED, OP_HLT};
    write_program_file(path, program, sizeof(program) / sizeof(program[0]));
    return path;
}

static const char *infinite_program_path(void) {
    static const char *path = TEST_TMP_DIR "/infinite.bin";
    const uint32_t program[] = {OP_B_SELF};
    write_program_file(path, program, sizeof(program) / sizeof(program[0]));
    return path;
}

static void init_debugger_or_die(Debugger *debugger, const char *path) {
    char error[256];
    if (!debugger_init(debugger, path, error, sizeof(error))) {
        fprintf(stderr, "debugger_init failed: %s\n", error);
        exit(1);
    }
}

static void test_debugger_init_and_reset(void) {
    /* TC-V05-CLI-002, TC-V05-RUN-002, TC-V05-RUN-003. */
    char error[256];
    Debugger debugger;
    const char *path = add_program_path();

    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_init(&debugger, TEST_TMP_DIR "/missing.bin", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "failed to open");

    init_debugger_or_die(&debugger, path);
    EXPECT_TRUE(debugger.loaded);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 0);
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u, error, sizeof(error)));
    debugger.emu.trace_enabled = true;
    debugger.emu.cpu.x[0] = 99;
    debugger.emu.cpu.pc = EMU_LOAD_ADDRESS + 4u;

    reset_error(error, sizeof(error));
    EXPECT_TRUE(debugger_reset(&debugger, error, sizeof(error)));
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(debugger.emu.cpu.x[0], 0);
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 0);
    EXPECT_TRUE(debugger.emu.trace_enabled);
    EXPECT_TRUE(debugger_has_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u));
    debugger_free(&debugger);
}

static void test_breakpoint_helpers(void) {
    /* TC-V05-BP-001 through TC-V05-BP-010. */
    char error[256];
    Debugger debugger;
    init_debugger_or_die(&debugger, add_program_path());

    reset_error(error, sizeof(error));
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u, error, sizeof(error)));
    EXPECT_TRUE(debugger_has_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u));
    EXPECT_U64_EQ(debugger.breakpoint_count, 1);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 2u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "4-byte aligned");

    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_add_breakpoint(&debugger, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "outside memory");

    reset_error(error, sizeof(error));
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, 0, error, sizeof(error)));
    EXPECT_TRUE(debugger_has_breakpoint(&debugger, 0));

    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "already exists");

    reset_error(error, sizeof(error));
    EXPECT_TRUE(debugger_delete_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u, error, sizeof(error)));
    EXPECT_FALSE(debugger_has_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u));

    reset_error(error, sizeof(error));
    EXPECT_TRUE(debugger_delete_breakpoint(&debugger, 1, error, sizeof(error)));
    EXPECT_U64_EQ(debugger.breakpoint_count, 0);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_delete_breakpoint(&debugger, EMU_LOAD_ADDRESS + 8u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "not found");

    for (size_t i = 0; i < EMU_MAX_BREAKPOINTS; i++) {
        reset_error(error, sizeof(error));
        EXPECT_TRUE(debugger_add_breakpoint(&debugger, (uint64_t)(i * 4u), error, sizeof(error)));
    }
    EXPECT_U64_EQ(debugger.breakpoint_count, EMU_MAX_BREAKPOINTS);
    reset_error(error, sizeof(error));
    EXPECT_FALSE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 0x100u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "maximum breakpoints");
    EXPECT_U64_EQ(debugger.breakpoint_count, EMU_MAX_BREAKPOINTS);

    debugger_free(&debugger);
}

static void test_step_and_continue(void) {
    /* TC-V05-STEP-001 through TC-V05-STEP-005 and TC-V05-CONT-001 through TC-V05-CONT-003. */
    char error[256];
    Debugger debugger;
    init_debugger_or_die(&debugger, add_program_path());

    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(debugger.emu.cpu.x[0], 2);
    EXPECT_U64_EQ(debugger.emu.cpu.x[1], 0);
    EXPECT_U64_EQ(debugger.emu.cpu.x[2], 0);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 1);

    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(debugger.emu.cpu.x[1], 3);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 2);

    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(debugger.emu.cpu.x[2], 5);
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 4);

    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "already halted");

    EXPECT_TRUE(debugger_reset(&debugger, error, sizeof(error)));
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS, error, sizeof(error)));
    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);

    EXPECT_TRUE(debugger_reset(&debugger, error, sizeof(error)));
    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(debugger.stopped_at_breakpoint);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_STR_CONTAINS(error, "breakpoint hit");

    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(debugger.emu.cpu.x[2], 5);

    debugger_free(&debugger);
}

static void test_continue_errors_and_limits(void) {
    /* TC-V05-CONT-005, TC-V05-ERR-001, TC-V05-ERR-002, TC-V05-ACC-005. */
    char error[256];
    Debugger debugger;

    init_debugger_or_die(&debugger, unsupported_program_path());
    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS);
    debugger_free(&debugger);

    init_debugger_or_die(&debugger, unsupported_program_path());
    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
    EXPECT_U64_EQ(debugger.emu.cpu.pc, EMU_LOAD_ADDRESS);
    debugger_free(&debugger);

    init_debugger_or_die(&debugger, infinite_program_path());
    debugger.emu.instruction_limit = 5;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(debugger.emu.cpu.instructions_executed, 5);
    debugger_free(&debugger);
}

int main(void) {
    ensure_tmp_dir();
    test_debugger_init_and_reset();
    test_breakpoint_helpers();
    test_step_and_continue();
    test_continue_errors_and_limits();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.5 unit/integration tests failed: %d failure(s) across %d assertion(s)\n", tests_failed,
                tests_run);
        return 1;
    }

    printf("v0.5 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

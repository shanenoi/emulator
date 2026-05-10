#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_NOP 0xd503201fu
#define OP_HLT 0xd4400000u
#define OP_UNSUPPORTED 0xffffffffu
#define OP_MOVZ_X0_2 0xd2800040u
#define OP_MOVZ_W0_1 0x52800020u
#define OP_ADD_X2_X0_3 0x91000c02u
#define OP_SUB_X0_X0_1 0xd1000400u
#define OP_CMP_X0_5 0xf100141fu
#define OP_CMP_X0_X1 0xeb01001fu
#define OP_RET_X30 0xd65f03c0u
#define OP_STR_X0_SP_PRE_NEG8 0xf81f8fe0u
#define OP_LDR_X1_SP_POST_8 0xf84087e1u
#define OP_STP_X29_X30_SP_PRE_NEG16 0xa9bf7bfdu
#define OP_LDP_X29_X30_SP_POST_16 0xa8c17bfdu

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
#define EXPECT_STR_NOT_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) != NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string not to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_bl(int64_t offset) {
    return 0x94000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_bcond(int64_t offset, EmuCondition condition) {
    return 0x54000000u | ((uint32_t)((offset / 4) & 0x7ffffu) << 5u) | (uint32_t)condition;
}

static uint32_t encode_cbnz(bool is_64_bit, uint8_t rn, int64_t offset) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x35000000u | ((uint32_t)((offset / 4) & 0x7ffffu) << 5u) |
           (uint32_t)(rn & 0x1fu);
}

static void load_program(Emulator *emu, const uint32_t *opcodes, size_t opcode_count) {
    char error[256];
    for (size_t i = 0; i < opcode_count; i++) {
        if (!memory_write32(&emu->memory, EMU_LOAD_ADDRESS + (uint64_t)(i * 4u), opcodes[i], error, sizeof(error))) {
            fprintf(stderr, "memory_write32 failed: %s\n", error);
            exit(1);
        }
    }
}

static void init_emulator_or_die(Emulator *emu) {
    char error[256];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        exit(1);
    }
}

static void format_or_expect(bool expected, uint32_t opcode, uint64_t address, char *out, size_t out_size) {
    memset(out, 0, out_size > 0 ? out_size : 1);
    bool result = cpu_format_instruction(opcode, address, out, out_size);
    if (expected) {
        EXPECT_TRUE(result);
    } else {
        EXPECT_FALSE(result);
    }
}

static void test_format_basic_instructions(void) {
    /* TC-V06-FMT-001 through TC-V06-FMT-004. */
    char out[256];

    format_or_expect(true, OP_NOP, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "0x0000000000001000");
    EXPECT_STR_CONTAINS(out, "0xd503201f");
    EXPECT_STR_CONTAINS(out, "nop");

    format_or_expect(true, OP_HLT, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "0xd4400000");
    EXPECT_STR_CONTAINS(out, "hlt");

    format_or_expect(true, OP_MOVZ_X0_2, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "movz x0");
    EXPECT_STR_CONTAINS(out, "#0x2");

    format_or_expect(true, OP_MOVZ_W0_1, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "movz w0");
    EXPECT_STR_CONTAINS(out, "#0x1");

    format_or_expect(true, OP_ADD_X2_X0_3, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "add x2, x0, #0x3");

    format_or_expect(true, OP_SUB_X0_X0_1, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "sub x0, x0, #0x1");
}

static void test_format_control_flow_and_cmp(void) {
    /* TC-V06-FMT-005 and TC-V06-FMT-006. */
    char out[256];

    format_or_expect(true, encode_b(8), EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "b 0x0000000000001008");

    format_or_expect(true, encode_bl(12), EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "bl 0x000000000000100c");

    format_or_expect(true, encode_bcond(8, EMU_COND_EQ), EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "b.eq 0x0000000000001008");

    format_or_expect(true, encode_cbnz(true, 0, -4), EMU_LOAD_ADDRESS + 8u, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "cbnz x0, 0x0000000000001004");

    format_or_expect(true, OP_CMP_X0_5, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "cmp x0, #0x5");

    format_or_expect(true, OP_CMP_X0_X1, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "cmp x0, x1");

    format_or_expect(true, OP_RET_X30, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "ret");
}

static void test_format_memory_instructions(void) {
    /* TC-V06-FMT-007. */
    char out[256];

    format_or_expect(true, OP_STR_X0_SP_PRE_NEG8, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "str x0, [sp, #-8]!");

    format_or_expect(true, OP_LDR_X1_SP_POST_8, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "ldr x1, [sp], #8");

    format_or_expect(true, OP_STP_X29_X30_SP_PRE_NEG16, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "stp x29, x30, [sp, #-16]!");

    format_or_expect(true, OP_LDP_X29_X30_SP_POST_16, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "ldp x29, x30, [sp], #16");
}

static void test_format_edge_cases(void) {
    /* TC-V06-FMT-008 through TC-V06-FMT-010. */
    char out[256];
    char tiny[8];

    format_or_expect(false, OP_UNSUPPORTED, EMU_LOAD_ADDRESS, out, sizeof(out));
    EXPECT_STR_CONTAINS(out, "0xffffffff");
    EXPECT_STR_CONTAINS(out, "<unsupported>");

    EXPECT_FALSE(cpu_format_instruction(OP_NOP, EMU_LOAD_ADDRESS, tiny, sizeof(tiny)));
    EXPECT_FALSE(cpu_format_instruction(OP_NOP, EMU_LOAD_ADDRESS, NULL, sizeof(out)));
    EXPECT_FALSE(cpu_format_instruction(OP_NOP, EMU_LOAD_ADDRESS, out, 0));
}

static void test_error_context(void) {
    /* TC-V06-ERR-001 through TC-V06-ERR-005, except loader errors covered by CLI. */
    char error[512];
    Emulator emu;

    const uint32_t unsupported[] = {OP_UNSUPPORTED};
    init_emulator_or_die(&emu);
    load_program(&emu, unsupported, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "decode error at pc=0x0000000000001000");
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
    EXPECT_STR_CONTAINS(error, "opcode=0xffffffff");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.pc = EMU_LOAD_ADDRESS + 2u;
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "misaligned pc");
    EXPECT_STR_CONTAINS(error, "0x0000000000001002");
    emulator_free(&emu);

    Memory memory;
    uint64_t target = 0;
    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    memset(error, 0, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(EMU_LOAD_ADDRESS, -0x2000, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "branch target before memory");
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001000");
    memory_free(&memory);

    const uint32_t bad_ret[] = {OP_RET_X30};
    init_emulator_or_die(&emu);
    load_program(&emu, bad_ret, 1);
    emu.cpu.x[30] = 1;
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "x30=0x0000000000000001");
    EXPECT_STR_CONTAINS(error, "return target");
    emulator_free(&emu);

    const uint32_t invalid_memory[] = {0xf94003e0u}; /* ldr x0, [sp] with initial sp at memory end. */
    init_emulator_or_die(&emu);
    load_program(&emu, invalid_memory, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "execution error at pc=0x0000000000001000");
    EXPECT_STR_CONTAINS(error, "opcode=0xf94003e0");
    EXPECT_STR_CONTAINS(error, "memory access out of bounds");
    emulator_free(&emu);
}

int main(void) {
    test_format_basic_instructions();
    test_format_control_flow_and_cmp();
    test_format_memory_instructions();
    test_format_edge_cases();
    test_error_context();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.6 unit/integration tests failed: %d failure(s) across %d assertion(s)\n", tests_failed,
                tests_run);
        return 1;
    }

    printf("v0.6 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

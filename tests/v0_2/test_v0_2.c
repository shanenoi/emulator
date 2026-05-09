#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_NOP 0xd503201fu
#define OP_HLT 0xd4400000u
#define OP_UNSUPPORTED 0xffffffffu

#define OP_MOVZ_X0_0 0xd2800000u
#define OP_MOVZ_X0_1 0xd2800020u
#define OP_MOVZ_X0_2 0xd2800040u
#define OP_MOVZ_X0_3 0xd2800060u
#define OP_MOVZ_X0_4 0xd2800080u
#define OP_MOVZ_X0_5 0xd28000a0u
#define OP_MOVZ_X0_6 0xd28000c0u
#define OP_MOVZ_X0_7 0xd28000e0u
#define OP_MOVZ_X0_9 0xd2800120u
#define OP_MOVZ_X1_0 0xd2800001u
#define OP_MOVZ_X1_3 0xd2800061u
#define OP_MOVZ_X1_5 0xd28000a1u
#define OP_MOVZ_X1_7 0xd28000e1u
#define OP_MOVZ_X1_99 0xd2800c61u
#define OP_MOVZ_X2_1 0xd2800022u

#define OP_ADD_X0_X0_1 0x91000400u
#define OP_ADD_X1_X1_1 0x91000421u
#define OP_SUB_X0_X0_1 0xd1000400u

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
#define EXPECT_I64_EQ(actual, expected) do { \
    tests_run++; \
    int64_t actual_value__ = (int64_t)(actual); \
    int64_t expected_value__ = (int64_t)(expected); \
    if (actual_value__ != expected_value__) { \
        fprintf(stderr, "FAIL %s:%d: %s == %" PRId64 ", expected %" PRId64 "\n", \
                __FILE__, __LINE__, #actual, actual_value__, expected_value__); \
        tests_failed++; \
    } \
} while (0)
#define EXPECT_KIND(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_COND(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STATUS(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_bcond(int64_t offset, EmuCondition condition) {
    return 0x54000000u | ((uint32_t)((offset / 4) & 0x7ffffu) << 5u) | (uint32_t)condition;
}

static uint32_t encode_cbz(bool is_64_bit, uint8_t rn, int64_t offset) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x34000000u | ((uint32_t)((offset / 4) & 0x7ffffu) << 5u) |
           (uint32_t)(rn & 0x1fu);
}

static uint32_t encode_cbnz(bool is_64_bit, uint8_t rn, int64_t offset) {
    return encode_cbz(is_64_bit, rn, offset) | 0x01000000u;
}

static uint32_t encode_cmp_imm(bool is_64_bit, uint8_t rn, uint16_t imm12, bool lsl12) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x71000000u | (lsl12 ? (1u << 22u) : 0u) |
           ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | 0x1fu;
}

static uint32_t encode_cmp_reg(bool is_64_bit, uint8_t rn, uint8_t rm) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x6b000000u | ((uint32_t)(rm & 0x1fu) << 16u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | 0x1fu;
}

static void reset_error(char *error, size_t error_size) {
    memset(error, 0, error_size);
}

static void init_emulator_or_die(Emulator *emu) {
    char error[256];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        exit(1);
    }
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

static EmuStatus run_program(const uint32_t *opcodes, size_t opcode_count, Emulator *emu, char *error, size_t error_size) {
    init_emulator_or_die(emu);
    load_program(emu, opcodes, opcode_count);
    return emulator_run(emu, error, error_size);
}

static void decode_or_die(uint32_t opcode, EmuDecodedInstruction *instruction) {
    char error[256];
    if (!cpu_decode(opcode, instruction, error, sizeof(error))) {
        fprintf(stderr, "cpu_decode failed for 0x%08x: %s\n", opcode, error);
        exit(1);
    }
}

static void test_decode_unconditional_branch(void) {
    /* TC-V02-DEC-B-001 through TC-V02-DEC-B-004. */
    EmuDecodedInstruction inst;

    decode_or_die(encode_b(8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B);
    EXPECT_I64_EQ(inst.offset, 8);

    decode_or_die(encode_b(-4), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B);
    EXPECT_I64_EQ(inst.offset, -4);

    decode_or_die(0x14000000u | 0x01ffffffu, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B);
    EXPECT_I64_EQ(inst.offset, 0x07fffffcll);

    decode_or_die(0x14000000u | 0x02000000u, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B);
    EXPECT_I64_EQ(inst.offset, -0x08000000ll);
}

static void test_decode_conditional_branch(void) {
    /* TC-V02-DEC-BCOND-001 through TC-V02-DEC-BCOND-004. */
    char error[256];
    EmuDecodedInstruction inst;

    decode_or_die(encode_bcond(8, EMU_COND_EQ), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B_COND);
    EXPECT_COND(inst.condition, EMU_COND_EQ);
    EXPECT_I64_EQ(inst.offset, 8);

    decode_or_die(encode_bcond(8, EMU_COND_NE), &inst);
    EXPECT_COND(inst.condition, EMU_COND_NE);

    decode_or_die(encode_bcond(-4, EMU_COND_LT), &inst);
    EXPECT_COND(inst.condition, EMU_COND_LT);
    EXPECT_I64_EQ(inst.offset, -4);
    decode_or_die(encode_bcond(4, EMU_COND_LE), &inst);
    EXPECT_COND(inst.condition, EMU_COND_LE);
    decode_or_die(encode_bcond(4, EMU_COND_GT), &inst);
    EXPECT_COND(inst.condition, EMU_COND_GT);
    decode_or_die(encode_bcond(4, EMU_COND_GE), &inst);
    EXPECT_COND(inst.condition, EMU_COND_GE);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_decode(encode_bcond(4, (EmuCondition)15), &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported B.cond condition");
}

static void test_decode_cbz_cbnz(void) {
    /* TC-V02-DEC-CBZ-001 through TC-V02-DEC-CBZ-003 and TC-V02-DEC-CBNZ-001 through TC-V02-DEC-CBNZ-002. */
    EmuDecodedInstruction inst;

    decode_or_die(encode_cbz(true, 3, 8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CBZ);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 3);
    EXPECT_I64_EQ(inst.offset, 8);

    decode_or_die(encode_cbz(false, 4, 12), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CBZ);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 4);
    EXPECT_I64_EQ(inst.offset, 12);

    decode_or_die(encode_cbnz(true, 5, -4), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CBNZ);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 5);
    EXPECT_I64_EQ(inst.offset, -4);

    decode_or_die(encode_cbnz(false, 6, -8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CBNZ);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 6);
    EXPECT_I64_EQ(inst.offset, -8);
}

static void test_decode_cmp(void) {
    /* TC-V02-DEC-CMP-001 through TC-V02-DEC-CMP-005. */
    EmuDecodedInstruction inst;

    decode_or_die(encode_cmp_imm(true, 0, 5, false), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CMP_IMM);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 0);
    EXPECT_U64_EQ(inst.imm, 5);

    decode_or_die(encode_cmp_imm(false, 1, 7, false), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CMP_IMM);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 1);
    EXPECT_U64_EQ(inst.imm, 7);

    decode_or_die(encode_cmp_reg(true, 2, 3), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CMP_REG);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 2);
    EXPECT_U64_EQ(inst.rm, 3);

    decode_or_die(encode_cmp_reg(false, 4, 5), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CMP_REG);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rn, 4);
    EXPECT_U64_EQ(inst.rm, 5);

    decode_or_die(encode_cmp_imm(true, 0, 1, true), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_CMP_IMM);
    EXPECT_U64_EQ(inst.imm, 4096);
}

static void test_condition_codes(void) {
    /* TC-V02-COND-001 through TC-V02-COND-006, plus implemented optional conditions. */
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = true}, EMU_COND_EQ));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = false}, EMU_COND_EQ));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = false}, EMU_COND_NE));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = true}, EMU_COND_NE));

    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.n = false, .v = false}, EMU_COND_LT));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = false, .v = true}, EMU_COND_LT));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = true, .v = false}, EMU_COND_LT));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.n = true, .v = true}, EMU_COND_LT));

    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = false, .v = false}, EMU_COND_GE));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.n = false, .v = true}, EMU_COND_GE));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.n = true, .v = false}, EMU_COND_GE));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = true, .v = true}, EMU_COND_GE));

    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = false, .n = false, .v = false}, EMU_COND_GT));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = true, .n = false, .v = false}, EMU_COND_GT));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = false, .n = true, .v = false}, EMU_COND_GT));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = false, .n = true, .v = true}, EMU_COND_GT));

    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = true, .n = false, .v = false}, EMU_COND_LE));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = false, .n = false, .v = false}, EMU_COND_LE));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.z = false, .n = true, .v = false}, EMU_COND_LE));
    EXPECT_FALSE(cpu_condition_passed((EmuFlags){.z = false, .n = true, .v = true}, EMU_COND_LE));

    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.c = true}, EMU_COND_CS));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.c = false}, EMU_COND_CC));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = true}, EMU_COND_MI));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.n = false}, EMU_COND_PL));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.v = true}, EMU_COND_VS));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.v = false}, EMU_COND_VC));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.c = true, .z = false}, EMU_COND_HI));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){.c = false, .z = false}, EMU_COND_LS));
    EXPECT_TRUE(cpu_condition_passed((EmuFlags){0}, EMU_COND_AL));
}

static void test_cmp_flags(void) {
    /* TC-V02-FLAG-001 through TC-V02-FLAG-008. */
    char error[256];
    Emulator emu;

    const uint32_t equal[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 5, false), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(equal, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_FALSE(emu.cpu.flags.n);
    EXPECT_TRUE(emu.cpu.flags.z);
    EXPECT_TRUE(emu.cpu.flags.c);
    EXPECT_FALSE(emu.cpu.flags.v);
    EXPECT_U64_EQ(emu.cpu.x[0], 5);
    emulator_free(&emu);

    const uint32_t greater[] = {OP_MOVZ_X0_6, encode_cmp_imm(true, 0, 5, false), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(greater, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_FALSE(emu.cpu.flags.z);
    EXPECT_FALSE(emu.cpu.flags.n);
    EXPECT_TRUE(cpu_condition_passed(emu.cpu.flags, EMU_COND_GT));
    emulator_free(&emu);

    const uint32_t less[] = {OP_MOVZ_X0_4, encode_cmp_imm(true, 0, 5, false), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(less, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_FALSE(emu.cpu.flags.z);
    EXPECT_TRUE(emu.cpu.flags.n);
    EXPECT_FALSE(emu.cpu.flags.v);
    EXPECT_FALSE(emu.cpu.flags.c);
    EXPECT_TRUE(cpu_condition_passed(emu.cpu.flags, EMU_COND_LT));
    emulator_free(&emu);

    const uint32_t no_borrow[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 4, false), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(no_borrow, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.cpu.flags.c);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x8000000000000000ull;
    const uint32_t overflow[] = {encode_cmp_imm(true, 0, 1, false), OP_HLT};
    load_program(&emu, overflow, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.cpu.flags.v);
    EXPECT_FALSE(emu.cpu.flags.z);
    EXPECT_TRUE(emu.cpu.flags.c);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x0000000100000000ull;
    const uint32_t cmp_w0_zero[] = {encode_cmp_imm(false, 0, 0, false), OP_HLT};
    load_program(&emu, cmp_w0_zero, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.cpu.flags.z);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x0000000100000000ull);
    emulator_free(&emu);

    const uint32_t cmp_does_not_modify[] = {OP_MOVZ_X0_9, encode_cmp_imm(true, 0, 3, false), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cmp_does_not_modify, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 9);
    emulator_free(&emu);
}

static void test_unconditional_branch_execution(void) {
    /* TC-V02-EXEC-B-001 through TC-V02-EXEC-B-005. */
    char error[256];
    Emulator emu;

    const uint32_t skip[] = {OP_MOVZ_X0_1, encode_b(8), OP_MOVZ_X0_5, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(skip, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 3);
    emulator_free(&emu);

    const uint32_t self_loop[] = {encode_b(0)};
    init_emulator_or_die(&emu);
    emu.instruction_limit = 7;
    load_program(&emu, self_loop, 1);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 7);
    emulator_free(&emu);

    const uint32_t branch_to_start[] = {OP_NOP, encode_b(-4)};
    init_emulator_or_die(&emu);
    emu.instruction_limit = 5;
    load_program(&emu, branch_to_start, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4);
    emulator_free(&emu);

    const uint32_t branch_to_final[] = {encode_b(8), OP_NOP, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(branch_to_final, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8);
    emulator_free(&emu);

    const uint32_t branch_outside[] = {encode_b((int64_t)(EMU_MEMORY_SIZE - EMU_LOAD_ADDRESS))};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(branch_outside, 1, &emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "branch target outside memory");
    emulator_free(&emu);
}

static void test_cbz_cbnz_execution(void) {
    /* TC-V02-EXEC-CBZ-001 through TC-V02-EXEC-CBZ-003 and TC-V02-EXEC-CBNZ-001 through TC-V02-EXEC-CBNZ-004. */
    char error[256];
    Emulator emu;

    const uint32_t cbz_taken[] = {OP_MOVZ_X0_0, encode_cbz(true, 0, 8), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cbz_taken, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t cbz_not_taken[] = {OP_MOVZ_X0_1, encode_cbz(true, 0, 8), OP_MOVZ_X1_7, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cbz_not_taken, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    emulator_free(&emu);

    const uint32_t cbnz_taken[] = {OP_MOVZ_X0_1, encode_cbnz(true, 0, 8), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cbnz_taken, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t cbnz_not_taken[] = {OP_MOVZ_X0_0, encode_cbnz(true, 0, 8), OP_MOVZ_X1_7, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cbnz_not_taken, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    emulator_free(&emu);

    const uint32_t countdown[] = {OP_MOVZ_X0_5, OP_MOVZ_X1_0, OP_ADD_X1_X1_1, OP_SUB_X0_X0_1, encode_cbnz(true, 0, -8), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(countdown, 6, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    EXPECT_U64_EQ(emu.cpu.x[1], 5);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 18);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x0000000100000000ull;
    const uint32_t cbz_w_taken[] = {encode_cbz(false, 0, 8), OP_MOVZ_X1_99, OP_HLT};
    load_program(&emu, cbz_w_taken, 3);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x0000000100000000ull;
    const uint32_t cbnz_w_not_taken[] = {encode_cbnz(false, 0, 8), OP_MOVZ_X1_7, OP_HLT};
    load_program(&emu, cbnz_w_not_taken, 3);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    emulator_free(&emu);
}

static void test_cmp_bcond_execution(void) {
    /* TC-V02-EXEC-BCOND-001 through TC-V02-EXEC-BCOND-009. */
    char error[256];
    Emulator emu;

    const uint32_t beq_taken[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_EQ), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(beq_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    EXPECT_TRUE(emu.cpu.flags.z);
    emulator_free(&emu);

    const uint32_t beq_not_taken[] = {OP_MOVZ_X0_4, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_EQ), OP_MOVZ_X1_7, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(beq_not_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    EXPECT_FALSE(emu.cpu.flags.z);
    emulator_free(&emu);

    const uint32_t bne_taken[] = {OP_MOVZ_X0_4, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_NE), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(bne_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    EXPECT_FALSE(emu.cpu.flags.z);
    emulator_free(&emu);

    const uint32_t bne_not_taken[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_NE), OP_MOVZ_X1_7, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(bne_not_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    EXPECT_TRUE(emu.cpu.flags.z);
    emulator_free(&emu);

    const uint32_t blt_taken[] = {OP_MOVZ_X0_4, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_LT), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(blt_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t bge_taken[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_GE), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(bge_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t bgt_taken[] = {OP_MOVZ_X0_6, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_GT), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(bgt_taken, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t ble_equal[] = {OP_MOVZ_X0_5, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_LE), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(ble_equal, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);

    const uint32_t ble_less[] = {OP_MOVZ_X0_4, encode_cmp_imm(true, 0, 5, false), encode_bcond(8, EMU_COND_LE), OP_MOVZ_X1_99, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(ble_less, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    emulator_free(&emu);
}

static void test_pc_and_instruction_count(void) {
    /* TC-V02-PC-001 through TC-V02-PC-005. */
    char error[256];
    Emulator emu;

    init_emulator_or_die(&emu);
    const uint32_t taken[] = {encode_b(12), OP_NOP, OP_NOP, OP_HLT};
    load_program(&emu, taken, 4);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 12);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    const uint32_t not_taken[] = {encode_bcond(8, EMU_COND_EQ), OP_HLT};
    load_program(&emu, not_taken, 2);
    emu.cpu.flags.z = false;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1);
    emulator_free(&emu);

    const uint32_t loop[] = {OP_MOVZ_X0_3, OP_SUB_X0_X0_1, encode_cbnz(true, 0, -4), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(loop, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 8);
    emulator_free(&emu);

    const uint32_t self_loop[] = {encode_b(0)};
    init_emulator_or_die(&emu);
    emu.instruction_limit = 3;
    load_program(&emu, self_loop, 1);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 3);
    emulator_free(&emu);
}

static void test_branch_errors(void) {
    /* TC-V02-ERR-001 through TC-V02-ERR-007. */
    char error[256];
    Memory memory;
    uint64_t target = 0;
    Emulator emu;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(EMU_LOAD_ADDRESS, 2, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "misaligned branch target");

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(0, -4, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "branch target before memory");

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(UINT64_MAX - 1ull, 4, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "branch target overflow");
    memory_free(&memory);

    EmuDecodedInstruction inst;
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_decode(encode_bcond(4, (EmuCondition)15), &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported B.cond condition");

    const uint32_t self_loop[] = {encode_b(0)};
    init_emulator_or_die(&emu);
    emu.instruction_limit = 2;
    load_program(&emu, self_loop, 1);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 2);
    emulator_free(&emu);

    const uint32_t halt_program[] = {OP_HLT};
    init_emulator_or_die(&emu);
    emu.instruction_limit = 0;
    load_program(&emu, halt_program, 1);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 0);
    emulator_free(&emu);

    const uint32_t branch_to_bad_opcode[] = {encode_b(8), OP_NOP, OP_UNSUPPORTED};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(branch_to_bad_opcode, 3, &emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8);
    emulator_free(&emu);
}

static void test_acceptance_programs(void) {
    /* TC-V02-ACC-001 through TC-V02-ACC-004 except trace CLI, which is covered by the shell test. */
    char error[256];
    Emulator emu;

    const uint32_t countdown[] = {OP_MOVZ_X0_5, OP_MOVZ_X1_0, OP_ADD_X1_X1_1, OP_SUB_X0_X0_1, encode_cbnz(true, 0, -8), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(countdown, 6, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    EXPECT_U64_EQ(emu.cpu.x[1], 5);
    emulator_free(&emu);

    const uint32_t cmp_demo[] = {OP_MOVZ_X0_7, encode_cmp_imm(true, 0, 7, false), encode_bcond(8, EMU_COND_EQ),
                                 OP_MOVZ_X1_99, OP_MOVZ_X2_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cmp_demo, 6, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0);
    EXPECT_U64_EQ(emu.cpu.x[2], 1);
    EXPECT_TRUE(emu.cpu.flags.z);
    emulator_free(&emu);

    const uint32_t cmp_bcond_loop[] = {OP_MOVZ_X0_0, OP_MOVZ_X1_5, OP_ADD_X0_X0_1, encode_cmp_reg(true, 0, 1),
                                       encode_bcond(-8, EMU_COND_LT), OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS(run_program(cmp_bcond_loop, 6, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 5);
    EXPECT_U64_EQ(emu.cpu.x[1], 5);
    emulator_free(&emu);
}

int main(void) {
    test_decode_unconditional_branch();
    test_decode_conditional_branch();
    test_decode_cbz_cbnz();
    test_decode_cmp();
    test_condition_codes();
    test_cmp_flags();
    test_unconditional_branch_execution();
    test_cbz_cbnz_execution();
    test_cmp_bcond_execution();
    test_pc_and_instruction_count();
    test_branch_errors();
    test_acceptance_programs();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.2 tests failed: %d failure(s) across %d assertion(s)\n", tests_failed, tests_run);
        return 1;
    }

    printf("v0.2 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

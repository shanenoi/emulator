#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_HLT 0xd4400000u
#define OP_UNSUPPORTED 0xffffffffu

#define OP_MOVZ_X0_0 0xd2800000u
#define OP_MOVZ_X0_1 0xd2800020u
#define OP_MOVZ_X0_2 0xd2800040u
#define OP_MOVZ_X0_3 0xd2800060u
#define OP_MOVZ_X0_5 0xd28000a0u
#define OP_MOVZ_X1_3 0xd2800061u
#define OP_MOVZ_X29_0X1234 0xd282469du
#define OP_MOVZ_X29_0XABCD 0xd29579bdu
#define OP_ADD_X0_X0_1 0x91000400u
#define OP_ADD_X0_X0_3 0x91000c00u
#define OP_STP_X29_X30_SP_PRE_NEG16 0xa9bf7bfdu
#define OP_LDP_X29_X30_SP_POST_16 0xa8c17bfdu
#define OP_RET_X30 0xd65f03c0u
#define OP_RET_X0 0xd65f0000u
#define OP_RET_X31 0xd65f03e0u

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
#define EXPECT_STATUS(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_KIND(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_bl(int64_t offset) {
    return 0x94000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
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

static EmuStatus run_program(const uint32_t *opcodes, size_t opcode_count, Emulator *emu, char *error,
                             size_t error_size) {
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

static void test_decode_bl(void) {
    /* TC-V04-DEC-BL-001 through TC-V04-DEC-BL-005. */
    char error[256];
    EmuDecodedInstruction inst;

    decode_or_die(encode_bl(8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_BL);
    EXPECT_I64_EQ(inst.offset, 8);

    decode_or_die(encode_bl(-8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_BL);
    EXPECT_I64_EQ(inst.offset, -8);

    decode_or_die(0x94000000u | 0x01ffffffu, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_BL);
    EXPECT_I64_EQ(inst.offset, 0x07fffffcll);

    decode_or_die(0x94000000u | 0x02000000u, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_BL);
    EXPECT_I64_EQ(inst.offset, -0x08000000ll);

    decode_or_die(encode_b(8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_B);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_decode(0x84000000u, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
}

static void test_decode_ret(void) {
    /* TC-V04-DEC-RET-001 through TC-V04-DEC-RET-004. */
    char error[256];
    EmuDecodedInstruction inst;

    decode_or_die(OP_RET_X30, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_RET);
    EXPECT_U64_EQ(inst.rn, 30);

    decode_or_die(OP_RET_X0, &inst);
    EXPECT_KIND(inst.kind, EMU_INST_RET);
    EXPECT_U64_EQ(inst.rn, 0);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_decode(0xd65f07c0u, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
}

static void test_bl_execution(void) {
    /* TC-V04-EXEC-BL-001 and TC-V04-EXEC-BL-002. */
    char error[256];
    Emulator emu;
    const uint32_t program[] = {
        encode_bl(8),
        OP_HLT,
        OP_MOVZ_X1_3,
        OP_HLT,
    };

    init_emulator_or_die(&emu);
    load_program(&emu, program, sizeof(program) / sizeof(program[0]));

    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.x[30], EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1);

    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 3);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 12u);
    emulator_free(&emu);
}

static void test_ret_execution(void) {
    /* TC-V04-EXEC-RET-001 through TC-V04-EXEC-RET-003 and TC-V04-EXEC-CALL-001. */
    char error[256];
    Emulator emu;
    const uint32_t program[] = {
        OP_MOVZ_X0_1,
        encode_bl(12),
        OP_MOVZ_X0_2,
        OP_HLT,
        OP_MOVZ_X1_3,
        OP_RET_X30,
    };

    EXPECT_STATUS(run_program(program, sizeof(program) / sizeof(program[0]), &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 2);
    EXPECT_U64_EQ(emu.cpu.x[1], 3);
    EXPECT_U64_EQ(emu.cpu.x[30], EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 12u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 6);
    emulator_free(&emu);

    const uint32_t simple_call[] = {
        OP_MOVZ_X0_2,
        encode_bl(8),
        OP_HLT,
        OP_ADD_X0_X0_3,
        OP_RET_X30,
    };
    EXPECT_STATUS(run_program(simple_call, sizeof(simple_call) / sizeof(simple_call[0]), &emu, error, sizeof(error)),
                  EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 5);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 5);
    emulator_free(&emu);
}

static void test_ret_custom_register(void) {
    /* TC-V04-EXEC-RET-002. */
    char error[256];
    Emulator emu;
    const uint32_t program[] = {
        OP_RET_X0,
        OP_UNSUPPORTED,
        OP_HLT,
    };

    init_emulator_or_die(&emu);
    load_program(&emu, program, sizeof(program) / sizeof(program[0]));
    emu.cpu.x[0] = EMU_LOAD_ADDRESS + 8u;
    emu.cpu.x[30] = 0x12345678ull;

    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(emu.cpu.x[30], 0x12345678ull);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1);
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_HALTED);
    emulator_free(&emu);
}

static void test_nested_and_sequential_calls(void) {
    /* TC-V04-EXEC-NEST-002 and TC-V04-EXEC-NEST-003. */
    char error[256];
    Emulator emu;
    const uint32_t nested[] = {
        OP_MOVZ_X0_0,
        encode_bl(8),
        OP_HLT,
        OP_STP_X29_X30_SP_PRE_NEG16,
        OP_ADD_X0_X0_1,
        encode_bl(16),
        OP_ADD_X0_X0_1,
        OP_LDP_X29_X30_SP_POST_16,
        OP_RET_X30,
        OP_ADD_X0_X0_1,
        OP_RET_X30,
    };

    EXPECT_STATUS(run_program(nested, sizeof(nested) / sizeof(nested[0]), &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 3);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 8u);
    emulator_free(&emu);

    const uint32_t sequential[] = {
        OP_MOVZ_X0_0,
        encode_bl(16),
        encode_bl(12),
        encode_bl(8),
        OP_HLT,
        OP_ADD_X0_X0_1,
        OP_RET_X30,
    };
    EXPECT_STATUS(run_program(sequential, sizeof(sequential) / sizeof(sequential[0]), &emu, error, sizeof(error)),
                  EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 3);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 16u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 11);
    emulator_free(&emu);
}

static void test_stack_frame_call(void) {
    /* TC-V04-EXEC-FRAME-001. */
    char error[256];
    Emulator emu;
    const uint32_t program[] = {
        OP_MOVZ_X29_0X1234,
        encode_bl(8),
        OP_HLT,
        OP_STP_X29_X30_SP_PRE_NEG16,
        OP_MOVZ_X29_0XABCD,
        OP_ADD_X0_X0_1,
        OP_LDP_X29_X30_SP_POST_16,
        OP_RET_X30,
    };

    EXPECT_STATUS(run_program(program, sizeof(program) / sizeof(program[0]), &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    EXPECT_U64_EQ(emu.cpu.x[29], 0x1234);
    EXPECT_U64_EQ(emu.cpu.x[30], EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);
}

static void test_failed_stack_save_is_atomic(void) {
    /* TC-V04-EXEC-FRAME-002. */
    char error[256];
    Emulator emu;
    const uint32_t program[] = {
        OP_STP_X29_X30_SP_PRE_NEG16,
        OP_HLT,
    };

    init_emulator_or_die(&emu);
    load_program(&emu, program, sizeof(program) / sizeof(program[0]));
    emu.cpu.sp = 8;
    emu.cpu.x[29] = 0xaaaa;
    emu.cpu.x[30] = 0xbbbb;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "pre-index write-back overflow");
    EXPECT_U64_EQ(emu.cpu.sp, 8);
    EXPECT_U64_EQ(emu.cpu.x[29], 0xaaaa);
    EXPECT_U64_EQ(emu.cpu.x[30], 0xbbbb);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    emulator_free(&emu);
}

static void test_bl_error_paths_are_atomic(void) {
    /* TC-V04-ERR-001 through TC-V04-ERR-003. */
    char error[256];
    Memory memory;
    uint64_t target = 0;
    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_FALSE(cpu_calculate_branch_target(EMU_LOAD_ADDRESS, -0x2000, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "before memory");
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(EMU_LOAD_ADDRESS, (int64_t)(EMU_MEMORY_SIZE - EMU_LOAD_ADDRESS), &memory,
                                             &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "outside memory");
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_branch_target(UINT64_MAX - 4u, 8, &memory, &target, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overflow");
    memory_free(&memory);

    Emulator emu;
    const uint32_t bad_bl[] = {encode_bl(-0x2000), OP_HLT};
    init_emulator_or_die(&emu);
    load_program(&emu, bad_bl, sizeof(bad_bl) / sizeof(bad_bl[0]));
    emu.cpu.x[30] = 0xdeadbeef;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "before memory");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.cpu.x[30], 0xdeadbeef);
    emulator_free(&emu);
}

static void test_ret_error_paths(void) {
    /* TC-V04-ERR-004 through TC-V04-ERR-006. */
    char error[256];
    Emulator emu;
    const uint32_t ret_program[] = {OP_RET_X30, OP_HLT};

    init_emulator_or_die(&emu);
    load_program(&emu, ret_program, sizeof(ret_program) / sizeof(ret_program[0]));
    emu.cpu.x[30] = EMU_LOAD_ADDRESS + 1u;
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "misaligned return target");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    load_program(&emu, ret_program, sizeof(ret_program) / sizeof(ret_program[0]));
    emu.cpu.x[30] = EMU_MEMORY_SIZE;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "return target outside memory");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    const uint32_t ret_xzr[] = {OP_RET_X31};
    load_program(&emu, ret_xzr, sizeof(ret_xzr) / sizeof(ret_xzr[0]));
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "invalid return target");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    load_program(&emu, ret_program, sizeof(ret_program) / sizeof(ret_program[0]));
    EXPECT_TRUE(memory_write32(&emu.memory, 0x2000, OP_UNSUPPORTED, error, sizeof(error)));
    emu.cpu.x[30] = 0x2000;
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, 0x2000);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
    emulator_free(&emu);
}

static void test_infinite_recursion_and_unsaved_nested_call(void) {
    /* TC-V04-ERR-007 and TC-V04-ERR-008. */
    char error[256];
    Emulator emu;
    const uint32_t recursive[] = {encode_bl(0)};

    init_emulator_or_die(&emu);
    load_program(&emu, recursive, sizeof(recursive) / sizeof(recursive[0]));
    emu.instruction_limit = 8;
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 8);
    emulator_free(&emu);

    const uint32_t unsaved[] = {
        encode_bl(8),
        OP_HLT,
        encode_bl(4),
        OP_RET_X30,
        OP_RET_X30,
    };
    init_emulator_or_die(&emu);
    load_program(&emu, unsaved, sizeof(unsaved) / sizeof(unsaved[0]));
    emu.instruction_limit = 16;
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    emulator_free(&emu);
}

int main(void) {
    test_decode_bl();
    test_decode_ret();
    test_bl_execution();
    test_ret_execution();
    test_ret_custom_register();
    test_nested_and_sequential_calls();
    test_stack_frame_call();
    test_failed_stack_save_is_atomic();
    test_bl_error_paths_are_atomic();
    test_ret_error_paths();
    test_infinite_recursion_and_unsaved_nested_call();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.4 unit/integration tests failed: %d failure(s) across %d assertion(s)\n", tests_failed,
                tests_run);
        return 1;
    }

    printf("v0.4 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

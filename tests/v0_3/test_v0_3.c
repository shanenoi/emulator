#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_HLT 0xd4400000u
#define OP_NOP 0xd503201fu
#define OP_UNSUPPORTED 0xffffffffu

#define OP_MOVZ_X0_1 0xd2800020u
#define OP_MOVZ_X0_7 0xd28000e0u
#define OP_MOVZ_X0_9 0xd2800120u
#define OP_MOVZ_X0_11 0xd2800160u
#define OP_MOVZ_X0_42 0xd2800540u
#define OP_MOVZ_X0_100 0xd2800c80u
#define OP_MOVZ_X1_2 0xd2800041u
#define OP_MOVZ_X1_22 0xd28002c1u
#define OP_MOVZ_X1_200 0xd2801901u
#define OP_MOVZ_X1_FFFF_LSL16 0xd2bfffe1u
#define OP_ADD_X1_X1_0X123 0x91048c21u

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
#define EXPECT_U8_EQ(actual, expected) EXPECT_U64_EQ((actual), (uint8_t)(expected))
#define EXPECT_KIND(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_MODE(actual, expected) EXPECT_U64_EQ((actual), (expected))
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

static uint32_t encode_load_store_unsigned(bool is_load, bool is_64_bit, uint8_t rt, uint8_t rn, uint64_t byte_offset) {
    uint8_t size = is_64_bit ? 3u : 2u;
    uint8_t access_size = (uint8_t)(1u << size);
    uint32_t imm12 = (uint32_t)(byte_offset / access_size) & 0xfffu;
    return ((uint32_t)size << 30u) | 0x39000000u | (is_load ? (1u << 22u) : 0u) |
           (imm12 << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_load_store_unscaled_or_wb(bool is_load, bool is_64_bit, uint8_t rt, uint8_t rn, int16_t offset,
                                                 EmuAddressMode mode) {
    uint8_t size = is_64_bit ? 3u : 2u;
    uint32_t mode_bits = 0;
    if (mode == EMU_ADDR_POST_INDEX) {
        mode_bits = 1u;
    } else if (mode == EMU_ADDR_PRE_INDEX) {
        mode_bits = 3u;
    } else {
        mode_bits = 0u;
    }
    return ((uint32_t)size << 30u) | 0x38000000u | (is_load ? (1u << 22u) : 0u) |
           (((uint32_t)offset & 0x1ffu) << 12u) | (mode_bits << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_pair(bool is_load, uint8_t rt, uint8_t rt2, uint8_t rn, int16_t byte_offset,
                            EmuAddressMode mode) {
    uint32_t mode_bits = 2u;
    if (mode == EMU_ADDR_POST_INDEX) {
        mode_bits = 1u;
    } else if (mode == EMU_ADDR_PRE_INDEX) {
        mode_bits = 3u;
    }
    int16_t scaled = (int16_t)(byte_offset / 8);
    return (2u << 30u) | 0x28000000u | (mode_bits << 23u) | (is_load ? (1u << 22u) : 0u) |
           (((uint32_t)scaled & 0x7fu) << 15u) | ((uint32_t)(rt2 & 0x1fu) << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rt & 0x1fu);
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

static void test_decode_single_register_load_store(void) {
    /* TC-V03-DEC-LS-001 through TC-V03-DEC-LS-006. */
    EmuDecodedInstruction inst;

    decode_or_die(encode_load_store_unsigned(true, true, 0, 1, 0), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDR);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 0);
    EXPECT_U64_EQ(inst.rn, 1);
    EXPECT_I64_EQ(inst.offset, 0);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_UNSIGNED_OFFSET);
    EXPECT_U64_EQ(inst.access_size, 8);

    decode_or_die(encode_load_store_unsigned(true, true, 2, 3, 16), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDR);
    EXPECT_U64_EQ(inst.rd, 2);
    EXPECT_U64_EQ(inst.rn, 3);
    EXPECT_I64_EQ(inst.offset, 16);

    decode_or_die(encode_load_store_unsigned(false, true, 4, 5, 0), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STR);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 4);
    EXPECT_U64_EQ(inst.rn, 5);
    EXPECT_I64_EQ(inst.offset, 0);

    decode_or_die(encode_load_store_unsigned(false, true, 6, 7, 24), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STR);
    EXPECT_U64_EQ(inst.rd, 6);
    EXPECT_U64_EQ(inst.rn, 7);
    EXPECT_I64_EQ(inst.offset, 24);

    decode_or_die(encode_load_store_unsigned(true, false, 8, 9, 4), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDR);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 8);
    EXPECT_U64_EQ(inst.rn, 9);
    EXPECT_I64_EQ(inst.offset, 4);
    EXPECT_U64_EQ(inst.access_size, 4);

    decode_or_die(encode_load_store_unsigned(false, false, 10, 11, 8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STR);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 10);
    EXPECT_U64_EQ(inst.rn, 11);
    EXPECT_I64_EQ(inst.offset, 8);
    EXPECT_U64_EQ(inst.access_size, 4);

    decode_or_die(encode_load_store_unsigned(true, true, 0, 31, 0), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDR);
    EXPECT_U64_EQ(inst.rn, 31);
    decode_or_die(encode_load_store_unsigned(false, true, 1, 31, 8), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STR);
    EXPECT_U64_EQ(inst.rn, 31);
    EXPECT_I64_EQ(inst.offset, 8);
}

static void test_decode_unscaled_writeback_and_pair(void) {
    /* TC-V03-DEC-U-001 through TC-V03-DEC-WB-002 and TC-V03-DEC-PAIR-001 through TC-V03-DEC-PAIR-003. */
    EmuDecodedInstruction inst;

    decode_or_die(encode_load_store_unscaled_or_wb(true, true, 0, 1, 7, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDUR);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_UNSCALED);
    EXPECT_I64_EQ(inst.offset, 7);

    decode_or_die(encode_load_store_unscaled_or_wb(true, true, 0, 1, -8, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDUR);
    EXPECT_I64_EQ(inst.offset, -8);

    decode_or_die(encode_load_store_unscaled_or_wb(false, true, 2, 3, 15, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STUR);
    EXPECT_U64_EQ(inst.rd, 2);
    EXPECT_U64_EQ(inst.rn, 3);
    EXPECT_I64_EQ(inst.offset, 15);
    decode_or_die(encode_load_store_unscaled_or_wb(false, true, 2, 3, -16, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STUR);
    EXPECT_I64_EQ(inst.offset, -16);

    decode_or_die(encode_load_store_unscaled_or_wb(true, false, 4, 5, 12, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDUR);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 4);
    EXPECT_U64_EQ(inst.rn, 5);
    EXPECT_I64_EQ(inst.offset, 12);
    EXPECT_U64_EQ(inst.access_size, 4);

    decode_or_die(encode_load_store_unscaled_or_wb(false, false, 6, 7, -4, EMU_ADDR_UNSCALED), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STUR);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 6);
    EXPECT_U64_EQ(inst.rn, 7);
    EXPECT_I64_EQ(inst.offset, -4);
    EXPECT_U64_EQ(inst.access_size, 4);

    decode_or_die(encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STR);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_PRE_INDEX);
    EXPECT_U64_EQ(inst.rn, 31);
    EXPECT_I64_EQ(inst.offset, -8);

    decode_or_die(encode_load_store_unscaled_or_wb(true, true, 0, 31, 8, EMU_ADDR_POST_INDEX), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDR);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_POST_INDEX);
    EXPECT_U64_EQ(inst.rn, 31);
    EXPECT_I64_EQ(inst.offset, 8);

    decode_or_die(encode_pair(false, 0, 1, 31, -16, EMU_ADDR_PRE_INDEX), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STP);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_PRE_INDEX);
    EXPECT_U64_EQ(inst.rd, 0);
    EXPECT_U64_EQ(inst.rt2, 1);
    EXPECT_U64_EQ(inst.rn, 31);
    EXPECT_I64_EQ(inst.offset, -16);
    EXPECT_U64_EQ(inst.access_size, 8);

    decode_or_die(encode_pair(true, 2, 3, 31, 16, EMU_ADDR_POST_INDEX), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDP);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_POST_INDEX);
    EXPECT_U64_EQ(inst.rd, 2);
    EXPECT_U64_EQ(inst.rt2, 3);
    EXPECT_I64_EQ(inst.offset, 16);

    decode_or_die(encode_pair(false, 4, 5, 6, 24, EMU_ADDR_PAIR_OFFSET), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_STP);
    EXPECT_MODE(inst.address_mode, EMU_ADDR_PAIR_OFFSET);
    EXPECT_U64_EQ(inst.rn, 6);
    EXPECT_I64_EQ(inst.offset, 24);
    decode_or_die(encode_pair(true, 4, 5, 6, -32, EMU_ADDR_PAIR_OFFSET), &inst);
    EXPECT_KIND(inst.kind, EMU_INST_LDP);
    EXPECT_I64_EQ(inst.offset, -32);
}

static void test_basic_load_store_execution(void) {
    /* TC-V03-EXEC-LS-001 through TC-V03-EXEC-LS-005. */
    char error[256];
    Emulator emu;
    uint64_t value64 = 0;
    uint32_t value32 = 0;
    uint8_t byte = 0;

    const uint32_t store_pre[] = {OP_MOVZ_X0_42, encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(store_pre, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE - 8u);
    EXPECT_TRUE(memory_read64(&emu.memory, emu.cpu.sp, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 42);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 3);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp - 8u, 0x1122334455667788ull, error, sizeof(error)));
    emu.cpu.sp -= 8u;
    const uint32_t load_sp[] = {encode_load_store_unsigned(true, true, 0, 31, 0), OP_HLT};
    load_program(&emu, load_sp, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x1122334455667788ull);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE - 8u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    uint64_t addr = emu.cpu.sp - 8u;
    emu.cpu.x[0] = 0xaaaabbbbccccddddu;
    for (uint64_t i = 0; i < 8; i++) {
        EXPECT_TRUE(memory_write8(&emu.memory, addr + i, 0xee, error, sizeof(error)));
    }
    const uint32_t str_w[] = {encode_load_store_unsigned(false, false, 0, 31, 0), OP_HLT};
    emu.cpu.sp = addr;
    load_program(&emu, str_w, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(memory_read32(&emu.memory, addr, &value32, error, sizeof(error)));
    EXPECT_U64_EQ(value32, 0xccccddddu);
    EXPECT_TRUE(memory_read8(&emu.memory, addr + 4u, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0xee);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.sp -= 4u;
    emu.cpu.x[0] = 0xffffffffffffffffull;
    EXPECT_TRUE(memory_write32(&emu.memory, emu.cpu.sp, 0xffffffffu, error, sizeof(error)));
    const uint32_t ldr_w[] = {encode_load_store_unsigned(true, false, 0, 31, 0), OP_HLT};
    load_program(&emu, ldr_w, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x00000000ffffffffull);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[5] = EMU_MEMORY_SIZE - 32u;
    const uint32_t offset_round_trip[] = {OP_MOVZ_X0_42, encode_load_store_unsigned(false, true, 0, 5, 16),
                                         encode_load_store_unsigned(true, true, 1, 5, 16), OP_HLT};
    load_program(&emu, offset_round_trip, 4);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 42);
    EXPECT_U64_EQ(emu.cpu.x[5], EMU_MEMORY_SIZE - 32u);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[5] = EMU_MEMORY_SIZE - 64u;
    emu.cpu.x[0] = 0xffffffff12345678ull;
    const uint32_t stur_w_ldur_w[] = {encode_load_store_unscaled_or_wb(false, false, 0, 5, -4, EMU_ADDR_UNSCALED),
                                      encode_load_store_unscaled_or_wb(true, false, 1, 5, -4, EMU_ADDR_UNSCALED),
                                      OP_HLT};
    load_program(&emu, stur_w_ldur_w, 3);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(memory_read32(&emu.memory, EMU_MEMORY_SIZE - 68u, &value32, error, sizeof(error)));
    EXPECT_U64_EQ(value32, 0x12345678u);
    EXPECT_U64_EQ(emu.cpu.x[1], 0x0000000012345678ull);
    EXPECT_U64_EQ(emu.cpu.x[5], EMU_MEMORY_SIZE - 64u);
    emulator_free(&emu);
}

static void test_stack_and_pair_execution(void) {
    /* TC-V03-EXEC-STK-001 through TC-V03-EXEC-STK-004 and TC-V03-EXEC-PAIR-001 through TC-V03-EXEC-PAIR-003. */
    char error[256];
    Emulator emu;
    uint64_t value64 = 0;

    const uint32_t push_pop[] = {OP_MOVZ_X0_42, encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX),
                                 encode_load_store_unscaled_or_wb(true, true, 1, 31, 8, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(push_pop, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 42);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    EXPECT_TRUE(memory_read64(&emu.memory, EMU_MEMORY_SIZE - 8u, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 42);
    emulator_free(&emu);

    const uint32_t lifo[] = {OP_MOVZ_X0_1, OP_MOVZ_X1_2,
                             encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX),
                             encode_load_store_unscaled_or_wb(false, true, 1, 31, -8, EMU_ADDR_PRE_INDEX),
                             encode_load_store_unscaled_or_wb(true, true, 2, 31, 8, EMU_ADDR_POST_INDEX),
                             encode_load_store_unscaled_or_wb(true, true, 3, 31, 8, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(lifo, 7, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 2);
    EXPECT_U64_EQ(emu.cpu.x[3], 1);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    const uint32_t stp_store[] = {OP_MOVZ_X0_11, OP_MOVZ_X1_22,
                                  encode_pair(false, 0, 1, 31, -16, EMU_ADDR_PRE_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(stp_store, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE - 16u);
    EXPECT_TRUE(memory_read64(&emu.memory, emu.cpu.sp, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 11);
    EXPECT_TRUE(memory_read64(&emu.memory, emu.cpu.sp + 8u, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 22);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.sp -= 16u;
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp, 0x1234ull, error, sizeof(error)));
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp + 8u, 0x5678ull, error, sizeof(error)));
    const uint32_t ldp_load[] = {encode_pair(true, 2, 3, 31, 16, EMU_ADDR_POST_INDEX), OP_HLT};
    load_program(&emu, ldp_load, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 0x1234);
    EXPECT_U64_EQ(emu.cpu.x[3], 0x5678);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    const uint32_t pair_round_trip[] = {OP_MOVZ_X0_100, OP_MOVZ_X1_200,
                                        encode_pair(false, 0, 1, 31, -16, EMU_ADDR_PRE_INDEX),
                                        encode_pair(true, 2, 3, 31, 16, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(pair_round_trip, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 100);
    EXPECT_U64_EQ(emu.cpu.x[3], 200);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[5] = EMU_MEMORY_SIZE - 64u;
    const uint32_t pair_offset_round_trip[] = {OP_MOVZ_X0_11, OP_MOVZ_X1_22,
                                              encode_pair(false, 0, 1, 5, 16, EMU_ADDR_PAIR_OFFSET),
                                              encode_pair(true, 2, 3, 5, 16, EMU_ADDR_PAIR_OFFSET), OP_HLT};
    load_program(&emu, pair_offset_round_trip, 5);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 11);
    EXPECT_U64_EQ(emu.cpu.x[3], 22);
    EXPECT_U64_EQ(emu.cpu.x[5], EMU_MEMORY_SIZE - 64u);
    EXPECT_TRUE(memory_read64(&emu.memory, EMU_MEMORY_SIZE - 48u, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 11);
    EXPECT_TRUE(memory_read64(&emu.memory, EMU_MEMORY_SIZE - 40u, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 22);
    emulator_free(&emu);
}

static void test_register_31_semantics(void) {
    /* TC-V03-SP-001 through TC-V03-SP-004. */
    char error[256];
    Emulator emu;
    uint64_t value64 = 0;

    const uint32_t sp_round_trip[] = {OP_MOVZ_X0_9, encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX),
                                      encode_load_store_unscaled_or_wb(true, true, 1, 31, 8, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(sp_round_trip, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 9);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp - 8u, 0xffffffffffffffffull, error, sizeof(error)));
    const uint32_t str_xzr[] = {encode_load_store_unscaled_or_wb(false, true, 31, 31, -8, EMU_ADDR_PRE_INDEX), OP_HLT};
    load_program(&emu, str_xzr, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE - 8u);
    EXPECT_TRUE(memory_read64(&emu.memory, emu.cpu.sp, &value64, error, sizeof(error)));
    EXPECT_U64_EQ(value64, 0);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x1111;
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp - 8u, 0x2222, error, sizeof(error)));
    emu.cpu.sp -= 8u;
    const uint32_t ldr_xzr[] = {encode_load_store_unsigned(true, true, 31, 31, 0), OP_HLT};
    load_program(&emu, ldr_xzr, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x1111);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 2);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[0] = 0x3333;
    emu.cpu.x[1] = EMU_MEMORY_SIZE;
    const uint32_t invalid_xzr_load[] = {encode_load_store_unsigned(true, true, 31, 1, 0), OP_HLT};
    load_program(&emu, invalid_xzr_load, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_U64_EQ(emu.cpu.x[0], 0x3333);
    emulator_free(&emu);
}

static void test_error_and_edge_cases(void) {
    /* TC-V03-ERR-001 through TC-V03-ERR-009. */
    char error[256];
    Emulator emu;
    uint8_t byte = 0;
    EmuDecodedInstruction inst;

    init_emulator_or_die(&emu);
    emu.cpu.x[1] = EMU_MEMORY_SIZE;
    emu.cpu.x[0] = 0x1234;
    const uint32_t oob_load[] = {encode_load_store_unsigned(true, true, 0, 1, 0), OP_HLT};
    load_program(&emu, oob_load, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_U64_EQ(emu.cpu.x[0], 0x1234);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 0);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.sp = EMU_MEMORY_SIZE - 4u;
    emu.cpu.x[0] = 0x1122334455667788ull;
    for (uint64_t i = 0; i < 4; i++) {
        EXPECT_TRUE(memory_write8(&emu.memory, emu.cpu.sp + i, 0xaa, error, sizeof(error)));
    }
    const uint32_t oob_store[] = {encode_load_store_unsigned(false, true, 0, 31, 0), OP_HLT};
    load_program(&emu, oob_store, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    for (uint64_t i = 0; i < 4; i++) {
        EXPECT_TRUE(memory_read8(&emu.memory, emu.cpu.sp + i, &byte, error, sizeof(error)));
        EXPECT_U8_EQ(byte, 0xaa);
    }
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[1] = 4;
    emu.cpu.x[0] = 0x55;
    EXPECT_TRUE(memory_write8(&emu.memory, 0, 0xcc, error, sizeof(error)));
    const uint32_t pre_underflow[] = {encode_load_store_unscaled_or_wb(false, true, 0, 1, -8, EMU_ADDR_PRE_INDEX), OP_HLT};
    load_program(&emu, pre_underflow, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "overflow");
    EXPECT_U64_EQ(emu.cpu.x[1], 4);
    EXPECT_TRUE(memory_read8(&emu.memory, 0, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0xcc);
    emulator_free(&emu);

    Memory fake_memory = {.bytes = NULL, .size = EMU_MEMORY_SIZE};
    Cpu cpu;
    cpu_init(&cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);
    cpu.x[1] = UINT64_MAX - 4ull;
    inst.kind = EMU_INST_LDR;
    inst.address_mode = EMU_ADDR_POST_INDEX;
    inst.rn = 1;
    inst.rd = 0;
    inst.access_size = 8;
    inst.offset = 8;
    uint64_t address = 0;
    uint64_t writeback = 0;
    bool has_writeback = false;
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_calculate_memory_access(&cpu, &inst, &fake_memory, &address, &writeback, &has_writeback, error,
                                             sizeof(error)));
    EXPECT_STR_CONTAINS(error, "post-index write-back overflow");
    EXPECT_U64_EQ(cpu.x[0], 0);
    EXPECT_U64_EQ(cpu.x[1], UINT64_MAX - 4ull);

    init_emulator_or_die(&emu);
    emu.cpu.sp = EMU_MEMORY_SIZE - 8u;
    emu.cpu.x[0] = 0x1111;
    emu.cpu.x[1] = 0x2222;
    for (uint64_t i = 0; i < 8; i++) {
        EXPECT_TRUE(memory_write8(&emu.memory, emu.cpu.sp + i, 0xdd, error, sizeof(error)));
    }
    const uint32_t stp_cross[] = {encode_pair(false, 0, 1, 31, 0, EMU_ADDR_PAIR_OFFSET), OP_HLT};
    load_program(&emu, stp_cross, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    for (uint64_t i = 0; i < 8; i++) {
        EXPECT_TRUE(memory_read8(&emu.memory, emu.cpu.sp + i, &byte, error, sizeof(error)));
        EXPECT_U8_EQ(byte, 0xdd);
    }
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.sp = EMU_MEMORY_SIZE - 8u;
    emu.cpu.x[2] = 0xaaaa;
    emu.cpu.x[3] = 0xbbbb;
    const uint32_t ldp_cross[] = {encode_pair(true, 2, 3, 31, 0, EMU_ADDR_PAIR_OFFSET), OP_HLT};
    load_program(&emu, ldp_cross, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_U64_EQ(emu.cpu.x[2], 0xaaaa);
    EXPECT_U64_EQ(emu.cpu.x[3], 0xbbbb);
    emulator_free(&emu);

    reset_error(error, sizeof(error));
    EXPECT_TRUE(cpu_decode(0x394003e0u, &inst, error, sizeof(error))); /* ldrb w0, [sp] */
    EXPECT_TRUE(inst.kind == EMU_INST_LDR);
    EXPECT_U8_EQ(inst.access_size, 1);
    reset_error(error, sizeof(error));
    EXPECT_TRUE(cpu_decode(0x390003e0u, &inst, error, sizeof(error))); /* strb w0, [sp] */
    EXPECT_TRUE(inst.kind == EMU_INST_STR);
    EXPECT_U8_EQ(inst.access_size, 1);
    reset_error(error, sizeof(error));
    EXPECT_TRUE(cpu_decode(0xb98003e0u, &inst, error, sizeof(error))); /* ldrsw x0, [sp] */
    EXPECT_TRUE(inst.kind == EMU_INST_LDR);
    EXPECT_U8_EQ(inst.access_size, 4);
    EXPECT_TRUE(inst.sign_extend);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(memory_write64(&emu.memory, emu.cpu.sp - 8u, 0xabcdef, error, sizeof(error)));
    const uint32_t ldr_sp[] = {encode_load_store_unsigned(true, true, 0, 31, 0), OP_HLT};
    load_program(&emu, ldr_sp, 2);
    reset_error(error, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.cpu.x[1] = EMU_MEMORY_SIZE - 17u;
    emu.cpu.x[0] = 0x0102030405060708ull;
    const uint32_t unaligned[] = {encode_load_store_unsigned(false, true, 0, 1, 0),
                                  encode_load_store_unsigned(true, true, 2, 1, 0), OP_HLT};
    load_program(&emu, unaligned, 3);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 0x0102030405060708ull);
    emulator_free(&emu);
}

static void test_acceptance_programs(void) {
    /* TC-V03-ACC-001 through TC-V03-ACC-004 using in-memory equivalents of the example programs. */
    char error[256];
    Emulator emu;

    const uint32_t memory_store_load[] = {OP_MOVZ_X0_42, encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX),
                                          encode_load_store_unscaled_or_wb(true, true, 1, 31, 8, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(memory_store_load, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 42);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    const uint32_t stack_push_pop[] = {OP_MOVZ_X0_1, OP_MOVZ_X1_2,
                                       encode_load_store_unscaled_or_wb(false, true, 0, 31, -8, EMU_ADDR_PRE_INDEX),
                                       encode_load_store_unscaled_or_wb(false, true, 1, 31, -8, EMU_ADDR_PRE_INDEX),
                                       encode_load_store_unscaled_or_wb(true, true, 2, 31, 8, EMU_ADDR_POST_INDEX),
                                       encode_load_store_unscaled_or_wb(true, true, 3, 31, 8, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(stack_push_pop, 7, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 2);
    EXPECT_U64_EQ(emu.cpu.x[3], 1);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    const uint32_t pair[] = {OP_MOVZ_X0_100, OP_MOVZ_X1_200, encode_pair(false, 0, 1, 31, -16, EMU_ADDR_PRE_INDEX),
                             encode_pair(true, 2, 3, 31, 16, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(pair, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 100);
    EXPECT_U64_EQ(emu.cpu.x[3], 200);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);

    const uint32_t w_example[] = {OP_MOVZ_X1_FFFF_LSL16, OP_ADD_X1_X1_0X123,
                                  encode_load_store_unscaled_or_wb(false, false, 1, 31, -4, EMU_ADDR_PRE_INDEX),
                                  encode_load_store_unscaled_or_wb(true, false, 2, 31, 4, EMU_ADDR_POST_INDEX), OP_HLT};
    EXPECT_STATUS(run_program(w_example, 5, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[2], 0xffff0123u);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    emulator_free(&emu);
}

int main(void) {
    test_decode_single_register_load_store();
    test_decode_unscaled_writeback_and_pair();
    test_basic_load_store_execution();
    test_stack_and_pair_execution();
    test_register_31_semantics();
    test_error_and_edge_cases();
    test_acceptance_programs();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.3 tests failed: %d failure(s) out of %d assertion(s)\n", tests_failed, tests_run);
        return 1;
    }

    printf("v0.3 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_HLT_0 0xd4400000u
#define OP_SVC_0 0xd4000001u
#define OP_UNSUPPORTED 0xffffffffu
#define OP_LOGICAL_IMM 0xb24003e0u
#define OP_UXTB_ALIAS 0x53001c20u
#define OP_SXTW_ALIAS 0x93407c20u

static int tests_run = 0;
static int tests_failed = 0;

static void fail_at(const char *file, int line, const char *expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    tests_failed++;
}

#define EXPECT_TRUE(expr) do { tests_run++; if (!(expr)) fail_at(__FILE__, __LINE__, #expr); } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_STATUS(actual, expected) EXPECT_U64_EQ((actual), (expected))
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
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)
#define EXPECT_MEM_EQ(actual, expected, length) do { \
    tests_run++; \
    if (memcmp((actual), (expected), (length)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: memory differs for %s\n", __FILE__, __LINE__, #actual); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_movz(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x52800000u | (((uint32_t)(shift / 16u) & 0x3u) << 21u) |
           ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_movn(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x12800000u | (((uint32_t)(shift / 16u) & 0x3u) << 21u) |
           ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_movk(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x72800000u | (((uint32_t)(shift / 16u) & 0x3u) << 21u) |
           ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_addsub_imm(uint8_t rd, uint8_t rn, uint16_t imm12, bool is_sub, bool is_64_bit) {
    uint32_t base = is_64_bit ? 0x91000000u : 0x11000000u;
    if (is_sub) {
        base |= 0x40000000u;
    }
    return base | ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rd & 0x1fu);
}

static uint32_t encode_addsub_reg(uint8_t rd, uint8_t rn, uint8_t rm, bool is_sub, bool is_64_bit,
                                  uint8_t shift_type, uint8_t shift_amount) {
    uint32_t base = is_64_bit ? 0x8b000000u : 0x0b000000u;
    if (is_sub) {
        base |= 0x40000000u;
    }
    return base | ((uint32_t)(shift_type & 0x3u) << 22u) | ((uint32_t)(rm & 0x1fu) << 16u) |
           ((uint32_t)(shift_amount & 0x3fu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rd & 0x1fu);
}

static uint32_t encode_logical_reg(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t opc, bool is_64_bit,
                                   uint8_t shift_type, uint8_t shift_amount) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x0a000000u | ((uint32_t)(opc & 0x3u) << 29u) |
           ((uint32_t)(shift_type & 0x3u) << 22u) | ((uint32_t)(rm & 0x1fu) << 16u) |
           ((uint32_t)(shift_amount & 0x3fu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rd & 0x1fu);
}

static uint32_t encode_shift_imm(uint8_t rd, uint8_t rn, uint8_t amount, uint8_t kind, bool is_64_bit) {
    unsigned width = is_64_bit ? 64u : 32u;
    uint32_t base = is_64_bit ? 0xd3400000u : 0x53000000u;
    uint8_t immr = 0;
    uint8_t imms = 0;
    if (kind == 0) { /* LSL */
        immr = (uint8_t)((width - amount) & (width - 1u));
        imms = (uint8_t)(width - 1u - amount);
    } else { /* LSR/ASR */
        base = (kind == 2 ? (is_64_bit ? 0x93400000u : 0x13000000u) : base);
        immr = amount;
        imms = (uint8_t)(width - 1u);
    }
    return base | ((uint32_t)immr << 16u) | ((uint32_t)imms << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) |
           (rd & 0x1fu);
}

static uint32_t encode_muldiv(uint8_t rd, uint8_t rn, uint8_t rm, int op, bool is_64_bit) {
    if (op == 0) { /* MUL */
        return (is_64_bit ? 0x9b000000u : 0x1b000000u) | ((uint32_t)(rm & 0x1fu) << 16u) |
               (31u << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rd & 0x1fu);
    }
    return (is_64_bit ? 0x9ac00000u : 0x1ac00000u) | (op == 2 ? 0x00000c00u : 0x00000800u) |
           ((uint32_t)(rm & 0x1fu) << 16u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rd & 0x1fu);
}

static uint32_t encode_adr(uint8_t rd, int32_t imm, bool page) {
    int32_t scaled = page ? imm / 4096 : imm;
    uint32_t uimm = (uint32_t)scaled & 0x1fffffu;
    return (page ? 0x90000000u : 0x10000000u) | ((uimm & 0x3u) << 29u) | (((uimm >> 2u) & 0x7ffffu) << 5u) |
           (rd & 0x1fu);
}

static uint32_t encode_ldrstr_unsigned(uint8_t rt, uint8_t rn, uint16_t offset, bool is_load, uint8_t access_size) {
    uint32_t size = access_size == 8 ? 3u : (access_size == 4 ? 2u : (access_size == 2 ? 1u : 0u));
    uint32_t opc = is_load ? 1u : 0u;
    uint32_t scale = access_size == 8 ? 3u : (access_size == 4 ? 2u : (access_size == 2 ? 1u : 0u));
    uint32_t imm12 = ((uint32_t)offset) >> scale;
    return (size << 30u) | 0x39000000u | (opc << 22u) | (imm12 << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) |
           (rt & 0x1fu);
}

static uint32_t encode_ldrstr_unscaled(uint8_t rt, uint8_t rn, int16_t offset, bool is_load, uint8_t access_size,
                                       uint8_t mode) {
    uint32_t size = access_size == 8 ? 3u : (access_size == 4 ? 2u : (access_size == 2 ? 1u : 0u));
    uint32_t opc = is_load ? 1u : 0u;
    return (size << 30u) | 0x38000000u | (opc << 22u) | ((uint32_t)(mode & 3u) << 10u) |
           (((uint32_t)offset & 0x1ffu) << 12u) | ((uint32_t)(rn & 0x1fu) << 5u) | (rt & 0x1fu);
}

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static void put_op(Memory *memory, uint64_t address, uint32_t opcode) {
    char error[256];
    if (!memory_write32(memory, address, opcode, error, sizeof(error))) {
        fprintf(stderr, "memory_write32 failed: %s\n", error);
        exit(1);
    }
}

static void init_cpu_memory(Cpu *cpu, Memory *memory) {
    char error[256];
    if (!memory_init(memory, EMU_MEMORY_SIZE, error, sizeof(error))) {
        fprintf(stderr, "memory_init failed: %s\n", error);
        exit(1);
    }
    cpu_init(cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);
}

static EmuStatus step_opcode(Cpu *cpu, Memory *memory, uint32_t opcode, char *error, size_t error_size) {
    put_op(memory, cpu->pc, opcode);
    return cpu_step(cpu, memory, error, error_size);
}

static void expect_format(uint32_t opcode, uint64_t address, const char *needle) {
    char text[128];
    EXPECT_TRUE(cpu_format_instruction(opcode, address, text, sizeof(text)));
    EXPECT_STR_CONTAINS(text, needle);
}

static void test_decode_and_formatting(void) {
    /* TC-V09-DEC-001 through TC-V09-DEC-014: new compiler-oriented opcodes decode/format clearly. */
    char error[256];
    EmuDecodedInstruction inst;

    EXPECT_TRUE(cpu_decode(encode_movk(0, 0x1234, 16, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_MOVK);
    EXPECT_U64_EQ(inst.rd, 0u);
    EXPECT_U64_EQ(inst.imm, 0x12340000u);
    EXPECT_TRUE(inst.is_64_bit);
    expect_format(encode_movk(0, 0x1234, 16, true), EMU_LOAD_ADDRESS, "movk x0, #0x1234, lsl #16");

    EXPECT_TRUE(cpu_decode(encode_addsub_reg(2, 0, 1, false, true, 0, 0), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ADD_REG);
    EXPECT_U64_EQ(inst.rd, 2u);
    EXPECT_TRUE(cpu_decode(encode_addsub_reg(3, 1, 0, true, false, 0, 0), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_SUB_REG);
    EXPECT_FALSE(inst.is_64_bit);
    expect_format(encode_addsub_reg(2, 0, 1, false, true, 0, 0), EMU_LOAD_ADDRESS, "add x2, x0, x1");

    EXPECT_TRUE(cpu_decode(encode_logical_reg(4, 0, 2, 0, true, 0, 0), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_AND_REG);
    EXPECT_TRUE(cpu_decode(encode_logical_reg(5, 0, 1, 1, true, 0, 0), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ORR_REG);
    EXPECT_TRUE(cpu_decode(encode_logical_reg(6, 0, 1, 2, false, 0, 0), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_EOR_REG);
    EXPECT_TRUE(cpu_decode(OP_LOGICAL_IMM, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ORR_IMM);

    EXPECT_TRUE(cpu_decode(encode_shift_imm(7, 0, 1, 0, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_LSL_IMM);
    EXPECT_TRUE(cpu_decode(encode_shift_imm(8, 0, 63, 1, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_LSR_IMM);
    EXPECT_TRUE(cpu_decode(encode_shift_imm(9, 0, 31, 2, false), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ASR_IMM);
    EXPECT_FALSE(cpu_decode(0x5300fc00u, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported bitfield");

    EXPECT_TRUE(cpu_decode(encode_muldiv(10, 0, 1, 0, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_MUL);
    EXPECT_TRUE(cpu_decode(encode_muldiv(11, 0, 1, 1, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_UDIV);
    EXPECT_TRUE(cpu_decode(encode_muldiv(12, 0, 1, 2, false), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_SDIV);

    EXPECT_TRUE(cpu_decode(encode_adr(13, -4, false), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ADR);
    EXPECT_U64_EQ((uint64_t)inst.offset, (uint64_t)-4ll);
    EXPECT_TRUE(cpu_decode(encode_adr(14, -4096, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ADRP);
    EXPECT_U64_EQ((uint64_t)inst.offset, (uint64_t)-4096ll);

    EXPECT_TRUE(cpu_decode(encode_ldrstr_unsigned(15, 0, 0, true, 1), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_LDR);
    EXPECT_U64_EQ(inst.access_size, 1u);
    EXPECT_TRUE(cpu_decode(encode_ldrstr_unsigned(15, 0, 2, false, 2), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_STR);
    EXPECT_U64_EQ(inst.access_size, 2u);
    expect_format(encode_ldrstr_unsigned(15, 0, 1, true, 1), EMU_LOAD_ADDRESS, "ldrb w15, [x0, #1]");
    expect_format(encode_ldrstr_unsigned(16, 0, 2, false, 2), EMU_LOAD_ADDRESS, "strh w16, [x0, #2]");

    EXPECT_TRUE(cpu_decode(OP_UXTB_ALIAS, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_BITFIELD_UNSIGNED);
    EXPECT_TRUE(cpu_decode(OP_SXTW_ALIAS, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_BITFIELD_SIGNED);
    EXPECT_FALSE(cpu_decode(OP_UNSUPPORTED, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported instruction");
}

static void test_execution_constants_alu_shifts_division(void) {
    /* TC-V09-EXEC-001 through TC-V09-EXEC-018 and TC-V09-ERR-001 through TC-V09-ERR-006. */
    char error[256];
    Cpu cpu;
    Memory memory;
    init_cpu_memory(&cpu, &memory);

    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_movz(0, 0x1111, 0, true), error, sizeof(error)), EMU_OK);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_movk(0, 0x2222, 16, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x0000000022221111ull);

    cpu_write_register(&cpu, 1, true, UINT64_MAX);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_movk(1, 0x1234, 0, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 1), 0xffff1234u);

    cpu_write_register(&cpu, 0, true, UINT64_MAX);
    cpu_write_register(&cpu, 1, true, 1);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_reg(2, 0, 1, false, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 2), 0u);
    cpu_write_register(&cpu, 0, true, 0);
    cpu_write_register(&cpu, 1, true, 1);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_reg(3, 0, 1, true, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 3), UINT64_MAX);

    cpu_write_register(&cpu, 0, true, 0xffffffffull);
    cpu_write_register(&cpu, 1, true, 1);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_reg(4, 0, 1, false, false, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 4), 0u);
    cpu_write_register(&cpu, 5, true, 0xabc);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_reg(31, 0, 1, false, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 31), 0u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_reg(6, 31, 1, false, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 6), 1u);

    cpu_write_register(&cpu, 0, true, 0xf0f0u);
    cpu_write_register(&cpu, 1, true, 0x0ff0u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_logical_reg(7, 0, 1, 0, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 7), 0x00f0u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_logical_reg(8, 0, 1, 1, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 8), 0xfff0u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_logical_reg(9, 0, 1, 2, false, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 9), 0xff00u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_logical_reg(31, 0, 1, 1, true, 0, 0), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 31), 0u);

    cpu_write_register(&cpu, 0, true, 0x8000000000000001ull);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_shift_imm(10, 0, 0, 0, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 10), 0x8000000000000001ull);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_shift_imm(11, 0, 1, 0, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 11), 0x0000000000000002ull);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_shift_imm(12, 0, 63, 1, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 12), 1u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_shift_imm(13, 0, 63, 2, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 13), UINT64_MAX);

    cpu_write_register(&cpu, 0, true, UINT64_MAX);
    cpu_write_register(&cpu, 1, true, 2);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_muldiv(14, 0, 1, 0, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 14), UINT64_MAX - 1u);
    cpu_write_register(&cpu, 0, true, 100);
    cpu_write_register(&cpu, 1, true, 7);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_muldiv(15, 0, 1, 1, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 15), 14u);
    cpu_write_register(&cpu, 1, true, 0);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_muldiv(16, 0, 1, 1, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 16), 0u);
    cpu_write_register(&cpu, 0, false, (uint32_t)-7);
    cpu_write_register(&cpu, 1, false, 3);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_muldiv(17, 0, 1, 2, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 17), (uint32_t)-2);
    cpu_write_register(&cpu, 0, false, 0x80000000u);
    cpu_write_register(&cpu, 1, false, 0xffffffffu);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_muldiv(18, 0, 1, 2, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 18), 0x80000000u);

    memory_free(&memory);
}

static void test_address_generation(void) {
    /* TC-V09-ADDR-001 through TC-V09-ADDR-007 and TC-V09-ERR-011 through TC-V09-ERR-013. */
    char error[256];
    Cpu cpu;
    Memory memory;
    init_cpu_memory(&cpu, &memory);

    cpu.pc = 0x1800;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_adr(0, 0x20, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x1820u);
    cpu.pc = 0x1800;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_adr(1, -4, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 1), 0x17fcu);
    cpu.pc = 0x1800;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_adr(2, 3, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 2), 0x1803u);
    cpu.pc = 0x1ff0;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_adr(3, 4096, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 3), 0x2000u);
    cpu.pc = 0x2000;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_adr(4, -4096, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 4), 0x1000u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 0, true, EMU_MEMORY_SIZE);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(5, 0, 0, true, 1), error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "out of bounds");

    memory_free(&memory);
}

static void test_byte_halfword_memory(void) {
    /* TC-V09-MEM-001 through TC-V09-MEM-010 and TC-V09-ERR-007 through TC-V09-ERR-009. */
    char error[256];
    Cpu cpu;
    Memory memory;
    init_cpu_memory(&cpu, &memory);

    memory.bytes[0x2000] = 0xffu;
    cpu_write_register(&cpu, 1, true, 0x2000);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(0, 1, 0, true, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0xffu);

    memory.bytes[0x2000] = 0xaa;
    memory.bytes[0x2001] = 0xbb;
    memory.bytes[0x2002] = 0xcc;
    cpu_write_register(&cpu, 0, true, 0x12345678u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(0, 1, 1, false, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(memory.bytes[0x2000], 0xaau);
    EXPECT_U64_EQ(memory.bytes[0x2001], 0x78u);
    EXPECT_U64_EQ(memory.bytes[0x2002], 0xccu);

    memory.bytes[0x2001] = 0xff;
    memory.bytes[0x2002] = 0xff;
    cpu_write_register(&cpu, 11, true, 0x2001);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(2, 11, 0, true, 2), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 2), 0xffffu);

    memory.bytes[0x2000] = 0xaa;
    memory.bytes[0x2001] = 0xbb;
    memory.bytes[0x2002] = 0xcc;
    memory.bytes[0x2003] = 0xdd;
    cpu_write_register(&cpu, 2, true, 0x12345678u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(2, 1, 2, false, 2), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(memory.bytes[0x2000], 0xaau);
    EXPECT_U64_EQ(memory.bytes[0x2001], 0xbbu);
    EXPECT_U64_EQ(memory.bytes[0x2002], 0x78u);
    EXPECT_U64_EQ(memory.bytes[0x2003], 0x56u);

    cpu_write_register(&cpu, 3, true, EMU_MEMORY_SIZE - 1u);
    cpu_write_register(&cpu, 4, true, 0x5a);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(4, 3, 0, false, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(memory.bytes[EMU_MEMORY_SIZE - 1u], 0x5au);
    cpu_write_register(&cpu, 5, true, 0x2001);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(6, 5, 0, true, 2), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 6), 0x78bbu);

    memory.bytes[EMU_MEMORY_SIZE - 1u] = 0xeeu;
    cpu_write_register(&cpu, 7, true, EMU_MEMORY_SIZE - 1u);
    cpu_write_register(&cpu, 8, true, 0x1234u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(8, 7, 0, false, 2), error, sizeof(error)), EMU_ERROR);
    EXPECT_U64_EQ(memory.bytes[EMU_MEMORY_SIZE - 1u], 0xeeu);
    EXPECT_STR_CONTAINS(error, "out of bounds");

    cpu.sp = 0x3000;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_imm(31, 31, 16, false, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.sp, 0x3010u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_addsub_imm(31, 31, 16, true, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.sp, 0x3000u);
    memory.bytes[0x3000] = 0x42;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(9, 31, 0, true, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 9), 0x42u);
    memory.bytes[0x3000] = 0x99;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(31, 31, 0, true, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 31), 0u);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unsigned(31, 31, 0, false, 1), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(memory.bytes[0x3000], 0u);

    cpu.sp = 0x4000;
    cpu_write_register(&cpu, 10, true, 0xab);
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unscaled(10, 31, 1, false, 1, 3), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.sp, 0x4001u);
    EXPECT_U64_EQ(memory.bytes[0x4001], 0xabu);
    cpu.sp = EMU_MEMORY_SIZE;
    EXPECT_STATUS(step_opcode(&cpu, &memory, encode_ldrstr_unscaled(10, 31, 1, false, 2, 3), error, sizeof(error)), EMU_ERROR);
    EXPECT_U64_EQ(cpu.sp, EMU_MEMORY_SIZE);

    memory_free(&memory);
}

static void test_emulator_runtime_edges(void) {
    /* TC-V09-ERR-014 through TC-V09-ERR-020: runtime/error behavior remains clear. */
    char error[512];
    Emulator emu;
    EXPECT_TRUE(emulator_init(&emu, error, sizeof(error)));
    emu.instruction_limit = 3;
    for (size_t i = 0; i < 4; i++) {
        put_op(&emu.memory, EMU_LOAD_ADDRESS + i * 4u, encode_b(0));
    }
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_STR_CONTAINS(error, "pc=");
    emulator_free(&emu);

    EXPECT_TRUE(emulator_init(&emu, error, sizeof(error)));
    uint64_t pc = EMU_LOAD_ADDRESS;
    put_op(&emu.memory, pc, encode_movz(0, 7, 0, true)); pc += 4;
    put_op(&emu.memory, pc, encode_movz(8, EMU_SYSCALL_EXIT, 0, true)); pc += 4;
    put_op(&emu.memory, pc, OP_SVC_0);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 7u);
    emulator_free(&emu);

    EXPECT_TRUE(emulator_init(&emu, error, sizeof(error)));
    pc = EMU_LOAD_ADDRESS;
    put_op(&emu.memory, pc, encode_movn(0, 0, 0, true)); pc += 4; /* x0 = -1 */
    put_op(&emu.memory, pc, encode_movz(8, EMU_SYSCALL_EXIT, 0, true)); pc += 4;
    put_op(&emu.memory, pc, OP_SVC_0);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.guest_exit_code, 255u);
    emulator_free(&emu);
}

int main(void) {
    test_decode_and_formatting();
    test_execution_constants_alu_shifts_division();
    test_address_generation();
    test_byte_halfword_memory();
    test_emulator_runtime_edges();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.9 tests failed: %d/%d assertions failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v0.9 tests passed: %d assertions\n", tests_run);
    return 0;
}

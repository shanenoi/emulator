#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_tbz(uint8_t rt, uint8_t bit, int32_t offset, bool nonzero) {
    return ((uint32_t)((bit >> 5u) & 1u) << 31u) | 0x36000000u | (nonzero ? 0x01000000u : 0u) |
           ((uint32_t)(bit & 0x1fu) << 19u) | (((uint32_t)(offset / 4) & 0x3fffu) << 5u) | (rt & 0x1fu);
}

static uint32_t encode_br(uint8_t rn) {
    return 0xd61f0000u | ((uint32_t)(rn & 0x1fu) << 5u);
}

static uint32_t encode_blr(uint8_t rn) {
    return 0xd63f0000u | ((uint32_t)(rn & 0x1fu) << 5u);
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

static void test_decode_new_groups(void) {
    char error[256] = {0};
    EmuDecodedInstruction inst;

    EXPECT_TRUE(cpu_decode(encode_tbz(0, 63, 16, false), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_TBZ);
    EXPECT_U64_EQ(inst.shift_amount, 63u);
    EXPECT_TRUE(inst.is_64_bit);

    EXPECT_TRUE(cpu_decode(0xd61f0040u, &inst, error, sizeof(error))); /* br x2 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_BR);
    EXPECT_U64_EQ(inst.rn, 2u);
    EXPECT_TRUE(cpu_decode(0xd63f0060u, &inst, error, sizeof(error))); /* blr x3 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_BLR);

    EXPECT_TRUE(cpu_decode(0x92401c20u, &inst, error, sizeof(error))); /* and x0, x1, #0xff */
    EXPECT_U64_EQ(inst.kind, EMU_INST_AND_IMM);
    EXPECT_U64_EQ(inst.imm, 0xffu);
    EXPECT_TRUE(cpu_decode(0xf24100e6u, &inst, error, sizeof(error))); /* ands x6, x7, high bit */
    EXPECT_U64_EQ(inst.kind, EMU_INST_AND_IMM);
    EXPECT_TRUE(inst.sets_flags);

    EXPECT_TRUE(cpu_decode(0x9a820020u, &inst, error, sizeof(error))); /* csel x0, x1, x2, eq */
    EXPECT_U64_EQ(inst.kind, EMU_INST_CSEL);
    EXPECT_U64_EQ(inst.condition, EMU_COND_EQ);

    EXPECT_TRUE(cpu_decode(0x8b220820u, &inst, error, sizeof(error))); /* add x0, x1, w2, uxtb #2 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_ADD_EXT_REG);
    EXPECT_U64_EQ(inst.extend_type, 0u);

    EXPECT_TRUE(cpu_decode(0x58000060u, &inst, error, sizeof(error))); /* ldr x0, literal */
    EXPECT_U64_EQ(inst.kind, EMU_INST_LDR_LITERAL);
    EXPECT_U64_EQ(inst.access_size, 8u);

    EXPECT_TRUE(cpu_decode(0xf8626820u, &inst, error, sizeof(error))); /* ldr x0, [x1, x2] */
    EXPECT_U64_EQ(inst.kind, EMU_INST_LDR);
    EXPECT_U64_EQ(inst.address_mode, EMU_ADDR_REGISTER_OFFSET);

    EXPECT_TRUE(cpu_decode(0x38ee69acu, &inst, error, sizeof(error))); /* ldrsb w12, [x13, x14] */
    EXPECT_TRUE(inst.sign_extend);
    EXPECT_U64_EQ(inst.access_size, 1u);

    EXPECT_TRUE(cpu_decode(0x9b031041u, &inst, error, sizeof(error))); /* madd x1, x2, x3, x4 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_MADD);
    EXPECT_TRUE(cpu_decode(0x9b039041u, &inst, error, sizeof(error))); /* msub x1, x2, x3, x4 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_MSUB);

    EXPECT_TRUE(cpu_decode(0xd3483c20u, &inst, error, sizeof(error))); /* ubfx x0, x1, #8, #8 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_BITFIELD_UNSIGNED);
    EXPECT_TRUE(cpu_decode(0x93407e30u, &inst, error, sizeof(error))); /* sxtw x16, w17 */
    EXPECT_U64_EQ(inst.kind, EMU_INST_BITFIELD_SIGNED);
}

static void test_tbz_tbnz_execute(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    cpu_write_register(&cpu, 0, true, 0);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, encode_tbz(0, 3, 8, false), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 8u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 0, true, 8u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, encode_tbz(0, 3, 8, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 8u);

    cpu.pc = EMU_LOAD_ADDRESS;
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, encode_tbz(0, 2, 8, true), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 4u);
    memory_free(&memory);
}

static void test_indirect_branches(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    cpu_write_register(&cpu, 2, true, EMU_LOAD_ADDRESS + 0x40u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, encode_br(2), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 0x40u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 3, true, EMU_LOAD_ADDRESS + 0x80u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, encode_blr(3), error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 0x80u);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 30), EMU_LOAD_ADDRESS + 4u);
    memory_free(&memory);
}

static void test_logical_immediate_and_csel(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    cpu_write_register(&cpu, 1, true, 0x1234u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x92401c20u, error, sizeof(error)), EMU_OK); /* and x0, x1, #0xff */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x34u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 7, true, 0x8000000000000000ull);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xf24100e6u, error, sizeof(error)), EMU_OK); /* ands x6, x7, high bit */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 6), 0x8000000000000000ull);
    EXPECT_TRUE(cpu.flags.n);
    EXPECT_FALSE(cpu.flags.z);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu.flags.z = true;
    cpu_write_register(&cpu, 1, true, 11u);
    cpu_write_register(&cpu, 2, true, 22u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x9a820020u, error, sizeof(error)), EMU_OK); /* csel x0, x1, x2, eq */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 11u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu.flags.z = true;
    cpu_write_register(&cpu, 4, false, 40u);
    cpu_write_register(&cpu, 5, false, 50u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x1a851483u, error, sizeof(error)), EMU_OK); /* csinc w3,w4,w5,ne */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 3), 51u);
    memory_free(&memory);
}

static void test_extended_arithmetic_and_madd(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    cpu_write_register(&cpu, 1, true, 10u);
    cpu_write_register(&cpu, 2, true, 0xffu);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x8b220820u, error, sizeof(error)), EMU_OK); /* add x0,x1,w2,uxtb #2 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 10u + (0xffu << 2u));

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 10, true, 1000u);
    cpu_write_register(&cpu, 11, false, 0xffffffffu);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xeb2bcd49u, error, sizeof(error)), EMU_OK); /* subs x9,x10,w11,sxtw #3 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 9), 1008u);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 2, true, 6u);
    cpu_write_register(&cpu, 3, true, 7u);
    cpu_write_register(&cpu, 4, true, 5u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x9b031041u, error, sizeof(error)), EMU_OK); /* madd x1,x2,x3,x4 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 1), 47u);

    cpu.pc = EMU_LOAD_ADDRESS;
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x9b039041u, error, sizeof(error)), EMU_OK); /* msub x1,x2,x3,x4 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 1), UINT64_MAX - 36u);
    memory_free(&memory);
}

static void test_load_store_new_forms(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    EXPECT_TRUE(memory_write64(&memory, EMU_LOAD_ADDRESS + 0x0c, 0x1122334455667788ull, error, sizeof(error)));
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x58000060u, error, sizeof(error)), EMU_OK); /* ldr x0, pc+12 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x1122334455667788ull);

    cpu.pc = EMU_LOAD_ADDRESS + 8u;
    EXPECT_TRUE(memory_write32(&memory, EMU_LOAD_ADDRESS + 0x14, 0xfffffff0u, error, sizeof(error)));
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x98000062u, error, sizeof(error)), EMU_OK); /* ldrsw x2, pc+20 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 2), 0xfffffffffffffff0ull);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 1, true, 0x3000u);
    cpu_write_register(&cpu, 2, true, 8u);
    EXPECT_TRUE(memory_write64(&memory, 0x3008u, 0xaabbccdd11223344ull, error, sizeof(error)));
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xf8626820u, error, sizeof(error)), EMU_OK); /* ldr x0,[x1,x2] */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0xaabbccdd11223344ull);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 4, true, 0x3010u);
    cpu_write_register(&cpu, 5, false, 3u);
    cpu_write_register(&cpu, 3, false, 0x12345678u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xb8255883u, error, sizeof(error)), EMU_OK); /* str w3,[x4,w5,uxtw #2] */
    {
        uint32_t value = 0;
        EXPECT_TRUE(memory_read32(&memory, 0x301cu, &value, error, sizeof(error)));
        EXPECT_U64_EQ(value, 0x12345678u);
    }

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 13, true, 0x3020u);
    cpu_write_register(&cpu, 14, true, 2u);
    EXPECT_TRUE(memory_write8(&memory, 0x3022u, 0xf0u, error, sizeof(error)));
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x38ee69acu, error, sizeof(error)), EMU_OK); /* ldrsb w12,[x13,x14] */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 12), 0xfffffff0u);
    memory_free(&memory);
}

static void test_bitfield_aliases(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);

    cpu_write_register(&cpu, 1, true, 0x123456789abcdef0ull);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xd3483c20u, error, sizeof(error)), EMU_OK); /* ubfx x0,x1,#8,#8 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0xdeu);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 17, false, 0xfffffff0u);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0x93407e30u, error, sizeof(error)), EMU_OK); /* sxtw x16,w17 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 16), 0xfffffffffffffff0ull);

    cpu.pc = EMU_LOAD_ADDRESS;
    cpu_write_register(&cpu, 4, true, 0xffffffffffffffffull);
    cpu_write_register(&cpu, 5, true, 0xabu);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xb3701ca4u, error, sizeof(error)), EMU_OK); /* bfi x4,x5,#16,#8 */
    EXPECT_U64_EQ(cpu_read_register(&cpu, 4), 0xffffffffffabffffull);
    memory_free(&memory);
}

static void test_decode_diagnostics_include_opcode(void) {
    Cpu cpu;
    Memory memory;
    char error[256] = {0};
    init_cpu_memory(&cpu, &memory);
    EXPECT_U64_EQ(step_opcode(&cpu, &memory, 0xffffffffu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001000");
    EXPECT_STR_CONTAINS(error, "opcode=0xffffffff");
    memory_free(&memory);
}

int main(void) {
    test_decode_new_groups();
    test_tbz_tbnz_execute();
    test_indirect_branches();
    test_logical_immediate_and_csel();
    test_extended_arithmetic_and_madd();
    test_load_store_new_forms();
    test_bitfield_aliases();
    test_decode_diagnostics_include_opcode();

    if (tests_failed != 0) {
        fprintf(stderr, "v1.12 tests failed: %d/%d assertions failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v1.12 tests passed: %d assertions\n", tests_run);
    return 0;
}

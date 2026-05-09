#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_TMP_DIR "tests/v0_1/tmp"

#define OP_NOP 0xd503201fu
#define OP_HLT 0xd4400000u
#define OP_UNSUPPORTED 0xffffffffu
#define OP_ZERO 0x00000000u

#define OP_MOVZ_X0_0 0xd2800000u
#define OP_MOVZ_X0_1 0xd2800020u
#define OP_MOVZ_X0_2 0xd2800040u
#define OP_MOVZ_X0_7 0xd28000e0u
#define OP_MOVZ_X0_10 0xd2800140u
#define OP_MOVZ_X0_42 0xd2800540u
#define OP_MOVZ_X0_1234 0xd2824680u
#define OP_MOVZ_X0_1234_LSL16 0xd2a24680u
#define OP_MOVZ_X0_FFFF 0xd29fffe0u
#define OP_MOVZ_X0_FFFF_LSL16 0xd2bfffe0u
#define OP_MOVZ_W0_1234 0x52824680u
#define OP_MOVZ_W0_1 0x52800020u
#define OP_MOVZ_X1_2 0xd2800041u
#define OP_MOVZ_X1_3 0xd2800061u
#define OP_MOVZ_X2_3 0xd2800062u
#define OP_MOVZ_X30_9 0xd280013eu
#define OP_MOVZ_XZR_123 0xd2800f7fu

#define OP_ADD_X0_XZR_5 0x910017e0u
#define OP_ADD_X0_X30_1 0x910007c0u
#define OP_ADD_X1_X0_0 0x91000001u
#define OP_ADD_X1_X0_3 0x91000c01u
#define OP_ADD_X2_X0_3 0x91000c02u
#define OP_ADD_X3_X0_9 0x91002403u
#define OP_ADD_X0_X0_0_LSL12 0x91400000u

#define OP_SUB_X1_X0_1 0xd1000401u
#define OP_SUB_X1_X0_3 0xd1000c01u
#define OP_SUB_X1_X0_4 0xd1001001u
#define OP_SUB_X4_X2_1 0xd1000444u

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
#define EXPECT_U32_EQ(actual, expected) EXPECT_U64_EQ((actual), (uint32_t)(expected))
#define EXPECT_U8_EQ(actual, expected) EXPECT_U64_EQ((actual), (uint8_t)(expected))
#define EXPECT_STATUS_EQ(actual, expected) EXPECT_U64_EQ((actual), (expected))
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

static void write_bytes_file(const char *path, const uint8_t *bytes, size_t byte_count) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (byte_count > 0 && fwrite(bytes, 1, byte_count, file) != byte_count) {
        fprintf(stderr, "failed to write %s\n", path);
        exit(1);
    }
    fclose(file);
}

static void write_repeated_file(const char *path, size_t byte_count, uint8_t value) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    for (size_t i = 0; i < byte_count; i++) {
        fputc(value, file);
    }
    fclose(file);
}

static void init_emulator_or_die(Emulator *emu) {
    char error[256];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        exit(1);
    }
}

static void load_program_into_emulator(Emulator *emu, const uint32_t *opcodes, size_t opcode_count) {
    char error[256];
    for (size_t i = 0; i < opcode_count; i++) {
        uint64_t address = EMU_LOAD_ADDRESS + (uint64_t)(i * 4u);
        if (!memory_write32(&emu->memory, address, opcodes[i], error, sizeof(error))) {
            fprintf(stderr, "memory_write32 failed: %s\n", error);
            exit(1);
        }
    }
}

static EmuStatus run_program(const uint32_t *opcodes, size_t opcode_count, Emulator *emu, char *error, size_t error_size) {
    init_emulator_or_die(emu);
    load_program_into_emulator(emu, opcodes, opcode_count);
    return emulator_run(emu, error, error_size);
}

static void test_cpu_initialization(void) {
    /* TC-CPU-001 through TC-CPU-004, plus register width and xzr behavior. */
    Cpu cpu;
    cpu_init(&cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);

    for (uint8_t i = 0; i < 31; i++) {
        EXPECT_U64_EQ(cpu.x[i], 0);
    }
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(cpu.sp, EMU_MEMORY_SIZE);
    EXPECT_FALSE(cpu.flags.n);
    EXPECT_FALSE(cpu.flags.z);
    EXPECT_FALSE(cpu.flags.c);
    EXPECT_FALSE(cpu.flags.v);
    EXPECT_FALSE(cpu.halted);
    EXPECT_U64_EQ(cpu.instructions_executed, 0);

    cpu_write_register(&cpu, 0, true, 0x1122334455667788ull);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x1122334455667788ull);
    cpu_write_register(&cpu, 0, false, 0xffffffffffffffffull);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0x00000000ffffffffull);
    cpu_write_register(&cpu, 31, true, 123);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 31), 0);
}

static void test_memory(void) {
    /* TC-MEM-001 through TC-MEM-008. */
    char error[256];
    Memory memory;
    uint8_t byte = 0;
    uint32_t word = 0;
    uint64_t doubleword = 0;

    reset_error(error, sizeof(error));
    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));

    EXPECT_TRUE(memory_read8(&memory, 0, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x00);
    EXPECT_TRUE(memory_read8(&memory, EMU_MEMORY_SIZE / 2u, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x00);
    EXPECT_TRUE(memory_read8(&memory, EMU_MEMORY_SIZE - 1u, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x00);

    EXPECT_TRUE(memory_write8(&memory, 0x2000, 0xab, error, sizeof(error)));
    EXPECT_TRUE(memory_read8(&memory, 0x2000, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0xab);

    EXPECT_TRUE(memory_write32(&memory, 0x2000, 0x12345678u, error, sizeof(error)));
    EXPECT_TRUE(memory_read8(&memory, 0x2000, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x78);
    EXPECT_TRUE(memory_read8(&memory, 0x2001, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x56);
    EXPECT_TRUE(memory_read8(&memory, 0x2002, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x34);
    EXPECT_TRUE(memory_read8(&memory, 0x2003, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0x12);
    EXPECT_TRUE(memory_read32(&memory, 0x2000, &word, error, sizeof(error)));
    EXPECT_U32_EQ(word, 0x12345678u);

    EXPECT_TRUE(memory_write64(&memory, 0x3000, 0x1122334455667788ull, error, sizeof(error)));
    EXPECT_TRUE(memory_read64(&memory, 0x3000, &doubleword, error, sizeof(error)));
    EXPECT_U64_EQ(doubleword, 0x1122334455667788ull);

    EXPECT_TRUE(memory_write8(&memory, EMU_MEMORY_SIZE - 1u, 0xcd, error, sizeof(error)));
    EXPECT_TRUE(memory_read8(&memory, EMU_MEMORY_SIZE - 1u, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0xcd);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(memory_read8(&memory, EMU_MEMORY_SIZE, &byte, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "address=0x0000000000100000");

    reset_error(error, sizeof(error));
    EXPECT_FALSE(memory_read32(&memory, EMU_MEMORY_SIZE - 2u, &word, error, sizeof(error)));

    EXPECT_TRUE(memory_write8(&memory, EMU_MEMORY_SIZE - 4u, 0xee, error, sizeof(error)));
    reset_error(error, sizeof(error));
    EXPECT_FALSE(memory_write64(&memory, EMU_MEMORY_SIZE - 4u, 0, error, sizeof(error)));
    EXPECT_TRUE(memory_read8(&memory, EMU_MEMORY_SIZE - 4u, &byte, error, sizeof(error)));
    EXPECT_U8_EQ(byte, 0xee);

    memory_free(&memory);
}

static void test_loader(void) {
    /* TC-LOAD-001 through TC-LOAD-007. */
    char error[512];
    Memory memory;
    uint32_t word = 0;
    const char *valid_path = TEST_TMP_DIR "/nop_hlt.bin";
    const char *empty_path = TEST_TMP_DIR "/empty.bin";
    const char *huge_path = TEST_TMP_DIR "/too_large.bin";
    const char *exact_path = TEST_TMP_DIR "/exact_fill.bin";
    const char *five_path = TEST_TMP_DIR "/five_bytes.bin";
    const uint32_t nop_hlt[] = {OP_NOP, OP_HLT};
    const uint8_t five_bytes[] = {0x00, 0x00, 0x40, 0xd4, 0xff};

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));

    write_program_file(valid_path, nop_hlt, 2);
    reset_error(error, sizeof(error));
    EXPECT_TRUE(load_raw_binary(&memory, valid_path, EMU_LOAD_ADDRESS, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_LOAD_ADDRESS, &word, error, sizeof(error)));
    EXPECT_U32_EQ(word, OP_NOP);

    write_bytes_file(empty_path, NULL, 0);
    reset_error(error, sizeof(error));
    EXPECT_FALSE(load_raw_binary(&memory, empty_path, EMU_LOAD_ADDRESS, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "empty");

    reset_error(error, sizeof(error));
    EXPECT_FALSE(load_raw_binary(&memory, TEST_TMP_DIR "/missing.bin", EMU_LOAD_ADDRESS, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "missing.bin");

    write_repeated_file(huge_path, EMU_MEMORY_SIZE - EMU_LOAD_ADDRESS + 1u, 0xaa);
    reset_error(error, sizeof(error));
    EXPECT_FALSE(load_raw_binary(&memory, huge_path, EMU_LOAD_ADDRESS, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "file size");
    EXPECT_STR_CONTAINS(error, "available");

    write_repeated_file(exact_path, EMU_MEMORY_SIZE - EMU_LOAD_ADDRESS, 0xbb);
    reset_error(error, sizeof(error));
    EXPECT_TRUE(load_raw_binary(&memory, exact_path, EMU_LOAD_ADDRESS, error, sizeof(error)));

    write_bytes_file(five_path, five_bytes, sizeof(five_bytes));
    reset_error(error, sizeof(error));
    EXPECT_TRUE(load_raw_binary(&memory, five_path, EMU_LOAD_ADDRESS, error, sizeof(error)));

    memory_free(&memory);
}

static void test_fetch_and_decode(void) {
    /* TC-FETCH-001 through TC-FETCH-004 and TC-DEC-001 through TC-DEC-007. */
    char error[256];
    Memory memory;
    Cpu cpu;
    uint32_t opcode = 0;
    EmuDecodedInstruction inst;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    cpu_init(&cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);

    EXPECT_TRUE(memory_write32(&memory, EMU_LOAD_ADDRESS, OP_NOP, error, sizeof(error)));
    EXPECT_TRUE(cpu_fetch(&cpu, &memory, &opcode, error, sizeof(error)));
    EXPECT_U32_EQ(opcode, OP_NOP);

    memory.bytes[EMU_LOAD_ADDRESS] = 0x1f;
    memory.bytes[EMU_LOAD_ADDRESS + 1u] = 0x20;
    memory.bytes[EMU_LOAD_ADDRESS + 2u] = 0x03;
    memory.bytes[EMU_LOAD_ADDRESS + 3u] = 0xd5;
    EXPECT_TRUE(cpu_fetch(&cpu, &memory, &opcode, error, sizeof(error)));
    EXPECT_U32_EQ(opcode, OP_NOP);

    cpu.pc = EMU_MEMORY_SIZE - 4u;
    EXPECT_TRUE(memory_write32(&memory, cpu.pc, OP_HLT, error, sizeof(error)));
    EXPECT_TRUE(cpu_fetch(&cpu, &memory, &opcode, error, sizeof(error)));
    EXPECT_U32_EQ(opcode, OP_HLT);

    cpu.pc = EMU_MEMORY_SIZE - 2u;
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_fetch(&cpu, &memory, &opcode, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "pc=0x00000000000ffffe");

    cpu.pc = EMU_LOAD_ADDRESS + 2u;
    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_fetch(&cpu, &memory, &opcode, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "misaligned pc");

    EXPECT_TRUE(cpu_decode(OP_NOP, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_NOP);

    EXPECT_TRUE(cpu_decode(OP_HLT, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_HLT);
    EXPECT_U64_EQ(inst.imm, 0);

    EXPECT_TRUE(cpu_decode(OP_MOVZ_X0_1234, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_MOVZ);
    EXPECT_TRUE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 0);
    EXPECT_U64_EQ(inst.imm, 0x1234);

    EXPECT_TRUE(cpu_decode(OP_MOVZ_W0_1234, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_MOVZ);
    EXPECT_FALSE(inst.is_64_bit);
    EXPECT_U64_EQ(inst.rd, 0);
    EXPECT_U64_EQ(inst.imm, 0x1234);

    EXPECT_TRUE(cpu_decode(OP_ADD_X2_X0_3, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_ADD_IMM);
    EXPECT_U64_EQ(inst.rd, 2);
    EXPECT_U64_EQ(inst.rn, 0);
    EXPECT_U64_EQ(inst.imm, 3);

    EXPECT_TRUE(cpu_decode(OP_SUB_X1_X0_3, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_SUB_IMM);
    EXPECT_U64_EQ(inst.rd, 1);
    EXPECT_U64_EQ(inst.rn, 0);
    EXPECT_U64_EQ(inst.imm, 3);

    EXPECT_TRUE(cpu_decode(OP_ADD_X0_X0_0_LSL12, &inst, error, sizeof(error)));
    EXPECT_STATUS_EQ(inst.kind, EMU_INST_ADD_IMM);
    EXPECT_U64_EQ(inst.imm, 0);

    reset_error(error, sizeof(error));
    EXPECT_FALSE(cpu_decode(OP_UNSUPPORTED, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "0xffffffff");

    memory_free(&memory);
}

static void test_execution_programs(void) {
    /* TC-EXEC-001 through TC-EXEC-014 and TC-ERR-001 through TC-ERR-003. */
    char error[512];
    Emulator emu;

    const uint32_t nop_hlt[] = {OP_NOP, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(nop_hlt, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 2);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    emulator_free(&emu);

    const uint32_t hlt_only[] = {OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(hlt_only, 1, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.cpu.halted);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1);
    emulator_free(&emu);

    const uint32_t after_hlt[] = {OP_HLT, OP_MOVZ_X0_42};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(after_hlt, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    emulator_free(&emu);

    const uint32_t movz_basic[] = {OP_MOVZ_X0_1234, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(movz_basic, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x1234);
    emulator_free(&emu);

    const uint32_t movz_zero[] = {OP_MOVZ_X0_0, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(movz_zero, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    emulator_free(&emu);

    const uint32_t movz_max_imm16[] = {OP_MOVZ_X0_FFFF, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(movz_max_imm16, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0xffff);
    emulator_free(&emu);

    const uint32_t movz_shift[] = {OP_MOVZ_X0_1234_LSL16, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(movz_shift, 2, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 0x12340000ull);
    emulator_free(&emu);

    const uint32_t w_zero_ext[] = {OP_MOVZ_X0_FFFF_LSL16, OP_MOVZ_W0_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(w_zero_ext, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    emulator_free(&emu);

    const uint32_t add_basic[] = {OP_MOVZ_X0_2, OP_ADD_X1_X0_3, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(add_basic, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 2);
    EXPECT_U64_EQ(emu.cpu.x[1], 5);
    emulator_free(&emu);

    const uint32_t add_zero[] = {OP_MOVZ_X0_7, OP_ADD_X1_X0_0, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(add_zero, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 7);
    emulator_free(&emu);

    const uint32_t sub_basic[] = {OP_MOVZ_X0_7, OP_SUB_X1_X0_3, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(sub_basic, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 4);
    emulator_free(&emu);

    const uint32_t sub_underflow[] = {OP_MOVZ_X0_0, OP_SUB_X1_X0_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(sub_underflow, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 0xffffffffffffffffull);
    emulator_free(&emu);

    const uint32_t xzr[] = {OP_MOVZ_XZR_123, OP_ADD_X0_XZR_5, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(xzr, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 5);
    emulator_free(&emu);

    const uint32_t x30[] = {OP_MOVZ_X30_9, OP_ADD_X0_X30_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(x30, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[30], 9);
    EXPECT_U64_EQ(emu.cpu.x[0], 10);
    emulator_free(&emu);

    const uint32_t sequential[] = {OP_MOVZ_X0_1, OP_MOVZ_X1_2, OP_ADD_X2_X0_3, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(sequential, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 12u);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    EXPECT_U64_EQ(emu.cpu.x[1], 2);
    EXPECT_U64_EQ(emu.cpu.x[2], 4);
    emulator_free(&emu);

    const uint32_t unsupported_first[] = {OP_UNSUPPORTED, OP_MOVZ_X0_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(unsupported_first, 3, &emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_U64_EQ(emu.cpu.x[0], 0);
    EXPECT_STR_CONTAINS(error, "0xffffffff");
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001000");
    emulator_free(&emu);

    const uint32_t unsupported_later[] = {OP_MOVZ_X0_1, OP_UNSUPPORTED, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(unsupported_later, 3, &emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    EXPECT_STR_CONTAINS(error, "0xffffffff");
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001004");
    emulator_free(&emu);

    const uint32_t no_hlt[] = {OP_MOVZ_X0_1};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(no_hlt, 1, &emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "0x00000000");
    emulator_free(&emu);

    const uint32_t nop_limit[] = {OP_NOP, OP_NOP, OP_HLT};
    init_emulator_or_die(&emu);
    load_program_into_emulator(&emu, nop_limit, 3);
    emu.instruction_limit = 2;
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    emulator_free(&emu);
}

static void test_acceptance_programs(void) {
    /* TC-ACC-001 through TC-ACC-004. */
    char error[512];
    Emulator emu;

    const uint32_t add_demo[] = {OP_MOVZ_X0_2, OP_MOVZ_X1_3, OP_ADD_X2_X0_3, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(add_demo, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 2);
    EXPECT_U64_EQ(emu.cpu.x[1], 3);
    EXPECT_U64_EQ(emu.cpu.x[2], 5);
    emulator_free(&emu);

    const uint32_t subtract[] = {OP_MOVZ_X0_10, OP_SUB_X1_X0_4, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(subtract, 3, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[1], 6);
    emulator_free(&emu);

    const uint32_t multi_reg[] = {OP_MOVZ_X0_1, OP_MOVZ_X1_2, OP_MOVZ_X2_3, OP_ADD_X3_X0_9, OP_SUB_X4_X2_1, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(multi_reg, 6, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 1);
    EXPECT_U64_EQ(emu.cpu.x[1], 2);
    EXPECT_U64_EQ(emu.cpu.x[2], 3);
    EXPECT_U64_EQ(emu.cpu.x[3], 10);
    EXPECT_U64_EQ(emu.cpu.x[4], 2);
    emulator_free(&emu);

    const uint32_t leading_nops[] = {OP_NOP, OP_NOP, OP_MOVZ_X0_42, OP_HLT};
    reset_error(error, sizeof(error));
    EXPECT_STATUS_EQ(run_program(leading_nops, 4, &emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.x[0], 42);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 4);
    emulator_free(&emu);
}

int main(void) {
    ensure_tmp_dir();
    test_cpu_initialization();
    test_memory();
    test_loader();
    test_fetch_and_decode();
    test_execution_programs();
    test_acceptance_programs();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.1 unit/integration tests failed: %d failure(s), %d assertion(s)\n", tests_failed, tests_run);
        return 1;
    }

    printf("v0.1 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

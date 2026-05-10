#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_SVC_0 0xd4000001u
#define OP_SVC_1 0xd4000021u
#define OP_SVC_FFFF 0xd41fffe1u
#define OP_HLT_0 0xd4400000u
#define OP_NOP 0xd503201fu
#define OP_INVALID_SVC_LIKE 0xd4000000u

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
#define EXPECT_MEM_EQ(actual, expected, length) do { \
    tests_run++; \
    if (memcmp((actual), (expected), (length)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: memory differs for %s\n", __FILE__, __LINE__, #actual); \
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
#define EXPECT_STR_NOT_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) != NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string not to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t encode_movz(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    uint32_t sf = is_64_bit ? 0x80000000u : 0u;
    uint32_t hw = (uint32_t)(shift / 16u) & 0x3u;
    return sf | 0x52800000u | (hw << 21u) | ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_add_imm(uint8_t rd, uint8_t rn, uint16_t imm12, bool is_64_bit) {
    return (is_64_bit ? 0x91000000u : 0x11000000u) | ((uint32_t)(imm12 & 0xfffu) << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_str_unsigned(uint8_t rt, uint8_t rn, uint16_t imm12, bool is_64_bit) {
    uint32_t size_bits = is_64_bit ? 0xf9000000u : 0xb9000000u;
    return size_bits | ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) |
           (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_bl(int64_t offset) {
    return 0x94000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
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

static void put_bytes(Emulator *emu, uint64_t address, const unsigned char *bytes, size_t length) {
    char error[256];
    for (size_t i = 0; i < length; i++) {
        if (!memory_write8(&emu->memory, address + (uint64_t)i, bytes[i], error, sizeof(error))) {
            fprintf(stderr, "memory_write8 failed: %s\n", error);
            exit(1);
        }
    }
}

static FILE *tmpfile_or_die(void) {
    FILE *file = tmpfile();
    if (file == NULL) {
        perror("tmpfile");
        exit(1);
    }
    return file;
}

static size_t read_stream(FILE *file, unsigned char *out, size_t capacity) {
    fflush(file);
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    return fread(out, 1, capacity, file);
}

static void test_svc_decode_and_formatting(void) {
    /* TC-V07-DECODE-001 through TC-V07-DECODE-005. */
    char error[256];
    char out[256];
    char tiny[8];
    EmuDecodedInstruction instruction;

    EXPECT_TRUE(cpu_decode(OP_SVC_0, &instruction, error, sizeof(error)));
    EXPECT_U64_EQ(instruction.kind, EMU_INST_SVC);
    EXPECT_U64_EQ(instruction.imm, 0);

    EXPECT_TRUE(cpu_decode(OP_SVC_1, &instruction, error, sizeof(error)));
    EXPECT_U64_EQ(instruction.kind, EMU_INST_SVC);
    EXPECT_U64_EQ(instruction.imm, 1);
    EXPECT_TRUE(cpu_decode(OP_SVC_FFFF, &instruction, error, sizeof(error)));
    EXPECT_U64_EQ(instruction.imm, 0xffffu);

    memset(error, 0, sizeof(error));
    EXPECT_FALSE(cpu_decode(OP_INVALID_SVC_LIKE, &instruction, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported instruction");

    memset(out, 0, sizeof(out));
    EXPECT_TRUE(cpu_format_instruction(OP_SVC_0, EMU_LOAD_ADDRESS, out, sizeof(out)));
    EXPECT_STR_CONTAINS(out, "0x0000000000001000");
    EXPECT_STR_CONTAINS(out, "0xd4000001");
    EXPECT_STR_CONTAINS(out, "svc #0x0");

    memset(out, 0, sizeof(out));
    EXPECT_TRUE(cpu_format_instruction(OP_SVC_FFFF, EMU_LOAD_ADDRESS, out, sizeof(out)));
    EXPECT_STR_CONTAINS(out, "svc #0xffff");

    memset(tiny, 0xaa, sizeof(tiny));
    EXPECT_FALSE(cpu_format_instruction(OP_SVC_0, EMU_LOAD_ADDRESS, tiny, sizeof(tiny)));
    EXPECT_U64_EQ((unsigned char)tiny[sizeof(tiny) - 1u], 0u);
}

static void test_exit_syscall_behavior(void) {
    /* TC-V07-ABI-001 and TC-V07-EXIT-001 through TC-V07-EXIT-006. */
    char error[512];
    Emulator emu;
    const uint32_t exit_7[] = {
        encode_movz(8, EMU_SYSCALL_EXIT, 0, true),
        encode_movz(0, 7, 0, true),
        OP_SVC_0,
        encode_movz(0, 99, 0, true),
        OP_HLT_0,
    };

    init_emulator_or_die(&emu);
    load_program(&emu, exit_7, sizeof(exit_7) / sizeof(exit_7[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 7u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 7u);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 12u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 3u);
    EXPECT_STATUS(emulator_step(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 3u);
    emulator_free(&emu);

    const uint32_t exit_large[] = {
        encode_movz(0, 0x1234, 0, true),
        encode_movz(8, EMU_SYSCALL_EXIT, 0, true),
        OP_SVC_0,
    };
    init_emulator_or_die(&emu);
    load_program(&emu, exit_large, sizeof(exit_large) / sizeof(exit_large[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 0x34u);
    emulator_free(&emu);

    const uint32_t hlt_only[] = {OP_NOP, OP_HLT_0, encode_movz(0, 99, 0, true)};
    init_emulator_or_die(&emu);
    load_program(&emu, hlt_only, sizeof(hlt_only) / sizeof(hlt_only[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_FALSE(emu.guest_exited);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    emulator_free(&emu);

    const uint32_t branch_to_exit[] = {
        encode_bl(8),
        encode_movz(0, 99, 0, true),
        encode_movz(0, 42, 0, true),
        encode_movz(8, EMU_SYSCALL_EXIT, 0, true),
        OP_SVC_0,
        OP_HLT_0,
    };
    init_emulator_or_die(&emu);
    load_program(&emu, branch_to_exit, sizeof(branch_to_exit) / sizeof(branch_to_exit[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(emu.guest_exit_code, 42u);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 20u);
    emulator_free(&emu);
}

static void test_write_syscall_outputs_and_abi(void) {
    /* TC-V07-ABI-002 through TC-V07-ABI-005 and TC-V07-WRITE-001 through TC-V07-WRITE-010. */
    char error[512];
    unsigned char buffer[8192];
    Emulator emu;
    FILE *out = tmpfile_or_die();
    FILE *err = tmpfile_or_die();
    const unsigned char abc[] = {'A', 'B', 'C'};

    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    emu.stderr_stream = err;
    put_bytes(&emu, 0x2000, abc, sizeof(abc));
    const uint32_t write_abc[] = {
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x2000, 0, true),
        encode_movz(2, 3, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        encode_add_imm(3, 0, 4, true),
        OP_HLT_0,
    };
    load_program(&emu, write_abc, sizeof(write_abc) / sizeof(write_abc[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 3u);
    EXPECT_MEM_EQ(buffer, abc, sizeof(abc));
    EXPECT_U64_EQ(read_stream(err, buffer, sizeof(buffer)), 0u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 3u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x2000u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 3u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 3), 7u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 8), EMU_SYSCALL_WRITE);
    emulator_free(&emu);
    fclose(out);
    fclose(err);

    out = tmpfile_or_die();
    err = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    emu.stderr_stream = err;
    const unsigned char binary[] = {'A', '\0', 'B', '\n'};
    put_bytes(&emu, 0x2100, binary, sizeof(binary));
    const uint32_t write_stderr_binary[] = {
        encode_movz(0, 2, 0, true),
        encode_movz(1, 0x2100, 0, true),
        encode_movz(2, 4, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        OP_HLT_0,
    };
    load_program(&emu, write_stderr_binary, sizeof(write_stderr_binary) / sizeof(write_stderr_binary[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    EXPECT_U64_EQ(read_stream(err, buffer, sizeof(buffer)), 4u);
    EXPECT_MEM_EQ(buffer, binary, sizeof(binary));
    emulator_free(&emu);
    fclose(out);
    fclose(err);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    const uint32_t zero_len[] = {
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x2200, 0, true),
        encode_movz(2, 0, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        OP_HLT_0,
    };
    load_program(&emu, zero_len, sizeof(zero_len) / sizeof(zero_len[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 0u);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    const unsigned char hello[] = "hello world\n";
    put_bytes(&emu, 0x2300, (const unsigned char *)"hello", 5);
    put_bytes(&emu, 0x2310, (const unsigned char *)" ", 1);
    put_bytes(&emu, 0x2320, (const unsigned char *)"world\n", 6);
    const uint32_t multi_write[] = {
        encode_movz(0, 1, 0, true), encode_movz(1, 0x2300, 0, true), encode_movz(2, 5, 0, true), encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0,
        encode_movz(0, 1, 0, true), encode_movz(1, 0x2310, 0, true), encode_movz(2, 1, 0, true), encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0,
        encode_movz(0, 1, 0, true), encode_movz(1, 0x2320, 0, true), encode_movz(2, 6, 0, true), encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0,
        OP_HLT_0,
    };
    load_program(&emu, multi_write, sizeof(multi_write) / sizeof(multi_write[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), sizeof(hello) - 1u);
    EXPECT_MEM_EQ(buffer, hello, sizeof(hello) - 1u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 6u);
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    const unsigned char z = 'Z';
    put_bytes(&emu, 0, &z, 1);
    const uint32_t addr_zero[] = {
        encode_movz(0, 1, 0, true), encode_movz(1, 0, 0, true), encode_movz(2, 1, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0, OP_HLT_0,
    };
    load_program(&emu, addr_zero, sizeof(addr_zero) / sizeof(addr_zero[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 1u);
    EXPECT_MEM_EQ(buffer, &z, 1u);
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    const unsigned char end_bytes[] = {'E', 'N', 'D', '!'};
    put_bytes(&emu, EMU_MEMORY_SIZE - 4u, end_bytes, sizeof(end_bytes));
    cpu_write_register(&emu.cpu, 0, true, 1);
    cpu_write_register(&emu.cpu, 1, true, EMU_MEMORY_SIZE - 4u);
    cpu_write_register(&emu.cpu, 2, true, sizeof(end_bytes));
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0, OP_HLT_0}, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), sizeof(end_bytes));
    EXPECT_MEM_EQ(buffer, end_bytes, sizeof(end_bytes));
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    for (size_t i = 0; i < 4096; i++) {
        buffer[i] = (unsigned char)('a' + (i % 26u));
    }
    put_bytes(&emu, 0x3000, buffer, 4096);
    cpu_write_register(&emu.cpu, 0, true, 1);
    cpu_write_register(&emu.cpu, 1, true, 0x3000);
    cpu_write_register(&emu.cpu, 2, true, 4096);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0, OP_HLT_0}, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    unsigned char actual[4096];
    EXPECT_U64_EQ(read_stream(out, actual, sizeof(actual)), 4096u);
    EXPECT_MEM_EQ(actual, buffer, 4096u);
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    const uint32_t stack_program[] = {
        encode_movz(0, 0x4142, 0, true),
        encode_str_unsigned(0, 31, 0, true),
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x8000, 0, true),
        encode_movz(2, 2, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        OP_HLT_0,
    };
    load_program(&emu, stack_program, sizeof(stack_program) / sizeof(stack_program[0]));
    emu.cpu.sp = 0x8000u;
    /* The toy CPU stores 64-bit little-endian; this checks syscalls see modified stack memory. */
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 2u);
    EXPECT_U64_EQ(buffer[0], 0x42u);
    EXPECT_U64_EQ(buffer[1], 0x41u);
    emulator_free(&emu);
    fclose(out);
}

static void test_syscall_errors_and_edges(void) {
    /* TC-V07-ERR-001 through TC-V07-ERR-010. */
    char error[512];
    unsigned char buffer[32];
    Emulator emu;
    FILE *out = tmpfile_or_die();
    const unsigned char byte = '!';

    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    put_bytes(&emu, 0x2400, &byte, 1);
    const uint32_t bad_fd[] = {
        encode_movz(0, 3, 0, true), encode_movz(1, 0x2400, 0, true), encode_movz(2, 1, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0, OP_HLT_0,
    };
    load_program(&emu, bad_fd, sizeof(bad_fd) / sizeof(bad_fd[0]));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), (uint64_t)EMU_SYSCALL_EBADF);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    emulator_free(&emu);
    fclose(out);

    init_emulator_or_die(&emu);
    cpu_write_register(&emu.cpu, 0, true, UINT64_MAX);
    cpu_write_register(&emu.cpu, 1, true, 0x2400);
    cpu_write_register(&emu.cpu, 2, true, 1);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0, OP_HLT_0}, 2);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), (uint64_t)EMU_SYSCALL_EBADF);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    cpu_write_register(&emu.cpu, 8, true, 999);
    load_program(&emu, (const uint32_t[]){OP_SVC_0, OP_HLT_0}, 2);
    EXPECT_STATUS(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), (uint64_t)EMU_SYSCALL_ENOSYS);
    EXPECT_FALSE(emu.guest_exited);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_EXIT);
    load_program(&emu, (const uint32_t[]){OP_SVC_1}, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_step(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "execution error at pc=0x0000000000001000");
    EXPECT_STR_CONTAINS(error, "opcode=0xd4000021");
    EXPECT_STR_CONTAINS(error, "unsupported svc immediate: #0x1");
    emulator_free(&emu);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    cpu_write_register(&emu.cpu, 0, true, 1);
    cpu_write_register(&emu.cpu, 1, true, EMU_MEMORY_SIZE + 1u);
    cpu_write_register(&emu.cpu, 2, true, 1);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0}, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "syscall write buffer out of bounds");
    EXPECT_STR_CONTAINS(error, "address=0x0000000000100001");
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001000");
    EXPECT_STR_CONTAINS(error, "opcode=0xd4000001");
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    emulator_free(&emu);
    fclose(out);

    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    cpu_write_register(&emu.cpu, 0, true, 1);
    cpu_write_register(&emu.cpu, 1, true, EMU_MEMORY_SIZE - 1u);
    cpu_write_register(&emu.cpu, 2, true, 2);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0}, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "length=0x0000000000000002");
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    emulator_free(&emu);
    fclose(out);

    init_emulator_or_die(&emu);
    cpu_write_register(&emu.cpu, 0, true, 1);
    cpu_write_register(&emu.cpu, 1, true, UINT64_MAX - 1u);
    cpu_write_register(&emu.cpu, 2, true, 4);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_WRITE);
    load_program(&emu, (const uint32_t[]){OP_SVC_0}, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "syscall write buffer out of bounds");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    emu.instruction_limit = 3;
    load_program(&emu, (const uint32_t[]){encode_b(0)}, 1);
    memset(error, 0, sizeof(error));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit reached");
    EXPECT_FALSE(emu.guest_exited);
    emulator_free(&emu);
}

static void test_debugger_syscall_behavior(void) {
    /* TC-V07-DEBUG-001 through TC-V07-DEBUG-006 core API behavior. */
    char error[512];
    unsigned char buffer[32];
    const unsigned char msg[] = "DBG";
    const char *path = "tests/v0_7/tmp/debug_syscall.bin";
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        exit(1);
    }
    const uint32_t program[] = {
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x1024, 0, true),
        encode_movz(2, 3, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        encode_movz(9, 5, 0, true),
        encode_movz(0, 0, 0, true),
        encode_movz(8, EMU_SYSCALL_EXIT, 0, true),
        OP_SVC_0,
    };
    for (size_t i = 0; i < sizeof(program) / sizeof(program[0]); i++) {
        uint32_t op = program[i];
        unsigned char bytes[] = {(unsigned char)(op & 0xffu), (unsigned char)((op >> 8u) & 0xffu),
                                 (unsigned char)((op >> 16u) & 0xffu), (unsigned char)((op >> 24u) & 0xffu)};
        if (fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
            perror("fwrite");
            exit(1);
        }
    }
    if (fwrite(msg, 1, sizeof(msg) - 1u, file) != sizeof(msg) - 1u) {
        perror("fwrite");
        exit(1);
    }
    fclose(file);

    Debugger debugger;
    FILE *out = tmpfile_or_die();
    EXPECT_TRUE(debugger_init(&debugger, path, error, sizeof(error)));
    debugger.emu.stdout_stream = out;
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 16u, error, sizeof(error)));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(debugger.stopped_at_breakpoint);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 0u);
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(read_stream(out, buffer, sizeof(buffer)), 3u);
    EXPECT_MEM_EQ(buffer, msg, sizeof(msg) - 1u);
    EXPECT_U64_EQ(cpu_read_register(&debugger.emu.cpu, 0), 3u);
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, EMU_LOAD_ADDRESS + 20u, error, sizeof(error)));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(debugger.stopped_at_breakpoint);
    EXPECT_U64_EQ(cpu_read_register(&debugger.emu.cpu, 0), 3u);
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(debugger.emu.guest_exited);
    EXPECT_TRUE(debugger_reset(&debugger, error, sizeof(error)));
    debugger.emu.stdout_stream = out;
    EXPECT_FALSE(debugger.emu.guest_exited);
    debugger_free(&debugger);
    fclose(out);
}

int main(void) {
    test_svc_decode_and_formatting();
    test_exit_syscall_behavior();
    test_write_syscall_outputs_and_abi();
    test_syscall_errors_and_edges();
    test_debugger_syscall_behavior();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.7 unit/integration tests failed: %d failure(s) across %d assertion(s)\n", tests_failed,
                tests_run);
        return 1;
    }

    printf("v0.7 unit/integration tests passed: %d assertion(s)\n", tests_run);
    return 0;
}

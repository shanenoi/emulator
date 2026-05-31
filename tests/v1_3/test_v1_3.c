#include "emulator.h"

#include <inttypes.h>
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
#define EXPECT_SIZE_EQ(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t op_hlt(void) { return 0xd4400000u; }
static uint32_t op_movz_w(unsigned rd, unsigned imm) { return 0x52800000u | ((imm & 0xffffu) << 5u) | (rd & 31u); }
static uint32_t op_movz_x(unsigned rd, unsigned imm, unsigned shift) {
    return 0xd2800000u | (((shift / 16u) & 3u) << 21u) | ((imm & 0xffffu) << 5u) | (rd & 31u);
}
static uint32_t op_movk_x(unsigned rd, unsigned imm, unsigned shift) {
    return 0xf2800000u | (((shift / 16u) & 3u) << 21u) | ((imm & 0xffffu) << 5u) | (rd & 31u);
}
static uint32_t op_ldr_w(unsigned rt, unsigned rn, unsigned imm) {
    return 0xb9400000u | (((imm / 4u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_ldr_x(unsigned rt, unsigned rn, unsigned imm) {
    return 0xf9400000u | (((imm / 8u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_strb(unsigned rt, unsigned rn, unsigned imm) {
    return 0x39000000u | ((imm & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_strh(unsigned rt, unsigned rn, unsigned imm) {
    return 0x79000000u | (((imm / 2u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_str_x(unsigned rt, unsigned rn, unsigned imm) {
    return 0xf9000000u | (((imm / 8u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_stp_x(unsigned rt, unsigned rt2, unsigned rn) {
    return 0xa9000000u | ((rt2 & 31u) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_ldr_pre_w(unsigned rt, unsigned rn, int offset) {
    return 0xb8400c00u | (((uint32_t)offset & 0x1ffu) << 12u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_str_post_b(unsigned rt, unsigned rn, int offset) {
    return 0x38000400u | (((uint32_t)offset & 0x1ffu) << 12u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_b(int offset_bytes) {
    return 0x14000000u | (((uint32_t)(offset_bytes / 4)) & 0x03ffffffu);
}

static void write_word(Memory *memory, uint64_t address, uint32_t word) {
    memory->bytes[address + 0u] = (uint8_t)(word & 0xffu);
    memory->bytes[address + 1u] = (uint8_t)((word >> 8u) & 0xffu);
    memory->bytes[address + 2u] = (uint8_t)((word >> 16u) & 0xffu);
    memory->bytes[address + 3u] = (uint8_t)((word >> 24u) & 0xffu);
}

static void init_memory(Memory *memory) {
    char error[512];
    EXPECT_TRUE(memory_init(memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    memory_clear_mappings(memory);
}

static size_t read_tmp(FILE *stream, unsigned char *buffer, size_t cap) {
    fflush(stream);
    fseek(stream, 0, SEEK_SET);
    return fread(buffer, 1, cap, stream);
}

static void test_device_map_and_routing(void) {
    /* TC-V13-BUS-001 through TC-V13-BUS-008, TC-V13-EDGE-009/010, and map shape checks. */
    Memory memory;
    char error[512];
    uint32_t word = 0;
    uint8_t byte = 0;
    EmuMemoryFaultKind fault = EMU_MEMORY_FAULT_NONE;

    init_memory(&memory);
    EXPECT_SIZE_EQ(memory.devices.range_count, 5u);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_UART_BASE) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_UART_BASE + EMU_DEVICE_SIZE - 1u) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_UART_BASE + EMU_DEVICE_SIZE) == NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_TIMER_BASE) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_RANDOM_BASE) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_KEYBOARD_BASE) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, EMU_DEVICE_TERMINAL_BASE) != NULL);
    EXPECT_TRUE(memory_find_device(&memory, 0x2000u) == NULL);

    EXPECT_TRUE(memory_map_range(&memory, 0x2000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "data", error, sizeof(error)));
    EXPECT_TRUE(memory_write32(&memory, 0x2000u, 0x11223344u, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, 0x2000u, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 0x11223344u);

    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word & 1u, 1u);
    EXPECT_FALSE(memory_read8(&memory, EMU_DEVICE_UART_BASE + EMU_DEVICE_SIZE - 1u, &byte, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid read register");
    EXPECT_FALSE(memory_read8(&memory, EMU_DEVICE_UART_BASE + EMU_DEVICE_SIZE, &byte, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_FALSE(memory_read32(&memory, UINT64_MAX - 1u, &word, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_TRUE(memory_check_access(&memory, 0x2000u, 0u, EMU_MAP_READ, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_NONE);
    EXPECT_FALSE(memory_check_execute(&memory, EMU_DEVICE_UART_BASE, 4u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "reserved non-executable device range");
    EXPECT_STR_CONTAINS(error, "device=uart");
    memory_free(&memory);
}

static void test_uart_device_policy(void) {
    /* TC-V13-UART-001/005/006/008/009/010/011 and TC-V13-FAULT-001/002/003/004. */
    Memory memory;
    char error[512];
    uint32_t status = 0;
    uint64_t wide = 0;
    uint8_t byte = 0;
    FILE *out = tmpfile();
    unsigned char buffer[8] = {0};

    EXPECT_TRUE(out != NULL);
    init_memory(&memory);
    memory_set_uart_output(&memory, out);

    EXPECT_TRUE(memory_write8(&memory, EMU_DEVICE_UART_BASE, 'H', error, sizeof(error)));
    EXPECT_TRUE(memory_write8(&memory, EMU_DEVICE_UART_BASE, 0x00u, error, sizeof(error)));
    EXPECT_TRUE(memory_write8(&memory, EMU_DEVICE_UART_BASE, 0xffu, error, sizeof(error)));
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 3u);
    EXPECT_U64_EQ(buffer[0], 'H');
    EXPECT_U64_EQ(buffer[1], 0u);
    EXPECT_U64_EQ(buffer[2], 0xffu);

    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_U64_EQ(status, 1u);
    EXPECT_FALSE(memory_read8(&memory, EMU_DEVICE_UART_BASE, &byte, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "read from write-only register");
    EXPECT_FALSE(memory_write32(&memory, EMU_DEVICE_UART_BASE, 0x41424344u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_FALSE(memory_write64(&memory, EMU_DEVICE_UART_BASE, 0x4142434445464748ull, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_FALSE(memory_read16(&memory, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET, (uint16_t *)&wide, error,
                               sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_write16(&memory, EMU_DEVICE_UART_BASE + 3u, 0x4142u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unaligned write access");
    EXPECT_FALSE(memory_write32(&memory, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET, 0u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write to read-only register");
    EXPECT_FALSE(memory_write8(&memory, EMU_DEVICE_UART_BASE + 0x10u, '!', error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid write register");
    EXPECT_FALSE(memory_write16(&memory, EMU_DEVICE_UART_BASE + EMU_DEVICE_SIZE - 1u, 0x4142u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "crosses device boundary");

    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 3u);
    memory_free(&memory);
    fclose(out);
}

static void test_timer_device_policy(void) {
    /* TC-V13-TIMER-001 through TC-V13-TIMER-008. */
    Memory memory;
    char error[512];
    uint32_t lo1 = 0;
    uint32_t hi = 0;
    uint32_t lo2 = 0;
    uint64_t wide = 0;

    init_memory(&memory);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &lo1, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_HI_OFFSET, &hi, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &lo2, error, sizeof(error)));
    EXPECT_U64_EQ(lo1, 0u);
    EXPECT_U64_EQ(hi, 0u);
    EXPECT_U64_EQ(lo2, 2u);

    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_RESET_OFFSET, 0u, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &lo1, error, sizeof(error)));
    EXPECT_U64_EQ(lo1, 0u);
    EXPECT_FALSE(memory_read8(&memory, EMU_DEVICE_TIMER_BASE, (uint8_t *)&lo2, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_read64(&memory, EMU_DEVICE_TIMER_BASE, &wide, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_write64(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_RESET_OFFSET, 0u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_FALSE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + 1u, &lo2, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unaligned read access");
    EXPECT_FALSE(memory_write32(&memory, EMU_DEVICE_TIMER_BASE, 0u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write to read-only register");
    EXPECT_FALSE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + 0x20u, &lo2, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid read register");
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &lo2, error, sizeof(error)));
    EXPECT_U64_EQ(lo2, 1u);

    memory_reset_devices(&memory);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &lo1, error, sizeof(error)));
    EXPECT_U64_EQ(lo1, 0u);
    memory_free(&memory);
}

static void test_random_device_policy(void) {
    /* TC-V13-RNG-001 through TC-V13-RNG-006. */
    Memory memory;
    char error[512];
    uint32_t first = 0;
    uint32_t second = 0;
    uint32_t seeded = 0;
    uint64_t wide = 0;

    init_memory(&memory);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &first, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &second, error, sizeof(error)));
    EXPECT_U64_EQ(first, 0x2a72c675u);
    EXPECT_U64_EQ(second, 0x80c2a550u);

    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_SEED_OFFSET, 0xabcd1234u, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &seeded, error, sizeof(error)));
    EXPECT_U64_EQ(seeded, 0x722d9803u);
    EXPECT_FALSE(memory_read16(&memory, EMU_DEVICE_RANDOM_BASE, (uint16_t *)&seeded, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_read64(&memory, EMU_DEVICE_RANDOM_BASE, &wide, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_write64(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, 0u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_FALSE(memory_write32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, 0u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write to read-only register");
    EXPECT_FALSE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + 0x20u, &seeded, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid read register");
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &seeded, error, sizeof(error)));
    EXPECT_U64_EQ(seeded, 0x0b9bdd86u);

    memory_reset_devices(&memory);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &first, error, sizeof(error)));
    EXPECT_U64_EQ(first, 0x2a72c675u);
    memory_free(&memory);
}

static void test_cpu_device_integration(void) {
    /* TC-V13-CPU-001 through TC-V13-CPU-007 and TC-V13-EDGE-011. */
    Cpu cpu;
    Memory memory;
    char error[512];
    FILE *out = tmpfile();
    unsigned char buffer[8] = {0};

    EXPECT_TRUE(out != NULL);
    init_memory(&memory);
    memory_set_uart_output(&memory, out);
    EXPECT_TRUE(memory_map_range(&memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC, "text", error, sizeof(error)));

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&memory, 0x1008u, op_movz_w(1, 'Q'));
    write_word(&memory, 0x100cu, op_strb(1, 0, 0));
    write_word(&memory, 0x1010u, op_hlt());
    write_word(&memory, 0x1014u, op_movz_w(1, 'Z'));
    write_word(&memory, 0x1018u, op_strb(1, 0, 0));
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 1u);
    EXPECT_U64_EQ(buffer[0], 'Q');
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_HALTED);
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 1u);

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0901u, 16));
    write_word(&memory, 0x1008u, op_ldr_w(2, 0, 0));
    write_word(&memory, 0x100cu, op_hlt());
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_U64_EQ(cpu.x[2], 0u);

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&memory, 0x1008u, op_strh(1, 0, 0));
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    cpu.x[1] = 0x4142u;
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000001008");
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_U64_EQ(cpu.pc, 0x1008u);

    write_word(&memory, 0x1008u, op_stp_x(1, 2, 0));
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 1u);

    memory_reset_devices(&memory);
    write_word(&memory, 0x1000u, op_movz_x(0, 0xfffcu, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&memory, 0x1008u, op_ldr_pre_w(3, 0, 4));
    write_word(&memory, 0x100cu, op_hlt());
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_U64_EQ(cpu.x[0], EMU_DEVICE_TIMER_BASE);
    EXPECT_U64_EQ(cpu.x[3], 0u);

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&memory, 0x1008u, op_movz_w(1, 'P'));
    write_word(&memory, 0x100cu, op_str_post_b(1, 0, 4));
    write_word(&memory, 0x1010u, op_hlt());
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_U64_EQ(cpu.x[0], EMU_DEVICE_UART_BASE + 4u);
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 2u);
    EXPECT_U64_EQ(buffer[1], 'P');

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0901u, 16));
    write_word(&memory, 0x1008u, op_ldr_x(4, 0, EMU_UART_STATUS_OFFSET));
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported read width");

    write_word(&memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&memory, 0x1008u, op_str_x(4, 0, 0));
    cpu_init(&cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_OK);
    EXPECT_TRUE(cpu_step(&cpu, &memory, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unsupported write width");

    memory_free(&memory);
    fclose(out);
}

static void test_device_execution_and_limits(void) {
    /* TC-V13-EDGE-012 and direct fetch-from-device policy. */
    Emulator emu;
    char error[512];
    FILE *out = tmpfile();
    unsigned char buffer[8] = {0};

    EXPECT_TRUE(out != NULL);
    EXPECT_TRUE(emulator_init(&emu, error, sizeof(error)));
    emu.instruction_limit = 8u;
    emu.stdout_stream = out;
    memory_set_uart_output(&emu.memory, out);
    memory_clear_mappings(&emu.memory);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC,
                                 "text", error, sizeof(error)));
    write_word(&emu.memory, 0x1000u, op_movz_x(0, 0x0000u, 0));
    write_word(&emu.memory, 0x1004u, op_movk_x(0, 0x0900u, 16));
    write_word(&emu.memory, 0x1008u, op_movz_w(1, 'L'));
    write_word(&emu.memory, 0x100cu, op_strb(1, 0, 0));
    write_word(&emu.memory, 0x1010u, op_b(-4));
    cpu_init(&emu.cpu, 0x1000u, EMU_MEMORY_SIZE);
    EXPECT_TRUE(emulator_run(&emu, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit reached");
    EXPECT_SIZE_EQ(read_tmp(out, buffer, sizeof(buffer)), 3u);
    EXPECT_U64_EQ(buffer[0], 'L');
    EXPECT_U64_EQ(buffer[1], 'L');
    EXPECT_U64_EQ(buffer[2], 'L');
    emulator_free(&emu);

    cpu_init(&emu.cpu, 0x1000u, EMU_MEMORY_SIZE);
    init_memory(&emu.memory);
    memory_clear_mappings(&emu.memory);
    cpu_init(&emu.cpu, EMU_DEVICE_UART_BASE, EMU_MEMORY_SIZE);
    EXPECT_TRUE(cpu_step(&emu.cpu, &emu.memory, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "reserved non-executable device range");
    memory_free(&emu.memory);
    fclose(out);
}

static void test_loader_device_boundaries(void) {
    /* TC-V13-LOAD-004/005/006 unit-level policies. */
    Memory memory;
    char error[512];

    init_memory(&memory);
    EXPECT_FALSE(memory_map_range(&memory, EMU_DEVICE_UART_BASE, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE,
                                  "bad-device-overlap", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "reserved device range");
    EXPECT_FALSE(memory_check_execute(&memory, EMU_DEVICE_UART_BASE, 4u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "reserved non-executable device range");
    memory_free(&memory);
}

int main(void) {
    test_device_map_and_routing();
    test_uart_device_policy();
    test_timer_device_policy();
    test_random_device_policy();
    test_cpu_device_integration();
    test_device_execution_and_limits();
    test_loader_device_boundaries();
    if (tests_failed != 0) {
        fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v1.3 device unit tests passed (%d checks)\n", tests_run);
    return 0;
}

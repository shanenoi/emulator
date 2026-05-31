#include "emulator.h"
#include "emulator_guest.h"

#include <inttypes.h>
#include <stdio.h>

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

static uint32_t read_frame_reg(Memory *memory, uint64_t offset) {
    char error[256];
    uint32_t value = 0xdeadbeefu;
    EXPECT_TRUE(memory_read32(memory, EMU_DEVICE_FRAME_BASE + offset, &value, error, sizeof(error)));
    return value;
}

static void write_frame_reg(Memory *memory, uint64_t offset, uint32_t value) {
    char error[256];
    EXPECT_TRUE(memory_write32(memory, EMU_DEVICE_FRAME_BASE + offset, value, error, sizeof(error)));
}

static uint64_t read_counter(Memory *memory) {
    uint32_t lo = read_frame_reg(memory, EMU_FRAME_COUNTER_LO_OFFSET);
    uint32_t hi = read_frame_reg(memory, EMU_FRAME_COUNTER_HI_OFFSET);
    return ((uint64_t)hi << 32u) | lo;
}

static void test_initial_state(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_U64_EQ(memory_frame_counter(&memory), 0u);
    EXPECT_FALSE(memory_frame_ready(&memory));
    EXPECT_U64_EQ(read_frame_reg(&memory, EMU_FRAME_STATUS_OFFSET), 0u);
    EXPECT_U64_EQ(read_counter(&memory), 0u);
    memory_free(&memory);
}

static void test_advance_and_ack(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    memory_advance_frame(&memory);
    EXPECT_U64_EQ(memory_frame_counter(&memory), 1u);
    EXPECT_TRUE(memory_frame_ready(&memory));
    EXPECT_U64_EQ(read_counter(&memory), 1u);
    EXPECT_U64_EQ(read_frame_reg(&memory, EMU_FRAME_STATUS_OFFSET), EMU_FRAME_STATUS_READY);
    write_frame_reg(&memory, EMU_FRAME_CONTROL_OFFSET, EMU_FRAME_CONTROL_CLEAR_READY);
    EXPECT_FALSE(memory_frame_ready(&memory));
    EXPECT_U64_EQ(read_counter(&memory), 1u);
    memory_free(&memory);
}

static void test_multiple_frames(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    for (uint64_t i = 1; i <= 5; i++) {
        memory_advance_frame(&memory);
        EXPECT_U64_EQ(memory_frame_counter(&memory), i);
        EXPECT_U64_EQ(read_counter(&memory), i);
    }
    EXPECT_TRUE(memory_frame_ready(&memory));
    memory_free(&memory);
}

static void test_guest_constants_compile(void) {
    EXPECT_U64_EQ(EMU_GUEST_FRAME_BASE, EMU_DEVICE_FRAME_BASE);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_STATUS, EMU_FRAME_STATUS_OFFSET);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_COUNTER_LO, EMU_FRAME_COUNTER_LO_OFFSET);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_COUNTER_HI, EMU_FRAME_COUNTER_HI_OFFSET);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_CONTROL, EMU_FRAME_CONTROL_OFFSET);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_STATUS_READY, EMU_FRAME_STATUS_READY);
    EXPECT_U64_EQ(EMU_GUEST_FRAME_CONTROL_CLEAR_READY, EMU_FRAME_CONTROL_CLEAR_READY);
}

int main(void) {
    test_initial_state();
    test_advance_and_ack();
    test_multiple_frames();
    test_guest_constants_compile();

    if (tests_failed != 0) {
        fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/v1_10/test_v1_10: %d checks passed\n", tests_run);
    return 0;
}

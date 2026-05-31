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
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static void test_keyboard_empty(void) {
    Memory memory;
    char error[256];
    uint32_t status = 99;
    uint32_t data = 99;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &data, error, sizeof(error)));
    EXPECT_U64_EQ(status, 0u);
    EXPECT_U64_EQ(data, 0u);
    memory_free(&memory);
}

static void test_keyboard_single_byte(void) {
    Memory memory;
    char error[256];
    uint32_t status = 0;
    uint32_t data = 0;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_keyboard_enqueue(&memory, 'x'));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_U64_EQ(status, EMU_KBD_STATUS_READY);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &data, error, sizeof(error)));
    EXPECT_U64_EQ(data, 'x');
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_U64_EQ(status, 0u);
    memory_free(&memory);
}

static void test_keyboard_fifo(void) {
    Memory memory;
    char error[256];
    uint32_t data = 0;
    const uint8_t bytes[] = {'w', 'a', 's', 'd'};

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_U64_EQ(memory_keyboard_enqueue_bytes(&memory, bytes, sizeof(bytes)), sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); i++) {
        EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &data, error, sizeof(error)));
        EXPECT_U64_EQ(data, bytes[i]);
    }
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &data, error, sizeof(error)));
    EXPECT_U64_EQ(data, 0u);
    memory_free(&memory);
}

static void test_keyboard_overflow_and_clear(void) {
    Memory memory;
    char error[256];
    uint32_t status = 0;
    uint32_t data = 0;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    for (unsigned i = 0; i < EMU_KBD_QUEUE_CAPACITY; i++) {
        EXPECT_TRUE(memory_keyboard_enqueue(&memory, (uint8_t)('A' + i)));
    }
    EXPECT_FALSE(memory_keyboard_enqueue(&memory, 'Z'));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_U64_EQ(status, EMU_KBD_STATUS_READY | EMU_KBD_STATUS_OVERFLOW);
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_CONTROL_OFFSET, EMU_KBD_CONTROL_CLEAR_OVERFLOW, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &status, error, sizeof(error)));
    EXPECT_U64_EQ(status, EMU_KBD_STATUS_READY);
    for (unsigned i = 0; i < EMU_KBD_QUEUE_CAPACITY; i++) {
        EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &data, error, sizeof(error)));
        EXPECT_U64_EQ(data, 'A' + i);
    }
    memory_free(&memory);
}

static void test_keyboard_register_policy(void) {
    Memory memory;
    char error[256];
    uint8_t byte = 0;
    uint64_t wide = 0;

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_FALSE(memory_read8(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &byte, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_write64(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_CONTROL_OFFSET, 1u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");
    EXPECT_FALSE(memory_read64(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &wide, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported read width");
    EXPECT_FALSE(memory_write32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, 1u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write to read-only register");
    EXPECT_FALSE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_CONTROL_OFFSET, (uint32_t *)&wide, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "read from write-only register");
    memory_free(&memory);
}

int main(void) {
    test_keyboard_empty();
    test_keyboard_single_byte();
    test_keyboard_fifo();
    test_keyboard_overflow_and_clear();
    test_keyboard_register_policy();

    if (tests_failed != 0) {
        fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/v1_7/test_v1_7: %d checks passed\n", tests_run);
    return 0;
}

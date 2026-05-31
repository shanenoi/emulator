#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
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

static uint32_t read_reg(Memory *memory, uint64_t offset) {
    char error[256];
    uint32_t value = 0xdeadbeefu;
    EXPECT_TRUE(memory_read32(memory, EMU_DEVICE_TERMINAL_BASE + offset, &value, error, sizeof(error)));
    return value;
}

static void write_reg(Memory *memory, uint64_t offset, uint32_t value) {
    char error[256];
    EXPECT_TRUE(memory_write32(memory, EMU_DEVICE_TERMINAL_BASE + offset, value, error, sizeof(error)));
}

static uint8_t read_cell(Memory *memory, uint32_t index) {
    write_reg(memory, EMU_TERM_INDEX_OFFSET, index);
    return (uint8_t)read_reg(memory, EMU_TERM_CELL_OFFSET);
}

static void test_initial_state(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_WIDTH_OFFSET), EMU_TERM_DEFAULT_WIDTH);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_HEIGHT_OFFSET), EMU_TERM_DEFAULT_HEIGHT);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_STATUS_OFFSET), 0u);
    EXPECT_U64_EQ(read_cell(&memory, 0), ' ');
    EXPECT_U64_EQ(read_cell(&memory, EMU_TERM_DEFAULT_WIDTH * EMU_TERM_DEFAULT_HEIGHT - 1u), ' ');
    memory_free(&memory);
}

static void test_single_character(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'A');
    EXPECT_U64_EQ(read_cell(&memory, 0), 'A');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 1u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_STATUS_OFFSET), EMU_TERM_STATUS_DIRTY);
    memory_free(&memory);
}

static void test_newline_and_wrap(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_terminal_configure(&memory, 5, 3, error, sizeof(error)));
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'A');
    write_reg(&memory, EMU_TERM_DATA_OFFSET, '\n');
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'B');
    EXPECT_U64_EQ(read_cell(&memory, 0), 'A');
    EXPECT_U64_EQ(read_cell(&memory, 5), 'B');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 1u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 1u);

    write_reg(&memory, EMU_TERM_CURSOR_X_OFFSET, 4);
    write_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET, 1);
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'Z');
    EXPECT_U64_EQ(read_cell(&memory, 9), 'Z');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 2u);
    memory_free(&memory);
}

static void test_clear_and_home(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_terminal_configure(&memory, 5, 3, error, sizeof(error)));
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'A');
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'B');
    write_reg(&memory, EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_CLEAR);
    EXPECT_U64_EQ(read_cell(&memory, 0), ' ');
    EXPECT_U64_EQ(read_cell(&memory, 1), ' ');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 0u);

    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'C');
    write_reg(&memory, EMU_TERM_DATA_OFFSET, 'D');
    write_reg(&memory, EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_HOME);
    EXPECT_U64_EQ(read_cell(&memory, 0), 'C');
    EXPECT_U64_EQ(read_cell(&memory, 1), 'D');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 0u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 0u);

    write_reg(&memory, EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_CLEAR_DIRTY);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_STATUS_OFFSET), 0u);
    memory_free(&memory);
}

static void test_direct_cell_access(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_terminal_configure(&memory, 5, 3, error, sizeof(error)));
    write_reg(&memory, EMU_TERM_INDEX_OFFSET, 7);
    write_reg(&memory, EMU_TERM_CELL_OFFSET, '@');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CELL_OFFSET), '@');
    EXPECT_U64_EQ(read_cell(&memory, 7), '@');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_STATUS_OFFSET), EMU_TERM_STATUS_DIRTY);
    memory_free(&memory);
}

static void test_bounds_and_scroll(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_terminal_configure(&memory, 5, 3, error, sizeof(error)));
    write_reg(&memory, EMU_TERM_CURSOR_X_OFFSET, 999);
    write_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET, 999);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 4u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 2u);

    write_reg(&memory, EMU_TERM_INDEX_OFFSET, 999);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CELL_OFFSET), 0u);
    write_reg(&memory, EMU_TERM_CELL_OFFSET, 'X');
    EXPECT_U64_EQ(read_cell(&memory, 14), ' ');

    write_reg(&memory, EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_CLEAR);
    const char *text = "ABCDEFGHIJKLMNOZ";
    for (size_t i = 0; text[i] != '\0'; i++) {
        write_reg(&memory, EMU_TERM_DATA_OFFSET, (uint8_t)text[i]);
    }
    EXPECT_U64_EQ(read_cell(&memory, 0), 'F');
    EXPECT_U64_EQ(read_cell(&memory, 9), 'O');
    EXPECT_U64_EQ(read_cell(&memory, 10), 'Z');
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_X_OFFSET), 1u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_CURSOR_Y_OFFSET), 2u);
    memory_free(&memory);
}

static void test_config_validation(void) {
    Memory memory;
    char error[256];

    EXPECT_TRUE(memory_init(&memory, EMU_MEMORY_SIZE, error, sizeof(error)));
    EXPECT_TRUE(memory_terminal_configure(&memory, 10, 3, error, sizeof(error)));
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_WIDTH_OFFSET), 10u);
    EXPECT_U64_EQ(read_reg(&memory, EMU_TERM_HEIGHT_OFFSET), 3u);
    EXPECT_FALSE(memory_terminal_configure(&memory, 0, 3, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid screen size");
    EXPECT_FALSE(memory_terminal_configure(&memory, 10, 101, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "invalid screen size");
    memory_free(&memory);
}

int main(void) {
    test_initial_state();
    test_single_character();
    test_newline_and_wrap();
    test_clear_and_home();
    test_direct_cell_access();
    test_bounds_and_scroll();
    test_config_validation();

    if (tests_failed != 0) {
        fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/v1_8/test_v1_8: %d checks passed\n", tests_run);
    return 0;
}

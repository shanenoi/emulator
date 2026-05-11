#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TMP "tests/v1_2/tmp/"
#define OP_HLT_0 0xd4400000u

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

static void init_memory_or_die(Memory *memory) {
    char error[512];
    if (!memory_init(memory, EMU_MEMORY_SIZE, error, sizeof(error))) {
        fprintf(stderr, "memory_init failed: %s\n", error);
        tests_failed++;
    }
    memory_clear_mappings(memory);
}

static void init_emulator_or_die(Emulator *emu) {
    char error[512];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        tests_failed++;
    }
}

static bool load_fixture(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size) {
    memset(error, 0, error_size);
    memset(program, 0, sizeof(*program));
    return emulator_load_program(emu, path, program, error, error_size);
}

static void test_page_mapping_model(void) {
    /* TC-V12-PAGE-001 through TC-V12-PAGE-010 and TC-V12-EDGE-005/006/007/008/010. */
    Memory memory;
    char error[512];
    char perms[4];

    EXPECT_U64_EQ(EMU_PAGE_SIZE, 4096u);
    EXPECT_U64_EQ(EMU_PAGE_SIZE & (EMU_PAGE_SIZE - 1u), 0u);

    init_memory_or_die(&memory);
    EXPECT_TRUE(memory.permissions_enabled);
    EXPECT_SIZE_EQ(memory.mapping_count, 0u);

    EXPECT_TRUE(memory_map_range(&memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ, "r", error, sizeof(error)));
    EXPECT_FALSE(memory_map_range(&memory, 0x1001u, 16u, EMU_MAP_WRITE, "overlap", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping");
    EXPECT_SIZE_EQ(memory.mapping_count, 1u);

    EXPECT_TRUE(memory_map_range(&memory, 0x2000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "rw", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "rx", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x4001u, 7u, EMU_MAP_EXEC, "unaligned-exact", error, sizeof(error)));
    EXPECT_SIZE_EQ(memory.mapping_count, 4u);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x4001u) != NULL);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x4000u) == NULL);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x4008u) == NULL);

    const EmuMemoryMapping *mapping = memory_find_mapping(&memory, 0x2000u);
    EXPECT_TRUE(mapping != NULL);
    EXPECT_U64_EQ(mapping->start, 0x2000u);
    EXPECT_U64_EQ(mapping->size, EMU_PAGE_SIZE);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x0fffu) == NULL);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x2fffu) == mapping);
    EXPECT_TRUE(memory_find_mapping(&memory, 0x3000u) != mapping);

    memory_format_permissions(EMU_MAP_READ, perms, sizeof(perms));
    EXPECT_TRUE(strcmp(perms, "r--") == 0);
    memory_format_permissions(EMU_MAP_READ | EMU_MAP_WRITE, perms, sizeof(perms));
    EXPECT_TRUE(strcmp(perms, "rw-") == 0);
    memory_format_permissions(EMU_MAP_READ | EMU_MAP_EXEC, perms, sizeof(perms));
    EXPECT_TRUE(strcmp(perms, "r-x") == 0);
    memory_format_permissions(EMU_MAP_EXEC, perms, sizeof(perms));
    EXPECT_TRUE(strcmp(perms, "--x") == 0);

    EXPECT_FALSE(memory_map_range(&memory, 0x5000u, 0u, EMU_MAP_READ, "zero", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "zero-length");
    EXPECT_FALSE(memory_map_range(&memory, 0x5000u, EMU_PAGE_SIZE, 0u, "none", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "at least one permission");
    EXPECT_FALSE(memory_map_range(&memory, EMU_MEMORY_SIZE - 16u, 32u, EMU_MAP_READ, "oob", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "outside memory");
    EXPECT_FALSE(memory_map_range(&memory, UINT64_MAX - 4u, 16u, EMU_MAP_READ, "overflow", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "outside memory");

    for (size_t i = 0; i < EMU_MAX_MEMORY_MAPPINGS - 4u; i++) {
        uint64_t base = 0x5000u + (uint64_t)i * EMU_PAGE_SIZE;
        EXPECT_TRUE(memory_map_range(&memory, base, EMU_PAGE_SIZE, EMU_MAP_READ, "many", error, sizeof(error)));
    }
    EXPECT_FALSE(memory_map_range(&memory, 0x25000u, EMU_PAGE_SIZE, EMU_MAP_READ, "too-many", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "too many mappings");

    memory_free(&memory);
}

static void test_access_enforcement(void) {
    /* TC-V12-ACCESS-001 through TC-V12-ACCESS-012 and TC-V12-EDGE-009/014. */
    Memory memory;
    char error[512];
    EmuMemoryFaultKind fault = EMU_MEMORY_FAULT_NONE;
    uint8_t byte = 0;
    uint32_t word = 0;
    uint64_t qword = 0;

    init_memory_or_die(&memory);
    EXPECT_TRUE(memory_map_range(&memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ, "read-only", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x2000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "read-write", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_EXEC, "exec-only", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x4000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "text", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, EMU_MEMORY_SIZE - EMU_PAGE_SIZE, EMU_PAGE_SIZE,
                                 EMU_MAP_READ | EMU_MAP_WRITE, "last", error, sizeof(error)));

    memory.bytes[0x1000] = 0x5au;
    EXPECT_TRUE(memory_read8(&memory, 0x1000u, &byte, error, sizeof(error)));
    EXPECT_U64_EQ(byte, 0x5au);

    EXPECT_FALSE(memory_check_access(&memory, 0x3000u, 1u, EMU_MAP_READ, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_READ_PERMISSION);
    EXPECT_STR_CONTAINS(error, "read permission denied");

    EXPECT_TRUE(memory_write64(&memory, 0x2000u, 0x1122334455667788ull, error, sizeof(error)));
    EXPECT_TRUE(memory_read64(&memory, 0x2000u, &qword, error, sizeof(error)));
    EXPECT_U64_EQ(qword, 0x1122334455667788ull);
    EXPECT_FALSE(memory_write8(&memory, 0x1000u, 0xffu, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write permission denied");
    EXPECT_U64_EQ(memory.bytes[0x1000], 0x5au);

    memory.bytes[0x4000] = (uint8_t)(OP_HLT_0 & 0xffu);
    memory.bytes[0x4001] = (uint8_t)((OP_HLT_0 >> 8u) & 0xffu);
    memory.bytes[0x4002] = (uint8_t)((OP_HLT_0 >> 16u) & 0xffu);
    memory.bytes[0x4003] = (uint8_t)((OP_HLT_0 >> 24u) & 0xffu);
    EXPECT_TRUE(memory_fetch32(&memory, 0x4000u, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, OP_HLT_0);
    EXPECT_FALSE(memory_check_access(&memory, 0x2000u, 4u, EMU_MAP_EXEC, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_EXEC_PERMISSION);
    EXPECT_STR_CONTAINS(error, "execute permission denied");

    EXPECT_FALSE(memory_check_access(&memory, 0x9000u, 8u, EMU_MAP_WRITE, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_UNMAPPED);
    EXPECT_STR_CONTAINS(error, "unmapped access");

    EXPECT_TRUE(memory_map_range(&memory, 0x5000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "cross-a", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&memory, 0x6000u, EMU_PAGE_SIZE, EMU_MAP_READ, "cross-b", error, sizeof(error)));
    memset(&memory.bytes[0x5ffcu], 0x11, 8u);
    EXPECT_FALSE(memory_write64(&memory, 0x5ffcu, 0xaabbccddeeff0011ull, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write permission denied");
    for (size_t i = 0; i < 8u; i++) {
        EXPECT_U64_EQ(memory.bytes[0x5ffcu + i], 0x11u);
    }
    EXPECT_TRUE(memory_read64(&memory, 0x5ffcu, &qword, error, sizeof(error)));

    EXPECT_TRUE(memory_map_range(&memory, 0x7000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "single", error, sizeof(error)));
    EXPECT_FALSE(memory_read32(&memory, 0x7ffeu, &word, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unmapped access");

    EXPECT_TRUE(memory_write8(&memory, EMU_MEMORY_SIZE - 1u, 0xabu, error, sizeof(error)));
    EXPECT_TRUE(memory_read8(&memory, EMU_MEMORY_SIZE - 1u, &byte, error, sizeof(error)));
    EXPECT_U64_EQ(byte, 0xabu);
    EXPECT_FALSE(memory_read32(&memory, EMU_MEMORY_SIZE - 2u, &word, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "out of bounds");
    EXPECT_FALSE(memory_check_access(&memory, UINT64_MAX - 1u, 8u, EMU_MAP_READ, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_BOUNDS);

    fault = EMU_MEMORY_FAULT_WRITE_PERMISSION;
    EXPECT_TRUE(memory_check_access(&memory, 0x2000u, 0u, EMU_MAP_WRITE, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_NONE);
    EXPECT_TRUE(memory_check_access(&memory, 0x2000u, 1u, EMU_MAP_WRITE, &fault, error, sizeof(error)));
    EXPECT_U64_EQ(fault, EMU_MEMORY_FAULT_NONE);

    memory_free(&memory);
}

static void test_raw_loader_stack_and_cpu_faults(void) {
    /* TC-V12-LOAD-001/002/015, TC-V12-STACK-001..007, TC-V12-CPU-001/005/006/008. */
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "exit7_raw.bin", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_RAW);
    EXPECT_U64_EQ(program.entry, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);

    const EmuMemoryMapping *code = memory_find_mapping(&emu.memory, EMU_LOAD_ADDRESS);
    EXPECT_TRUE(code != NULL);
    EXPECT_U64_EQ(code->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    EXPECT_TRUE(strstr(code->name, "raw") != NULL);
    EXPECT_TRUE(memory_find_mapping(&emu.memory, 0x2000u) == NULL);

    const EmuMemoryMapping *stack = memory_find_mapping(&emu.memory, EMU_MEMORY_SIZE - 1u);
    EXPECT_TRUE(stack != NULL);
    EXPECT_U64_EQ(stack->permissions, EMU_MAP_READ | EMU_MAP_WRITE);
    EXPECT_TRUE(strcmp(stack->name, "stack") == 0);
    EmuMemoryMapping guard;
    EXPECT_TRUE(memory_find_stack_guard(&emu.memory, stack->start - 1u, &guard));
    EXPECT_U64_EQ(guard.permissions, 0u);
    EXPECT_STR_CONTAINS(guard.name, "stack-guard");
    EXPECT_FALSE(memory_check_write(&emu.memory, stack->start - 8u, 8u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unmapped access");
    EXPECT_TRUE(memory_write64(&emu.memory, stack->start, 0xabcdu, error, sizeof(error)));
    EXPECT_FALSE(memory_check_execute(&emu.memory, stack->start, 4u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "execute permission denied");

    EXPECT_TRUE(emulator_run(&emu, error, sizeof(error)) == EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 7u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "write_code_page.bin", &program, error, sizeof(error)));
    EXPECT_TRUE(emulator_run(&emu, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "write permission denied");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "execute_unmapped.bin", &program, error, sizeof(error)));
    EXPECT_TRUE(emulator_run(&emu, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unmapped access");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "execute_stack.bin", &program, error, sizeof(error)));
    EXPECT_TRUE(emulator_run(&emu, error, sizeof(error)) == EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "execute permission denied");
    emulator_free(&emu);
}

static void test_elf_and_macho_loader_permissions(void) {
    /* TC-V12-LOAD-003 through TC-V12-LOAD-014 and TC-V12-EDGE-001..004/011/012. */
    Emulator emu;
    EmuLoadedProgram program;
    char error[512];
    uint64_t value = 0;

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "text_data.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_ELF64);
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    const EmuMemoryMapping *text = memory_find_mapping(&emu.memory, 0x1000u);
    const EmuMemoryMapping *data = memory_find_mapping(&emu.memory, 0x3000u);
    EXPECT_TRUE(text != NULL);
    EXPECT_TRUE(data != NULL);
    EXPECT_U64_EQ(text->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    EXPECT_U64_EQ(data->permissions, EMU_MAP_READ | EMU_MAP_WRITE);
    for (size_t i = 4u; i < 12u; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[0x3000u + i], 0u);
    }
    EXPECT_FALSE(memory_write8(&emu.memory, 0x1000u, 0x99u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "write permission denied");
    EXPECT_TRUE(memory_write64(&emu.memory, 0x3004u, 0x0102030405060708ull, error, sizeof(error)));
    EXPECT_TRUE(memory_read64(&emu.memory, 0x3004u, &value, error, sizeof(error)));
    EXPECT_U64_EQ(value, 0x0102030405060708ull);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "data_entry.elf", &program, error, sizeof(error)));
    /* v1.2 intentionally keeps the documented compatibility policy: the entry segment is executable. */
    EXPECT_TRUE(memory_check_execute(&emu.memory, 0x3000u, 4u, error, sizeof(error)));
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "zero_mem_segment.elf", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    EXPECT_TRUE(memory_find_mapping(&emu.memory, 0x5000u) == NULL);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "stdout_data.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_MACHO64);
    const EmuMemoryMapping *mtext = memory_find_mapping(&emu.memory, 0x1000u);
    const EmuMemoryMapping *mdata = memory_find_mapping(&emu.memory, 0x3000u);
    EXPECT_TRUE(mtext != NULL);
    EXPECT_TRUE(mdata != NULL);
    EXPECT_U64_EQ(mtext->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    EXPECT_U64_EQ(mdata->permissions, EMU_MAP_READ | EMU_MAP_WRITE);
    EXPECT_FALSE(memory_check_execute(&emu.memory, 0x3000u, 4u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "execute permission denied");
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_fixture(&emu, TMP "overlap_1byte.macho", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping");
    EXPECT_SIZE_EQ(emu.memory.mapping_count, 0u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "adjacent.macho", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_fixture(&emu, TMP "zero_sized_segment.macho", &program, error, sizeof(error)));
    EXPECT_SIZE_EQ(program.segment_count, 1u);
    emulator_free(&emu);
}

int main(void) {
    test_page_mapping_model();
    test_access_enforcement();
    test_raw_loader_stack_and_cpu_faults();
    test_elf_and_macho_loader_permissions();

    if (tests_failed != 0) {
        fprintf(stderr, "%d of %d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v1.2 unit tests passed (%d checks)\n", tests_run);
    return 0;
}

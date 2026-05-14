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

#define VECTOR 0x1080ull

static uint32_t op_hlt(void) { return 0xd4400000u; }
static uint32_t op_nop(void) { return 0xd503201fu; }
static uint32_t op_eret(void) { return 0xd69f03e0u; }
static uint32_t op_brk(unsigned imm) { return 0xd4200000u | ((imm & 0xffffu) << 5u); }
static uint32_t op_svc(unsigned imm) { return 0xd4000001u | ((imm & 0xffffu) << 5u); }
static uint32_t op_b(int offset_bytes) { return 0x14000000u | (((uint32_t)(offset_bytes / 4)) & 0x03ffffffu); }
static uint32_t op_add_x_imm(unsigned rd, unsigned rn, unsigned imm) {
    return 0x91000000u | ((imm & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rd & 31u);
}
static uint32_t op_ldr_w(unsigned rt, unsigned rn, unsigned imm) {
    return 0xb9400000u | (((imm / 4u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_ldr_x(unsigned rt, unsigned rn, unsigned imm) {
    return 0xf9400000u | (((imm / 8u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_str_x(unsigned rt, unsigned rn, unsigned imm) {
    return 0xf9000000u | (((imm / 8u) & 0xfffu) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_str_post_x(unsigned rt, unsigned rn, int offset) {
    return 0xf8000400u | (((uint32_t)offset & 0x1ffu) << 12u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_ldp_x(unsigned rt, unsigned rt2, unsigned rn) {
    return 0xa9400000u | ((rt2 & 31u) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_stp_x(unsigned rt, unsigned rt2, unsigned rn) {
    return 0xa9000000u | ((rt2 & 31u) << 10u) | ((rn & 31u) << 5u) | (rt & 31u);
}
static uint32_t op_udiv_x(unsigned rd, unsigned rn, unsigned rm) {
    return 0x9ac00800u | ((rm & 31u) << 16u) | ((rn & 31u) << 5u) | (rd & 31u);
}

static void write_word(Memory *memory, uint64_t address, uint32_t word) {
    memory->bytes[address + 0u] = (uint8_t)(word & 0xffu);
    memory->bytes[address + 1u] = (uint8_t)((word >> 8u) & 0xffu);
    memory->bytes[address + 2u] = (uint8_t)((word >> 16u) & 0xffu);
    memory->bytes[address + 3u] = (uint8_t)((word >> 24u) & 0xffu);
}

static void init_emu(Emulator *emu) {
    char error[512];
    EXPECT_TRUE(emulator_init(emu, error, sizeof(error)));
}

static void configure_vector_or_fail(Emulator *emu) {
    char error[512];
    EXPECT_TRUE(emulator_configure_exception_vector(emu, VECTOR, error, sizeof(error)));
}

static void install_paged_text_or_fail(Emulator *emu) {
    char error[512];
    memory_clear_mappings(&emu->memory);
    EXPECT_TRUE(memory_map_range(&emu->memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "text", error,
                                 sizeof(error)));
    configure_vector_or_fail(emu);
}

static void test_default_and_vector_configuration(void) {
    /* TC-V14-CFG-001 through TC-V14-CFG-006 and reset visibility basics. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);

    EXPECT_FALSE(emu.exceptions.vector_configured);
    EXPECT_U64_EQ(emu.exceptions.vector_base, 0u);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_TRUE(emu.exceptions.interrupts_enabled);
    EXPECT_FALSE(emu.exceptions.pending_timer_interrupt);
    EXPECT_U64_EQ(emulator_get_exception_context(&emu)->cause, EMU_EXCEPTION_NONE);

    EXPECT_TRUE(emulator_configure_exception_vector(&emu, VECTOR, error, sizeof(error)));
    EXPECT_TRUE(emu.exceptions.vector_configured);
    EXPECT_U64_EQ(emu.exceptions.vector_base, VECTOR);

    EXPECT_FALSE(emulator_configure_exception_vector(&emu, VECTOR + 2u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "4-byte aligned");
    EXPECT_U64_EQ(emu.exceptions.vector_base, VECTOR);

    EXPECT_FALSE(emulator_configure_exception_vector(&emu, EMU_DEVICE_UART_BASE, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "device range");

    EXPECT_FALSE(emulator_configure_exception_vector(&emu, 0x800000u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "not executable");

    emulator_clear_exception_vector(&emu);
    EXPECT_FALSE(emu.exceptions.vector_configured);
    EXPECT_U64_EQ(emu.exceptions.vector_base, 0u);
    emulator_free(&emu);
}

static void test_brk_context_and_return(void) {
    /* TC-V14-CTX-001/002/004/005/006/009/010 and TC-V14-RET-001/009/010. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    configure_vector_or_fail(&emu);

    emu.cpu.flags.n = true;
    emu.cpu.flags.c = true;
    cpu_write_register(&emu.cpu, 10, true, 0xabcddcbau);
    write_word(&emu.memory, 0x1000u, op_brk(0x14));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, VECTOR, op_eret());

    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_BREAKPOINT_OR_TRAP);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x14u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 0x1000u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 3), 0x1004u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 10), 0xabcddcbau);
    EXPECT_TRUE(emu.cpu.flags.n);
    EXPECT_TRUE(emu.cpu.flags.c);
    EXPECT_U64_EQ(emulator_get_exception_context(&emu)->cause, EMU_EXCEPTION_NONE);
    EXPECT_U64_EQ(emulator_get_exception_context(&emu)->depth, 0u);
    EXPECT_FALSE(emu.exceptions.active);
    emulator_free(&emu);
}

static void test_invalid_instruction_and_device_fault_can_skip(void) {
    /* TC-V14-SYNC-001/011, TC-V14-CTX-003/007, and TC-V14-RET-003. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    configure_vector_or_fail(&emu);

    write_word(&emu.memory, 0x1000u, 0xffffffffu);
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, VECTOR, op_add_x_imm(3, 3, 4));
    write_word(&emu.memory, VECTOR + 4u, op_eret());
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_INVALID_INSTRUCTION);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 0x1000u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 3), 0x1004u);
    emulator_free(&emu);

    init_emu(&emu);
    configure_vector_or_fail(&emu);
    cpu_write_register(&emu.cpu, 4, true, EMU_DEVICE_UART_BASE);
    write_word(&emu.memory, 0x1000u, op_ldr_w(5, 4, 0));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, VECTOR, op_add_x_imm(3, 3, 4));
    write_word(&emu.memory, VECTOR + 4u, op_eret());
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_DEVICE_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), EMU_DEVICE_UART_BASE);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 5), 0u);
    emulator_free(&emu);
}

static void test_svc_policy_and_divide_by_zero(void) {
    /* TC-V14-SYNC-003/004/014 and v0.7 SVC #0 compatibility. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    configure_vector_or_fail(&emu);
    write_word(&emu.memory, 0x1000u, op_svc(1));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, VECTOR, op_eret());
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_SVC_TRAP);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 1u);
    emulator_free(&emu);

    init_emu(&emu);
    cpu_write_register(&emu.cpu, 8, true, 0x777u);
    write_word(&emu.memory, 0x1000u, op_svc(0));
    write_word(&emu.memory, 0x1004u, op_hlt());
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), (uint64_t)EMU_SYSCALL_ENOSYS);
    emulator_free(&emu);

    init_emu(&emu);
    cpu_write_register(&emu.cpu, 1, true, 123u);
    cpu_write_register(&emu.cpu, 2, true, 0u);
    write_word(&emu.memory, 0x1000u, op_udiv_x(0, 1, 2));
    write_word(&emu.memory, 0x1004u, op_hlt());
    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 0u);
    emulator_free(&emu);
}

static void test_return_and_nested_fault_errors(void) {
    /* TC-V14-RET-004/005/006/007 and TC-V14-EDGE-005/008. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    EXPECT_FALSE(emulator_exception_return(&emu, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "eret without active exception");
    emulator_free(&emu);

    init_emu(&emu);
    configure_vector_or_fail(&emu);
    EXPECT_U64_EQ(emulator_raise_exception(&emu, EMU_EXCEPTION_BREAKPOINT_OR_TRAP, 0, 0x1000u, 0x1004u, error,
                                           sizeof(error)), EMU_OK);
    cpu_write_register(&emu.cpu, 3, true, 0x1002u);
    EXPECT_FALSE(emulator_exception_return(&emu, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "misaligned");
    cpu_write_register(&emu.cpu, 3, true, EMU_DEVICE_UART_BASE);
    EXPECT_FALSE(emulator_exception_return(&emu, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "not executable");
    emulator_free(&emu);

    init_emu(&emu);
    configure_vector_or_fail(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(0x33));
    write_word(&emu.memory, VECTOR, 0xffffffffu);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "double fault");
    emulator_free(&emu);
}

static void test_fetch_and_permission_faults(void) {
    /* TC-V14-SYNC-005/006/007/008/009/010/012 and v1.2 permission interaction. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    install_paged_text_or_fail(&emu);

    write_word(&emu.memory, 0x1000u, op_b(0x5000 - 0x1000));
    write_word(&emu.memory, VECTOR, op_eret());
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, 0x5000u);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_FETCH_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x5000u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 0x5000u);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ, "ro-data", error,
                                 sizeof(error)));
    write_word(&emu.memory, 0x1000u, op_b(0x3000 - 0x1000));
    write_word(&emu.memory, 0x3000u, op_hlt());
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_EXEC_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x3000u);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    emu.cpu.pc = EMU_DEVICE_UART_BASE;
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_EXEC_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), EMU_DEVICE_UART_BASE);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x4000u, EMU_PAGE_SIZE, EMU_MAP_EXEC, "exec-only", error,
                                 sizeof(error)));
    cpu_write_register(&emu.cpu, 4, true, 0x4000u);
    cpu_write_register(&emu.cpu, 5, true, 0xaaaa5555u);
    write_word(&emu.memory, 0x1000u, op_ldr_x(5, 4, 0));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_READ_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x4000u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 5), 0xaaaa5555u);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ, "ro-data", error,
                                 sizeof(error)));
    cpu_write_register(&emu.cpu, 4, true, 0x3000u);
    cpu_write_register(&emu.cpu, 5, true, 0x1111222233334444ull);
    write_word(&emu.memory, 0x1000u, op_str_x(5, 4, 0));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_WRITE_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x3000u);
    uint64_t stored = 0;
    EXPECT_TRUE(memory_read64(&emu.memory, 0x3000u, &stored, error, sizeof(error)));
    EXPECT_U64_EQ(stored, 0u);
    emulator_free(&emu);
}

static void test_exception_return_to_unmapped_target(void) {
    /* TC-V14-RET-008: return validation still respects the page map. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_U64_EQ(emulator_raise_exception(&emu, EMU_EXCEPTION_BREAKPOINT_OR_TRAP, 0, 0x1000u, 0x1004u, error,
                                           sizeof(error)), EMU_OK);
    cpu_write_register(&emu.cpu, 3, true, 0x5000u);
    EXPECT_FALSE(emulator_exception_return(&emu, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "not executable");
    emulator_free(&emu);
}

static void test_sequential_handled_exceptions(void) {
    /* TC-V14-EDGE-001: two handled traps in one run return independently. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    configure_vector_or_fail(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(0x11));
    write_word(&emu.memory, 0x1004u, op_brk(0x22));
    write_word(&emu.memory, 0x1008u, op_hlt());
    write_word(&emu.memory, VECTOR, op_eret());

    EXPECT_U64_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_BREAKPOINT_OR_TRAP);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0x22u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 0x1004u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 3), 0x1008u);
    emulator_free(&emu);
}

static void test_faulting_memory_operations_are_atomic(void) {
    /* TC-V14-EDGE-002/003/004: no write-back, register, or partial-memory update on faults. */
    Emulator emu;
    char error[512] = {0};
    uint64_t value = 0;
    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ, "ro-data", error,
                                 sizeof(error)));
    cpu_write_register(&emu.cpu, 4, true, 0x3000u);
    cpu_write_register(&emu.cpu, 5, true, 0x777788889999aaaau);
    write_word(&emu.memory, 0x1000u, op_str_post_x(5, 4, 8));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_WRITE_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 4), 0x3000u);
    EXPECT_TRUE(memory_read64(&emu.memory, 0x3000u, &value, error, sizeof(error)));
    EXPECT_U64_EQ(value, 0u);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "rw-data", error,
                                 sizeof(error)));
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x4000u, EMU_PAGE_SIZE, EMU_MAP_READ, "ro-data", error,
                                 sizeof(error)));
    EXPECT_TRUE(memory_write64(&emu.memory, 0x3ff8u, 0xaaaaaaaaaaaaaaaau, error, sizeof(error)));
    cpu_write_register(&emu.cpu, 4, true, 0x3ff8u);
    cpu_write_register(&emu.cpu, 5, true, 0x1111111111111111u);
    cpu_write_register(&emu.cpu, 6, true, 0x2222222222222222u);
    write_word(&emu.memory, 0x1000u, op_stp_x(5, 6, 4));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_WRITE_PERMISSION_FAULT);
    EXPECT_TRUE(memory_read64(&emu.memory, 0x3ff8u, &value, error, sizeof(error)));
    EXPECT_U64_EQ(value, 0xaaaaaaaaaaaaaaaau);
    EXPECT_TRUE(memory_read64(&emu.memory, 0x4000u, &value, error, sizeof(error)));
    EXPECT_U64_EQ(value, 0u);
    emulator_free(&emu);

    init_emu(&emu);
    install_paged_text_or_fail(&emu);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE, "rw-data", error,
                                 sizeof(error)));
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x4000u, EMU_PAGE_SIZE, EMU_MAP_WRITE, "write-only", error,
                                 sizeof(error)));
    EXPECT_TRUE(memory_write64(&emu.memory, 0x3ff8u, 0xbbbbbbbbbbbbbbbbu, error, sizeof(error)));
    cpu_write_register(&emu.cpu, 4, true, 0x3ff8u);
    cpu_write_register(&emu.cpu, 5, true, 0x5555u);
    cpu_write_register(&emu.cpu, 6, true, 0x6666u);
    write_word(&emu.memory, 0x1000u, op_ldp_x(5, 6, 4));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_READ_PERMISSION_FAULT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 5), 0x5555u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 6), 0x6666u);
    emulator_free(&emu);
}

static void test_mmio_exception_controller(void) {
    /* TC-V14-CFG-002, TC-V14-CTX-001/002/003, and TC-V14-DEV-001 through register surface. */
    Emulator emu;
    char error[512] = {0};
    uint32_t control = 0;
    uint64_t value = 0;
    init_emu(&emu);

    EXPECT_TRUE(memory_write64(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_VECTOR_OFFSET, VECTOR, error,
                               sizeof(error)));
    EXPECT_TRUE(memory_write32(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_CONTROL_OFFSET,
                               EMU_EXCEPTION_CONTROL_VECTOR_ENABLE | EMU_EXCEPTION_CONTROL_INTERRUPTS_ENABLE, error,
                               sizeof(error)));
    EXPECT_TRUE(memory_read32(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_CONTROL_OFFSET, &control, error,
                              sizeof(error)));
    EXPECT_U64_EQ(control & EMU_EXCEPTION_CONTROL_VECTOR_ENABLE, EMU_EXCEPTION_CONTROL_VECTOR_ENABLE);
    EXPECT_TRUE(memory_read64(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_VECTOR_OFFSET, &value, error,
                              sizeof(error)));
    EXPECT_U64_EQ(value, VECTOR);

    EXPECT_U64_EQ(emulator_raise_exception(&emu, EMU_EXCEPTION_READ_FAULT, 0xdeadbeefu, 0x1000u, 0x1000u, error,
                                           sizeof(error)), EMU_OK);
    EXPECT_TRUE(memory_read32(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_CAUSE_OFFSET, &control, error,
                              sizeof(error)));
    EXPECT_U64_EQ(control, EMU_EXCEPTION_READ_FAULT);
    EXPECT_TRUE(memory_read64(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_FAULT_ADDRESS_OFFSET, &value,
                              error, sizeof(error)));
    EXPECT_U64_EQ(value, 0xdeadbeefu);
    EXPECT_FALSE(memory_write32(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_CAUSE_OFFSET, 0u, error,
                                sizeof(error)));
    EXPECT_STR_CONTAINS(error, "read-only");
    emulator_free(&emu);
}

static void test_timer_interrupt_mask_and_delivery(void) {
    /* TC-V14-IRQ-001 through TC-V14-IRQ-006 plus return mask policy. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    configure_vector_or_fail(&emu);
    write_word(&emu.memory, 0x1000u, op_nop());
    write_word(&emu.memory, 0x1004u, op_nop());
    write_word(&emu.memory, 0x1008u, op_hlt());
    write_word(&emu.memory, VECTOR, op_eret());

    emulator_set_interrupts_enabled(&emu, false);
    EXPECT_TRUE(emulator_queue_timer_interrupt(&emu));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_TRUE(emu.exceptions.pending_timer_interrupt);

    emulator_set_interrupts_enabled(&emu, true);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_TIMER_INTERRUPT);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 2), 0x1008u);
    EXPECT_FALSE(emu.exceptions.interrupts_enabled);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_TRUE(emu.exceptions.interrupts_enabled);
    EXPECT_U64_EQ(emu.cpu.pc, 0x1008u);
    emulator_free(&emu);

    init_emu(&emu);
    configure_vector_or_fail(&emu);
    emu.cpu.instructions_executed = 100u;
    EXPECT_TRUE(memory_write64(&emu.memory, EMU_DEVICE_EXCEPTION_BASE + EMU_EXCEPTION_TIMER_INTERVAL_OFFSET, 3u,
                               error, sizeof(error)));
    EXPECT_TRUE(emu.exceptions.timer_deadline_relative_pending);
    write_word(&emu.memory, 0x1000u, op_nop());
    write_word(&emu.memory, 0x1004u, op_nop());
    write_word(&emu.memory, 0x1008u, op_nop());
    write_word(&emu.memory, 0x100cu, op_nop());
    write_word(&emu.memory, 0x1010u, op_hlt());
    write_word(&emu.memory, VECTOR, op_eret());
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.pending_timer_interrupt);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.pending_timer_interrupt);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.pending_timer_interrupt);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_EXCEPTION_TIMER_INTERRUPT);
    emulator_free(&emu);
}

static void test_unhandled_mode_stays_terminal(void) {
    /* TC-V14-CFG-001, TC-V14-SYNC-002 handled/unhandled pair, and CLI exit-code policy at API level. */
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(0x44));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unhandled exception");
    EXPECT_STR_CONTAINS(error, "breakpoint-or-trap");
    EXPECT_FALSE(emu.exceptions.active);
    emulator_free(&emu);
}

int main(void) {
    test_default_and_vector_configuration();
    test_brk_context_and_return();
    test_invalid_instruction_and_device_fault_can_skip();
    test_svc_policy_and_divide_by_zero();
    test_return_and_nested_fault_errors();
    test_fetch_and_permission_faults();
    test_exception_return_to_unmapped_target();
    test_sequential_handled_exceptions();
    test_faulting_memory_operations_are_atomic();
    test_mmio_exception_controller();
    test_timer_interrupt_mask_and_delivery();
    test_unhandled_mode_stays_terminal();

    if (tests_failed != 0) {
        fprintf(stderr, "v1.4 exception tests failed: %d of %d checks failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v1.4 exception unit tests passed (%d checks)\n", tests_run);
    return 0;
}

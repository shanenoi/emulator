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
#define EXPECT_STATUS_EQ(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

static uint32_t op_hlt(void) { return 0xd4400000u; }
static uint32_t op_nop(void) { return 0xd503201fu; }
static uint32_t op_brk(unsigned imm) { return 0xd4200000u | ((imm & 0xffffu) << 5u); }
static uint32_t op_eret(void) { return 0xd69f03e0u; }
static uint32_t op_movz_x(unsigned rd, unsigned imm, unsigned shift) {
    return 0xd2800000u | (((shift / 16u) & 3u) << 21u) | ((imm & 0xffffu) << 5u) | (rd & 31u);
}
static uint32_t op_cmp_imm_x(unsigned rn, unsigned imm) {
    return 0xf100001fu | ((imm & 0xfffu) << 10u) | ((rn & 31u) << 5u);
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

static void init_emu(Emulator *emu) {
    char error[512];
    EXPECT_TRUE(emulator_init(emu, error, sizeof(error)));
    memory_clear_mappings(&emu->memory);
    EXPECT_TRUE(memory_map_range(&emu->memory, 0x1000u, 0x3000u, EMU_MAP_READ | EMU_MAP_EXEC, "test:text",
                                 error, sizeof(error)));
    EXPECT_TRUE(memory_map_stack(&emu->memory, EMU_MEMORY_SIZE, EMU_STACK_SIZE, error, sizeof(error)));
}

static void test_boot_info_and_task_setup(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, EMU_LOAD_ADDRESS, op_hlt());
    write_word(&emu.memory, 0x1040u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));

    cpu_write_register(&emu.cpu, 5, true, 0xbeef);
    emu.cpu.flags.n = true;
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, true, error, sizeof(error)));
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_TRUE(kernel->enabled);
    EXPECT_TRUE(kernel->boot_info_enabled);
    EXPECT_U64_EQ(kernel->kernel_entry, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), kernel->boot_info_address);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), sizeof(EmuToyKernelBootInfo));
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 5), 0u);
    EXPECT_FALSE(emu.cpu.flags.n);
    EXPECT_U64_EQ(kernel->boot_info.magic, EMU_TOY_KERNEL_BOOT_INFO_MAGIC);
    EXPECT_U64_EQ(kernel->boot_info.version, EMU_TOY_KERNEL_BOOT_INFO_VERSION);
    EXPECT_U64_EQ(kernel->boot_info.memory_size, EMU_MEMORY_SIZE);
    EXPECT_U64_EQ(kernel->boot_info.uart_base, EMU_DEVICE_UART_BASE);

    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));
    EXPECT_U64_EQ(kernel->task_count, 1u);
    EXPECT_U64_EQ(kernel->boot_info.task_count, 1u);
    EXPECT_U64_EQ(kernel->tasks[0].entry, 0x1040u);
    EXPECT_U64_EQ(kernel->tasks[0].stack_size, EMU_TOY_KERNEL_STACK_SIZE);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_READY);
    emulator_free(&emu);
}

static void test_explicit_start_and_exit_status(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_movz_x(0, 7, 0));
    write_word(&emu.memory, 0x1014u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));

    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_TRUE(kernel->tasks_started);
    EXPECT_TRUE(kernel->completed);
    EXPECT_U64_EQ(emu.guest_exit_code, 0u);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[0].exit_code, 7u);
    EXPECT_U64_EQ(kernel->tasks[0].pc, 0x1018u);
    emulator_free(&emu);
}

static void test_context_round_robin_and_flags(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_movz_x(19, 0x1111u, 0));
    write_word(&emu.memory, 0x1014u, op_movz_x(0, 5u, 0));
    write_word(&emu.memory, 0x1018u, op_cmp_imm_x(0, 5u));
    write_word(&emu.memory, 0x101cu, op_brk(EMU_TOY_KERNEL_TRAP_YIELD));
    write_word(&emu.memory, 0x1020u, op_movz_x(0, 1u, 0));
    write_word(&emu.memory, 0x1024u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    write_word(&emu.memory, 0x1040u, op_movz_x(19, 0x2222u, 0));
    write_word(&emu.memory, 0x1044u, op_movz_x(0, 2u, 0));
    write_word(&emu.memory, 0x1048u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));

    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[1].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[0].yields, 1u);
    EXPECT_U64_EQ(kernel->tasks[0].x[19], 0x1111u);
    EXPECT_U64_EQ(kernel->tasks[1].x[19], 0x2222u);
    EXPECT_TRUE(kernel->tasks[0].flags.z);
    EXPECT_FALSE(kernel->tasks[0].flags.n);
    EXPECT_U64_EQ(kernel->tasks[0].exit_code, 1u);
    EXPECT_U64_EQ(kernel->tasks[1].exit_code, 2u);
    emulator_free(&emu);
}

static void test_full_context_switch_round_trip(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_brk(EMU_TOY_KERNEL_TRAP_YIELD));
    write_word(&emu.memory, 0x1014u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    write_word(&emu.memory, 0x1040u, op_brk(EMU_TOY_KERNEL_TRAP_YIELD));
    write_word(&emu.memory, 0x1044u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));

    EXPECT_STATUS_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->current_task, 0u);
    uint64_t task0_sp = kernel->tasks[0].stack_base + 0x100u;
    uint64_t task1_sp = kernel->tasks[1].stack_base + 0x200u;
    for (uint8_t reg = 0; reg < 31u; reg++) {
        cpu_write_register(&emu.cpu, reg, true, 0xabc000u + reg);
    }
    emu.cpu.sp = task0_sp;
    emu.cpu.flags = (EmuFlags){true, false, true, false};
    EXPECT_STATUS_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_READY);
    EXPECT_U64_EQ(kernel->tasks[0].pc, 0x1014u);
    EXPECT_U64_EQ(kernel->tasks[0].sp, task0_sp);
    EXPECT_TRUE(kernel->tasks[0].flags.n);
    EXPECT_FALSE(kernel->tasks[0].flags.z);
    EXPECT_TRUE(kernel->tasks[0].flags.c);
    EXPECT_FALSE(kernel->tasks[0].flags.v);
    for (uint8_t reg = 0; reg < 31u; reg++) {
        EXPECT_U64_EQ(kernel->tasks[0].x[reg], 0xabc000u + reg);
        cpu_write_register(&emu.cpu, reg, true, 0xdef000u + reg);
    }
    emu.cpu.sp = task1_sp;
    emu.cpu.flags = (EmuFlags){false, true, false, true};

    EXPECT_STATUS_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->current_task, 0u);
    EXPECT_U64_EQ(emu.cpu.pc, 0x1014u);
    EXPECT_U64_EQ(emu.cpu.sp, task0_sp);
    EXPECT_TRUE(emu.cpu.flags.n);
    EXPECT_FALSE(emu.cpu.flags.z);
    EXPECT_TRUE(emu.cpu.flags.c);
    EXPECT_FALSE(emu.cpu.flags.v);
    for (uint8_t reg = 0; reg < 31u; reg++) {
        EXPECT_U64_EQ(cpu_read_register(&emu.cpu, reg), 0xabc000u + reg);
    }
    EXPECT_U64_EQ(kernel->tasks[1].pc, 0x1044u);
    EXPECT_U64_EQ(kernel->tasks[1].sp, task1_sp);
    EXPECT_FALSE(kernel->tasks[1].flags.n);
    EXPECT_TRUE(kernel->tasks[1].flags.z);
    EXPECT_FALSE(kernel->tasks[1].flags.c);
    EXPECT_TRUE(kernel->tasks[1].flags.v);
    for (uint8_t reg = 0; reg < 31u; reg++) {
        EXPECT_U64_EQ(kernel->tasks[1].x[reg], 0xdef000u + reg);
    }
    emulator_free(&emu);
}

static void test_task_fault_isolated_and_status_71(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, 0xffffffffu);
    write_word(&emu.memory, 0x1040u, op_movz_x(0, 3u, 0));
    write_word(&emu.memory, 0x1044u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));

    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_TRUE(kernel->completed);
    EXPECT_U64_EQ(emu.guest_exit_code, 71u);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_FAULTED);
    EXPECT_U64_EQ(kernel->tasks[0].fault_cause, EMU_EXCEPTION_INVALID_INSTRUCTION);
    EXPECT_U64_EQ(kernel->tasks[0].fault_address, 0u);
    EXPECT_U64_EQ(kernel->tasks[1].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[1].exit_code, 3u);
    emulator_free(&emu);
}

static void test_eret_fault_isolated_inside_task(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_eret());
    write_word(&emu.memory, 0x1040u, op_movz_x(0, 6u, 0));
    write_word(&emu.memory, 0x1044u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));

    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(emu.guest_exit_code, 71u);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_FAULTED);
    EXPECT_U64_EQ(kernel->tasks[0].fault_cause, EMU_EXCEPTION_INVALID_INSTRUCTION);
    EXPECT_U64_EQ(kernel->tasks[1].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[1].exit_code, 6u);
    emulator_free(&emu);
}

static void test_scheduler_edges_zero_three_and_max_tasks(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emulator_get_toy_kernel(&emu)->completed);
    EXPECT_U64_EQ(emu.guest_exit_code, 0u);
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_brk(EMU_TOY_KERNEL_TRAP_YIELD));
    write_word(&emu.memory, 0x1014u, op_movz_x(0, 1u, 0));
    write_word(&emu.memory, 0x1018u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    write_word(&emu.memory, 0x1040u, op_brk(EMU_TOY_KERNEL_TRAP_YIELD));
    write_word(&emu.memory, 0x1044u, op_movz_x(0, 2u, 0));
    write_word(&emu.memory, 0x1048u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    write_word(&emu.memory, 0x1080u, op_movz_x(0, 3u, 0));
    write_word(&emu.memory, 0x1084u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1080u, error, sizeof(error)));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    for (size_t i = 0; i < 3u; i++) {
        EXPECT_U64_EQ(kernel->tasks[i].state, EMU_TOY_TASK_EXITED);
        EXPECT_U64_EQ(kernel->tasks[i].exit_code, i + 1u);
    }
    EXPECT_U64_EQ(kernel->tasks[0].yields, 1u);
    EXPECT_U64_EQ(kernel->tasks[1].yields, 1u);
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    for (size_t i = 0; i < EMU_TOY_KERNEL_MAX_TASKS; i++) {
        uint64_t pc = 0x1100u + (uint64_t)i * 0x10u;
        write_word(&emu.memory, pc, op_movz_x(0, (unsigned)i, 0));
        write_word(&emu.memory, pc + 4u, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
        EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, pc, error, sizeof(error)));
    }
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    kernel = emulator_get_toy_kernel(&emu);
    for (size_t i = 0; i < EMU_TOY_KERNEL_MAX_TASKS; i++) {
        EXPECT_U64_EQ(kernel->tasks[i].state, EMU_TOY_TASK_EXITED);
        EXPECT_U64_EQ(kernel->tasks[i].exit_code, i);
    }
    emulator_free(&emu);
}

static void test_timer_wakes_sleeping_task_and_schedules(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1004u, op_hlt());
    write_word(&emu.memory, 0x1010u, op_movz_x(0, 1u, 0));
    write_word(&emu.memory, 0x1014u, op_brk(EMU_TOY_KERNEL_TRAP_SLEEP));
    write_word(&emu.memory, 0x1018u, op_movz_x(0, 9u, 0));
    write_word(&emu.memory, 0x101cu, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    write_word(&emu.memory, 0x1040u, op_nop());
    write_word(&emu.memory, 0x1044u, op_nop());
    write_word(&emu.memory, 0x1048u, op_movz_x(0, 4u, 0));
    write_word(&emu.memory, 0x104cu, op_brk(EMU_TOY_KERNEL_TRAP_TASK_EXIT));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));
    emulator_configure_timer_interrupt(&emu, 2u);

    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[0].exit_code, 9u);
    EXPECT_U64_EQ(kernel->tasks[0].wake_tick, 0u);
    EXPECT_TRUE(kernel->timer_ticks >= 1u);
    EXPECT_TRUE(kernel->timer_schedules >= 1u);
    emulator_free(&emu);
}

static void test_deadlock_panic_and_validation_errors(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    write_word(&emu.memory, 0x1010u, op_movz_x(0, 1u, 0));
    write_word(&emu.memory, 0x1014u, op_brk(EMU_TOY_KERNEL_TRAP_SLEEP));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, 0x1010u, error, sizeof(error)));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "deadlock");
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_movz_x(0, 0x70u, 0));
    write_word(&emu.memory, 0x1004u, op_brk(EMU_TOY_KERNEL_TRAP_PANIC));
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_TRUE(emulator_get_toy_kernel(&emu)->panic);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->panic_code, 0x70u);
    EXPECT_STR_CONTAINS(error, "toy kernel panic");
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_hlt());
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    EXPECT_FALSE(emulator_toy_kernel_add_task(&emu, 0x1002u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "4-byte aligned");
    for (size_t i = 0; i < EMU_TOY_KERNEL_MAX_TASKS; i++) {
        uint64_t pc = 0x1100u + (uint64_t)i * 4u;
        write_word(&emu.memory, pc, op_hlt());
        EXPECT_TRUE(emulator_toy_kernel_add_task(&emu, pc, error, sizeof(error)));
    }
    write_word(&emu.memory, 0x1200u, op_hlt());
    EXPECT_FALSE(emulator_toy_kernel_add_task(&emu, 0x1200u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "too many toy kernel tasks");
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_hlt());
    write_word(&emu.memory, 0x1040u, op_hlt());
    EXPECT_TRUE(emulator_enable_toy_kernel(&emu, false, error, sizeof(error)));
    uint64_t first_task_stack_base = EMU_MEMORY_SIZE - EMU_STACK_SIZE - EMU_PAGE_SIZE - EMU_TOY_KERNEL_STACK_SIZE;
    EXPECT_TRUE(memory_map_range(&emu.memory, first_task_stack_base, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE,
                                 "test:stack-overlap", error, sizeof(error)));
    EXPECT_FALSE(emulator_toy_kernel_add_task(&emu, 0x1040u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping");
    emulator_free(&emu);
}

static void test_kernel_traps_do_not_change_non_kernel_brk_behavior(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_START_TASKS));
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "unhandled exception");
    emulator_free(&emu);

    init_emu(&emu);
    write_word(&emu.memory, 0x1000u, op_b(0));
    emu.instruction_limit = 3u;
    EXPECT_STATUS_EQ(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit reached");
    emulator_free(&emu);
}

int main(void) {
    test_boot_info_and_task_setup();
    test_explicit_start_and_exit_status();
    test_context_round_robin_and_flags();
    test_full_context_switch_round_trip();
    test_task_fault_isolated_and_status_71();
    test_eret_fault_isolated_inside_task();
    test_scheduler_edges_zero_three_and_max_tasks();
    test_timer_wakes_sleeping_task_and_schedules();
    test_deadlock_panic_and_validation_errors();
    test_kernel_traps_do_not_change_non_kernel_brk_behavior();

    if (tests_failed != 0) {
        fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/v1_5/test_v1_5: %d tests passed\n", tests_run);
    return 0;
}

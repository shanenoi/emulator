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
#define EXPECT_I64_EQ(actual, expected) do { \
    tests_run++; \
    int64_t actual_value__ = (int64_t)(actual); \
    int64_t expected_value__ = (int64_t)(expected); \
    if (actual_value__ != expected_value__) { \
        fprintf(stderr, "FAIL %s:%d: %s == %" PRId64 ", expected %" PRId64 "\n", \
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
static uint32_t op_brk(unsigned imm) { return 0xd4200000u | ((imm & 0xffffu) << 5u); }

static void write_word(Memory *memory, uint64_t address, uint32_t word) {
    memory->bytes[address + 0u] = (uint8_t)(word & 0xffu);
    memory->bytes[address + 1u] = (uint8_t)((word >> 8u) & 0xffu);
    memory->bytes[address + 2u] = (uint8_t)((word >> 16u) & 0xffu);
    memory->bytes[address + 3u] = (uint8_t)((word >> 24u) & 0xffu);
}

static void init_emu(Emulator *emu, bool boot_info) {
    char error[512] = {0};
    EXPECT_TRUE(emulator_init(emu, error, sizeof(error)));
    memory_clear_mappings(&emu->memory);
    EXPECT_TRUE(memory_map_range(&emu->memory, 0x1000u, 0x4000u, EMU_MAP_READ | EMU_MAP_EXEC, "text", error, sizeof(error)));
    EXPECT_TRUE(memory_map_range(&emu->memory, 0x6000u, 0x4000u, EMU_MAP_READ | EMU_MAP_WRITE, "data", error, sizeof(error)));
    EXPECT_TRUE(memory_map_stack(&emu->memory, EMU_MEMORY_SIZE, EMU_STACK_SIZE, error, sizeof(error)));
    write_word(&emu->memory, 0x1000u, op_brk(EMU_TOY_KERNEL_TRAP_SERVICE));
    write_word(&emu->memory, 0x1004u, op_hlt());
    write_word(&emu->memory, 0x1100u, op_brk(EMU_TOY_KERNEL_TRAP_SERVICE));
    write_word(&emu->memory, 0x1104u, op_hlt());
    write_word(&emu->memory, 0x1200u, op_brk(EMU_TOY_KERNEL_TRAP_SERVICE));
    write_word(&emu->memory, 0x1204u, op_hlt());
    EXPECT_TRUE(emulator_enable_toy_kernel(emu, boot_info, error, sizeof(error)));
}

static EmuStatus step_service(Emulator *emu, uint64_t pc, char *error, size_t error_size) {
    emu->cpu.pc = pc;
    emu->cpu.halted = false;
    return emulator_step(emu, error, error_size);
}

static uint64_t service_create(Emulator *emu, uint64_t entry, uint64_t stack_base, uint64_t stack_size,
                               uint64_t arg0, uint64_t flags, const char *name) {
    char error[512] = {0};
    if (name != NULL) {
        size_t len = strlen(name);
        memcpy(&emu->memory.bytes[0x6000u], name, len);
        cpu_write_register(&emu->cpu, 5, true, 0x6000u);
        cpu_write_register(&emu->cpu, 6, true, len);
    } else {
        cpu_write_register(&emu->cpu, 5, true, 0u);
        cpu_write_register(&emu->cpu, 6, true, 0u);
    }
    cpu_write_register(&emu->cpu, 8, true, EMU_TOY_SERVICE_TASK_CREATE);
    cpu_write_register(&emu->cpu, 0, true, entry);
    cpu_write_register(&emu->cpu, 1, true, stack_base);
    cpu_write_register(&emu->cpu, 2, true, stack_size);
    cpu_write_register(&emu->cpu, 3, true, arg0);
    cpu_write_register(&emu->cpu, 4, true, flags);
    EXPECT_STATUS_EQ(step_service(emu, 0x1000u, error, sizeof(error)), EMU_OK);
    return cpu_read_register(&emu->cpu, 0);
}

static int64_t signed_x0(const Emulator *emu) {
    return (int64_t)cpu_read_register(&emu->cpu, 0);
}

static void set_running_task(Emulator *emu, size_t index) {
    EmuToyKernel *kernel = &emu->toy_kernel;
    for (size_t i = 0; i < kernel->task_count; i++) {
        if (kernel->tasks[i].state == EMU_TOY_TASK_RUNNING) {
            kernel->tasks[i].state = EMU_TOY_TASK_READY;
        }
    }
    kernel->tasks_started = true;
    kernel->current_task = index;
    kernel->tasks[index].state = EMU_TOY_TASK_RUNNING;
}

static void test_boot_info_descriptor_readonly_and_discovery(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu, true);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->boot_info.magic, EMU_TOY_KERNEL_BOOT_INFO_MAGIC);
    EXPECT_U64_EQ(kernel->boot_info.version, EMU_TOY_KERNEL_BOOT_INFO_VERSION);
    EXPECT_U64_EQ(kernel->boot_info.size, sizeof(EmuToyKernelBootInfo));
    EXPECT_U64_EQ(kernel->boot_info.memory_base, 0u);
    EXPECT_U64_EQ(kernel->boot_info.memory_size, EMU_MEMORY_SIZE);
    EXPECT_U64_EQ(kernel->boot_info.descriptor_table_address, EMU_TOY_KERNEL_DESCRIPTOR_TABLE_ADDRESS);
    EXPECT_U64_EQ(kernel->boot_info.descriptor_count, EMU_TOY_KERNEL_MAX_TASKS);
    EXPECT_U64_EQ(kernel->boot_info.descriptor_size, sizeof(EmuToyTaskDescriptor));
    EXPECT_U64_EQ(kernel->boot_info.service_trap, EMU_TOY_KERNEL_TRAP_SERVICE);
    EXPECT_U64_EQ(kernel->boot_info.mailbox_slots, EMU_TOY_KERNEL_MAILBOX_SLOTS);
    EXPECT_U64_EQ(kernel->boot_info.mailbox_message_size, EMU_TOY_KERNEL_MAILBOX_MESSAGE_SIZE);
    EXPECT_U64_EQ(kernel->boot_info.supported_services, EMU_TOY_SERVICE_SUPPORTED_MASK);
    EXPECT_U64_EQ(kernel->boot_info.descriptor_table_address % 16u, 0u);
    EXPECT_FALSE(memory_write64(&emu.memory, kernel->boot_info_address, 0, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "permission denied");
    memset(error, 0, sizeof(error));
    EXPECT_FALSE(memory_write64(&emu.memory, kernel->descriptor_table_address, 0, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "permission denied");
    EXPECT_U64_EQ(kernel->boot_info.magic, EMU_TOY_KERNEL_BOOT_INFO_MAGIC);
    emulator_free(&emu);
}

static void test_guest_create_ids_labels_descriptors_and_capacity(void) {
    Emulator emu;
    init_emu(&emu, true);
    uint64_t id0 = service_create(&emu, 0x1100u, 0, 0, 0x1234u, 0, "alpha-task-name-is-long");
    uint64_t id1 = service_create(&emu, 0x1200u, 0, 0, 0x5678u, 0, "beta");
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(id0, 0u);
    EXPECT_U64_EQ(id1, 1u);
    EXPECT_U64_EQ(kernel->task_count, 2u);
    EXPECT_U64_EQ(kernel->next_task_id, 2u);
    EXPECT_U64_EQ(kernel->tasks[0].guest_created, 1u);
    EXPECT_U64_EQ(kernel->tasks[0].x[0], 0x1234u);
    EXPECT_TRUE(strncmp(kernel->tasks[0].name, "alpha-task-name", EMU_TOY_KERNEL_TASK_NAME_SIZE - 1u) == 0);
    EmuToyTaskDescriptor desc;
    memcpy(&desc, &emu.memory.bytes[kernel->descriptor_table_address], sizeof(desc));
    EXPECT_U64_EQ(desc.magic, EMU_TOY_KERNEL_DESCRIPTOR_MAGIC);
    EXPECT_U64_EQ(desc.version, EMU_TOY_KERNEL_DESCRIPTOR_VERSION);
    EXPECT_U64_EQ(desc.task_id, 0u);
    EXPECT_U64_EQ(desc.state, EMU_TOY_TASK_READY);
    EXPECT_U64_EQ(desc.entry_pc, 0x1100u);
    EXPECT_U64_EQ(desc.guest_created, 1u);
    EXPECT_U64_EQ(desc.mailbox_count, 0u);

    for (size_t i = 2; i < EMU_TOY_KERNEL_MAX_TASKS; i++) {
        EXPECT_U64_EQ(service_create(&emu, 0x1100u, 0, 0, 0, 0, NULL), i);
    }
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_NO_SLOT);
    EXPECT_U64_EQ(kernel->task_count, EMU_TOY_KERNEL_MAX_TASKS);
    emulator_free(&emu);
}

static void test_task_create_validation_matrix(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu, true);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1102u, 0, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_ENTRY);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x90000u, 0, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_ENTRY);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x6000u, 0, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_ENTRY);
    EXPECT_I64_EQ((int64_t)service_create(&emu, EMU_DEVICE_UART_BASE, 0, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_ENTRY);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0, 0, 0, 1, NULL), EMU_TOY_SERVICE_ERR_BAD_FLAGS);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0x7001u, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0x7000u, 0, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0x1000u, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0x00080000ull, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, EMU_TOY_KERNEL_DESCRIPTOR_TABLE_ADDRESS, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, EMU_DEVICE_UART_BASE, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, UINT64_MAX - 0x10u, 0x40u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_U64_EQ(service_create(&emu, 0x1100u, 0x7000u, 0x100u, 0, 0, NULL), 0u);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1200u, 0x7080u, 0x100u, 0, 0, NULL), EMU_TOY_SERVICE_ERR_BAD_STACK);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->task_count, 1u);
    EXPECT_TRUE(memory_write64(&emu.memory, 0x7000u, 0xfeedu, error, sizeof(error)));
    emulator_free(&emu);
}

static void test_services_context_and_scheduler_runtime_create(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu, false);
    EXPECT_I64_EQ((int64_t)service_create(&emu, 0x1100u, 0, 0, 0, 0, NULL), 0);
    set_running_task(&emu, 0);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_GET_ID);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 0u);
    EXPECT_I64_EQ(emulator_get_toy_kernel(&emu)->last_service_status, EMU_TOY_SERVICE_OK);

    cpu_write_register(&emu.cpu, 8, true, 999u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_UNKNOWN);

    set_running_task(&emu, 0);
    uint64_t id1 = service_create(&emu, 0x1200u, 0, 0, 0x44u, 0, "rt");
    EXPECT_U64_EQ(id1, 1u);
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->tasks[1].state, EMU_TOY_TASK_READY);
    EXPECT_U64_EQ(kernel->tasks[1].x[0], 0x44u);

    set_running_task(&emu, 0);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_TASK_EXIT);
    cpu_write_register(&emu.cpu, 0, true, 9u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    kernel = emulator_get_toy_kernel(&emu);
    EXPECT_U64_EQ(kernel->tasks[0].state, EMU_TOY_TASK_EXITED);
    EXPECT_U64_EQ(kernel->tasks[0].exit_code, 9u);
    EXPECT_U64_EQ(kernel->current_task, 1u);
    EXPECT_U64_EQ(emu.cpu.pc, 0x1200u);
    emulator_free(&emu);
}

static void test_mailbox_success_boundaries_and_errors(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu, true);
    EXPECT_U64_EQ(service_create(&emu, 0x1100u, 0, 0, 0, 0, "sender"), 0u);
    EXPECT_U64_EQ(service_create(&emu, 0x1200u, 0, 0, 0, 0, "receiver"), 1u);
    set_running_task(&emu, 0);
    memcpy(&emu.memory.bytes[0x6000u], "abc", 3);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_SEND);
    cpu_write_register(&emu.cpu, 0, true, 1u);
    cpu_write_register(&emu.cpu, 1, true, 0x6000u);
    cpu_write_register(&emu.cpu, 2, true, 3u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_OK);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->tasks[1].mailbox_count, 1u);

    set_running_task(&emu, 1);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_RECV);
    cpu_write_register(&emu.cpu, 0, true, 0x7000u);
    cpu_write_register(&emu.cpu, 1, true, 2u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_BAD_ARGUMENT);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->tasks[1].mailbox_count, 1u);

    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_RECV);
    cpu_write_register(&emu.cpu, 0, true, 0x7000u);
    cpu_write_register(&emu.cpu, 1, true, 32u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 3u);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 1), 0u);
    EXPECT_TRUE(memcmp(&emu.memory.bytes[0x7000u], "abc", 3) == 0);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->tasks[1].mailbox_count, 0u);

    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_RECV);
    cpu_write_register(&emu.cpu, 0, true, 0x7000u);
    cpu_write_register(&emu.cpu, 1, true, 32u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_WOULD_BLOCK);

    set_running_task(&emu, 0);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_SEND);
    cpu_write_register(&emu.cpu, 0, true, 0u);
    cpu_write_register(&emu.cpu, 1, true, 0u);
    cpu_write_register(&emu.cpu, 2, true, 0u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_OK);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->tasks[0].mailbox_count, 1u);

    for (size_t i = emulator_get_toy_kernel(&emu)->tasks[1].mailbox_count; i < EMU_TOY_KERNEL_MAILBOX_SLOTS; i++) {
        cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_SEND);
        cpu_write_register(&emu.cpu, 0, true, 1u);
        cpu_write_register(&emu.cpu, 1, true, 0x6000u);
        cpu_write_register(&emu.cpu, 2, true, 1u);
        EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
        EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_OK);
    }
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_SEND);
    cpu_write_register(&emu.cpu, 0, true, 1u);
    cpu_write_register(&emu.cpu, 1, true, 0x6000u);
    cpu_write_register(&emu.cpu, 2, true, 1u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_WOULD_BLOCK);

    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_SEND);
    cpu_write_register(&emu.cpu, 0, true, 999u);
    cpu_write_register(&emu.cpu, 1, true, 0x6000u);
    cpu_write_register(&emu.cpu, 2, true, 1u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_BAD_ARGUMENT);
    emulator_free(&emu);
}

static void test_get_info_console_and_panic(void) {
    Emulator emu;
    char error[512] = {0};
    init_emu(&emu, true);
    EXPECT_U64_EQ(service_create(&emu, 0x1100u, 0, 0, 0, 0, NULL), 0u);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_GET_INFO);
    cpu_write_register(&emu.cpu, 0, true, 0u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), EMU_TOY_KERNEL_DESCRIPTOR_TABLE_ADDRESS);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_GET_INFO);
    cpu_write_register(&emu.cpu, 0, true, 42u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_NOT_FOUND);

    FILE *tmp = tmpfile();
    EXPECT_TRUE(tmp != NULL);
    emu.stdout_stream = tmp;
    memcpy(&emu.memory.bytes[0x6000u], "hi", 2);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_CONSOLE_WRITE);
    cpu_write_register(&emu.cpu, 0, true, 0x6000u);
    cpu_write_register(&emu.cpu, 1, true, 2u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 2u);
    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_CONSOLE_WRITE);
    cpu_write_register(&emu.cpu, 0, true, EMU_MEMORY_SIZE + 0x100u);
    cpu_write_register(&emu.cpu, 1, true, 4u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_OK);
    EXPECT_I64_EQ(signed_x0(&emu), EMU_TOY_SERVICE_ERR_BAD_ARGUMENT);
    fclose(tmp);

    cpu_write_register(&emu.cpu, 8, true, EMU_TOY_SERVICE_KERNEL_PANIC);
    cpu_write_register(&emu.cpu, 0, true, 0x55u);
    EXPECT_STATUS_EQ(step_service(&emu, 0x1000u, error, sizeof(error)), EMU_ERROR);
    EXPECT_TRUE(emulator_get_toy_kernel(&emu)->panic);
    EXPECT_U64_EQ(emulator_get_toy_kernel(&emu)->panic_code, 0x55u);
    emulator_free(&emu);
}

int main(void) {
    test_boot_info_descriptor_readonly_and_discovery();
    test_guest_create_ids_labels_descriptors_and_capacity();
    test_task_create_validation_matrix();
    test_services_context_and_scheduler_runtime_create();
    test_mailbox_success_boundaries_and_errors();
    test_get_info_console_and_panic();
    if (tests_failed != 0) {
        fprintf(stderr, "tests/v1_6/test_v1_6: %d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/v1_6/test_v1_6: %d tests passed\n", tests_run);
    return 0;
}

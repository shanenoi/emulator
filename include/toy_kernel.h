#ifndef EMU_TOY_KERNEL_H
#define EMU_TOY_KERNEL_H

#include "exceptions.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    EMU_TOY_TASK_EMPTY = 0,
    EMU_TOY_TASK_READY,
    EMU_TOY_TASK_RUNNING,
    EMU_TOY_TASK_BLOCKED,
    EMU_TOY_TASK_EXITED,
    EMU_TOY_TASK_FAULTED,
} EmuToyTaskState;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t memory_base;
    uint64_t memory_size;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_size;
    uint64_t task_stack_base;
    uint64_t task_stack_size;
    uint64_t task_count;
    uint64_t uart_base;
    uint64_t timer_base;
    uint64_t random_base;
    uint64_t exception_controller_base;
    uint64_t descriptor_table_address;
    uint64_t descriptor_count;
    uint64_t descriptor_size;
    uint64_t service_trap;
    uint64_t mailbox_slots;
    uint64_t mailbox_message_size;
    uint64_t supported_services;
} EmuToyKernelBootInfo;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t descriptor_size;
    uint64_t task_id;
    uint64_t state;
    uint64_t entry_pc;
    uint64_t saved_pc;
    uint64_t saved_sp;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t exit_code;
    uint64_t fault_cause;
    uint64_t fault_address;
    uint64_t wake_tick;
    uint64_t switch_count;
    uint64_t mailbox_count;
    uint64_t guest_created;
    char name[EMU_TOY_KERNEL_TASK_NAME_SIZE];
} EmuToyTaskDescriptor;

typedef struct {
    bool used;
    uint64_t sender_task_id;
    uint64_t length;
    uint8_t bytes[EMU_TOY_KERNEL_MAILBOX_MESSAGE_SIZE];
} EmuToyMailboxMessage;

typedef struct {
    EmuToyTaskState state;
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    EmuFlags flags;
    uint64_t entry;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t exit_code;
    uint64_t yields;
    uint64_t wake_tick;
    uint64_t task_id;
    uint64_t switch_count;
    bool guest_created;
    char name[EMU_TOY_KERNEL_TASK_NAME_SIZE];
    EmuToyMailboxMessage mailbox[EMU_TOY_KERNEL_MAILBOX_SLOTS];
    size_t mailbox_head;
    size_t mailbox_count;
    EmuExceptionCause fault_cause;
    uint64_t fault_address;
} EmuToyTask;

typedef struct EmuToyKernel {
    bool enabled;
    bool boot_info_enabled;
    bool tasks_started;
    bool completed;
    bool panic;
    uint64_t panic_code;
    uint64_t timer_ticks;
    uint64_t timer_schedules;
    uint64_t boot_info_address;
    uint64_t descriptor_table_address;
    uint64_t kernel_entry;
    uint64_t kernel_stack_top;
    uint64_t task_stack_next;
    uint64_t next_task_id;
    uint64_t service_calls;
    uint64_t last_service_id;
    int64_t last_service_status;
    uint64_t mailbox_sends;
    uint64_t mailbox_recvs;
    size_t task_count;
    size_t current_task;
    EmuToyTask tasks[EMU_TOY_KERNEL_MAX_TASKS];
    EmuToyKernelBootInfo boot_info;
} EmuToyKernel;

#endif

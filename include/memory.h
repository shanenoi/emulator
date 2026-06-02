#ifndef EMU_MEMORY_H
#define EMU_MEMORY_H

#include "exceptions.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t start;
    uint64_t size;
    uint8_t permissions;
    char name[32];
} EmuMemoryMapping;

typedef enum {
    EMU_FAULT_NONE = 0,
    EMU_FAULT_OUT_OF_BOUNDS,
    EMU_FAULT_PERMISSION,
    EMU_FAULT_UNMAPPED,
    EMU_FAULT_UNALIGNED,
    EMU_FAULT_DEVICE,
} EmuFaultKind;

typedef enum {
    EMU_FAULT_ACCESS_NONE = 0,
    EMU_FAULT_ACCESS_READ,
    EMU_FAULT_ACCESS_WRITE,
    EMU_FAULT_ACCESS_EXECUTE,
} EmuFaultAccess;

typedef struct {
    EmuFaultKind kind;
    EmuFaultAccess access;
    uint64_t address;
    uint64_t width;
    bool is_write;
    char message[192];
} EmuFault;

typedef enum {
    EMU_DEVICE_UART = 0,
    EMU_DEVICE_TIMER,
    EMU_DEVICE_RANDOM,
    EMU_DEVICE_EXCEPTION,
    EMU_DEVICE_KEYBOARD,
    EMU_DEVICE_TERMINAL,
    EMU_DEVICE_FRAME,
} EmuDeviceKind;

typedef struct {
    uint64_t start;
    uint64_t size;
    uint8_t permissions;
    EmuDeviceKind kind;
    char name[32];
} EmuDeviceRange;

typedef struct {
    EmuDeviceRange ranges[7];
    size_t range_count;
    uint64_t timer_ticks;
    uint32_t random_state;
    FILE *uart_output;
    EmuExceptionController *exceptions;
    uint8_t keyboard_queue[EMU_KBD_QUEUE_CAPACITY];
    size_t keyboard_head;
    size_t keyboard_count;
    bool keyboard_overflow;
    uint32_t terminal_width;
    uint32_t terminal_height;
    uint32_t terminal_cursor_x;
    uint32_t terminal_cursor_y;
    uint32_t terminal_index;
    bool terminal_dirty;
    uint8_t terminal_cells[EMU_TERM_MAX_CELLS];
    uint64_t frame_counter;
    bool frame_ready;
} EmuDeviceBus;

typedef struct Memory {
    uint8_t *bytes;
    size_t size;
    EmuMemoryMapping mappings[EMU_MAX_MEMORY_MAPPINGS];
    size_t mapping_count;
    bool permissions_enabled;
    EmuDeviceBus devices;
    EmuFault last_fault;
} Memory;

typedef enum {
    EMU_MEMORY_FAULT_NONE = 0,
    EMU_MEMORY_FAULT_BOUNDS,
    EMU_MEMORY_FAULT_UNMAPPED,
    EMU_MEMORY_FAULT_READ_PERMISSION,
    EMU_MEMORY_FAULT_WRITE_PERMISSION,
    EMU_MEMORY_FAULT_EXEC_PERMISSION,
} EmuMemoryFaultKind;

bool memory_init(Memory *memory, size_t size, char *error, size_t error_size);
void memory_free(Memory *memory);
void memory_clear_mappings(Memory *memory);
bool memory_map_range(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name,
                      char *error, size_t error_size);
bool memory_map_stack(Memory *memory, uint64_t stack_top, uint64_t stack_size, char *error, size_t error_size);
bool memory_check_read(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_write(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_execute(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_access(const Memory *memory, uint64_t address, uint64_t length, uint8_t required,
                         EmuMemoryFaultKind *fault_kind, char *error, size_t error_size);
void memory_clear_last_fault(Memory *memory);
const EmuFault *memory_last_fault(const Memory *memory);
bool memory_fetch32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size);
void memory_format_permissions(uint8_t permissions, char *out, size_t out_size);
void memory_print_mappings(const Memory *memory, FILE *stream);
const EmuMemoryMapping *memory_find_mapping(const Memory *memory, uint64_t address);
bool memory_find_stack_guard(const Memory *memory, uint64_t address, EmuMemoryMapping *out);
void memory_print_devices(const Memory *memory, FILE *stream);
const EmuDeviceRange *memory_find_device(const Memory *memory, uint64_t address);
void memory_reset_devices(Memory *memory);
void memory_set_uart_output(Memory *memory, FILE *stream);
bool memory_keyboard_enqueue(Memory *memory, uint8_t value);
size_t memory_keyboard_enqueue_bytes(Memory *memory, const uint8_t *bytes, size_t length);
bool memory_terminal_configure(Memory *memory, uint32_t width, uint32_t height, char *error, size_t error_size);
uint32_t memory_terminal_width(const Memory *memory);
uint32_t memory_terminal_height(const Memory *memory);
uint32_t memory_terminal_cursor_x(const Memory *memory);
uint32_t memory_terminal_cursor_y(const Memory *memory);
bool memory_terminal_dirty(const Memory *memory);
const uint8_t *memory_terminal_cells(const Memory *memory);
void memory_advance_frame(Memory *memory);
uint64_t memory_frame_counter(const Memory *memory);
bool memory_frame_ready(const Memory *memory);
bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size);
bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size);
bool memory_read16(const Memory *memory, uint64_t address, uint16_t *out, char *error, size_t error_size);
bool memory_write16(Memory *memory, uint64_t address, uint16_t value, char *error, size_t error_size);
bool memory_read32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size);
bool memory_write32(Memory *memory, uint64_t address, uint32_t value, char *error, size_t error_size);
bool memory_read64(const Memory *memory, uint64_t address, uint64_t *out, char *error, size_t error_size);
bool memory_write64(Memory *memory, uint64_t address, uint64_t value, char *error, size_t error_size);

#endif

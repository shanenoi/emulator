#include "devices.h"

#include <stdio.h>
#include <string.h>

static void terminal_clear_cells(EmuDeviceBus *devices) {
    memset(devices->terminal_cells, ' ', EMU_TERM_MAX_CELLS);
}

bool emu_terminal_dimensions_valid(uint32_t width, uint32_t height) {
    return width >= EMU_TERM_MIN_WIDTH && width <= EMU_TERM_MAX_WIDTH &&
           height >= EMU_TERM_MIN_HEIGHT && height <= EMU_TERM_MAX_HEIGHT;
}

uint32_t emu_terminal_cell_count(const EmuDeviceBus *devices) {
    return devices->terminal_width * devices->terminal_height;
}

static void terminal_reset_state(EmuDeviceBus *devices, uint32_t width, uint32_t height) {
    devices->terminal_width = width;
    devices->terminal_height = height;
    devices->terminal_cursor_x = 0;
    devices->terminal_cursor_y = 0;
    devices->terminal_index = 0;
    devices->terminal_dirty = false;
    terminal_clear_cells(devices);
}

void emu_devices_install_default(Memory *memory) {
    EmuExceptionController *exceptions = memory->devices.exceptions;
    uint8_t keyboard_queue[EMU_KBD_QUEUE_CAPACITY];
    size_t keyboard_head = memory->devices.keyboard_head;
    size_t keyboard_count = memory->devices.keyboard_count;
    bool keyboard_overflow = memory->devices.keyboard_overflow;
    uint32_t terminal_width = memory->devices.terminal_width;
    uint32_t terminal_height = memory->devices.terminal_height;
    uint32_t terminal_cursor_x = memory->devices.terminal_cursor_x;
    uint32_t terminal_cursor_y = memory->devices.terminal_cursor_y;
    uint32_t terminal_index = memory->devices.terminal_index;
    bool terminal_dirty = memory->devices.terminal_dirty;
    uint8_t terminal_cells[EMU_TERM_MAX_CELLS];
    bool keep_terminal = emu_terminal_dimensions_valid(terminal_width, terminal_height);
    uint64_t frame_counter = memory->devices.frame_counter;
    bool frame_ready = memory->devices.frame_ready;
    memcpy(keyboard_queue, memory->devices.keyboard_queue, sizeof(keyboard_queue));
    memcpy(terminal_cells, memory->devices.terminal_cells, sizeof(terminal_cells));

    memory->devices.range_count = 6;
    memory->devices.ranges[0] = (EmuDeviceRange){EMU_DEVICE_UART_BASE, EMU_DEVICE_SIZE,
                                                  EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_UART, "uart"};
    memory->devices.ranges[1] = (EmuDeviceRange){EMU_DEVICE_TIMER_BASE, EMU_DEVICE_SIZE,
                                                   EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_TIMER, "timer"};
    memory->devices.ranges[2] = (EmuDeviceRange){EMU_DEVICE_RANDOM_BASE, EMU_DEVICE_SIZE,
                                                   EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_RANDOM, "random"};
    memory->devices.ranges[3] = (EmuDeviceRange){EMU_DEVICE_KEYBOARD_BASE, EMU_DEVICE_SIZE,
                                                  EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_KEYBOARD, "keyboard"};
    memory->devices.ranges[4] = (EmuDeviceRange){EMU_DEVICE_TERMINAL_BASE, EMU_DEVICE_SIZE,
                                                  EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_TERMINAL, "terminal"};
    memory->devices.ranges[5] = (EmuDeviceRange){EMU_DEVICE_FRAME_BASE, EMU_DEVICE_SIZE,
                                                 EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_FRAME, "frame"};
    if (exceptions != NULL) {
        memory->devices.ranges[memory->devices.range_count++] =
            (EmuDeviceRange){EMU_DEVICE_EXCEPTION_BASE, EMU_DEVICE_SIZE, EMU_MAP_READ | EMU_MAP_WRITE,
                             EMU_DEVICE_EXCEPTION, "exception"};
    }
    memory->devices.timer_ticks = 0;
    memory->devices.random_state = 0x00c0ffeeu;
    memory->devices.uart_output = stdout;
    memory->devices.exceptions = exceptions;
    memcpy(memory->devices.keyboard_queue, keyboard_queue, sizeof(memory->devices.keyboard_queue));
    memory->devices.keyboard_head = keyboard_head;
    memory->devices.keyboard_count = keyboard_count;
    memory->devices.keyboard_overflow = keyboard_overflow;
    if (keep_terminal) {
        memory->devices.terminal_width = terminal_width;
        memory->devices.terminal_height = terminal_height;
        memory->devices.terminal_cursor_x = terminal_cursor_x < terminal_width ? terminal_cursor_x : terminal_width - 1u;
        memory->devices.terminal_cursor_y = terminal_cursor_y < terminal_height ? terminal_cursor_y : terminal_height - 1u;
        memory->devices.terminal_index = terminal_index;
        memory->devices.terminal_dirty = terminal_dirty;
        memcpy(memory->devices.terminal_cells, terminal_cells, sizeof(memory->devices.terminal_cells));
    } else {
        terminal_reset_state(&memory->devices, EMU_TERM_DEFAULT_WIDTH, EMU_TERM_DEFAULT_HEIGHT);
    }
    memory->devices.frame_counter = frame_counter;
    memory->devices.frame_ready = frame_ready;
}

uint32_t emu_keyboard_status_bits(const EmuDeviceBus *devices) {
    uint32_t bits = 0;
    if (devices->keyboard_count > 0) {
        bits |= EMU_KBD_STATUS_READY;
    }
    if (devices->keyboard_overflow) {
        bits |= EMU_KBD_STATUS_OVERFLOW;
    }
    return bits;
}

bool memory_keyboard_enqueue(Memory *memory, uint8_t value) {
    if (memory == NULL) {
        return false;
    }
    if (memory->devices.keyboard_count >= EMU_KBD_QUEUE_CAPACITY) {
        memory->devices.keyboard_overflow = true;
        return false;
    }
    size_t tail = (memory->devices.keyboard_head + memory->devices.keyboard_count) % EMU_KBD_QUEUE_CAPACITY;
    memory->devices.keyboard_queue[tail] = value;
    memory->devices.keyboard_count++;
    return true;
}

size_t memory_keyboard_enqueue_bytes(Memory *memory, const uint8_t *bytes, size_t length) {
    size_t accepted = 0;
    if (memory == NULL || bytes == NULL) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (memory_keyboard_enqueue(memory, bytes[i])) {
            accepted++;
        }
    }
    return accepted;
}

uint8_t emu_keyboard_pop(EmuDeviceBus *devices) {
    if (devices->keyboard_count == 0) {
        return 0;
    }
    uint8_t value = devices->keyboard_queue[devices->keyboard_head];
    devices->keyboard_head = (devices->keyboard_head + 1u) % EMU_KBD_QUEUE_CAPACITY;
    devices->keyboard_count--;
    return value;
}

void emu_terminal_mark_dirty(EmuDeviceBus *devices) {
    devices->terminal_dirty = true;
}

static void terminal_scroll_one_row(EmuDeviceBus *devices) {
    uint32_t width = devices->terminal_width;
    uint32_t height = devices->terminal_height;
    if (height > 1u) {
        memmove(devices->terminal_cells, devices->terminal_cells + width, (size_t)width * (height - 1u));
    }
    memset(devices->terminal_cells + ((size_t)width * (height - 1u)), ' ', width);
    devices->terminal_cursor_y = height - 1u;
    emu_terminal_mark_dirty(devices);
}

static void terminal_wrap_or_scroll(EmuDeviceBus *devices) {
    if (devices->terminal_cursor_y >= devices->terminal_height) {
        terminal_scroll_one_row(devices);
    }
}

void emu_terminal_set_cursor(EmuDeviceBus *devices, uint32_t x, uint32_t y) {
    if (x >= devices->terminal_width) {
        x = devices->terminal_width - 1u;
    }
    if (y >= devices->terminal_height) {
        y = devices->terminal_height - 1u;
    }
    if (devices->terminal_cursor_x != x || devices->terminal_cursor_y != y) {
        devices->terminal_cursor_x = x;
        devices->terminal_cursor_y = y;
        emu_terminal_mark_dirty(devices);
    }
}

void emu_terminal_home(EmuDeviceBus *devices) {
    emu_terminal_set_cursor(devices, 0, 0);
}

void emu_terminal_clear_screen(EmuDeviceBus *devices) {
    uint32_t cells = emu_terminal_cell_count(devices);
    bool changed = devices->terminal_cursor_x != 0 || devices->terminal_cursor_y != 0;
    for (uint32_t i = 0; i < cells; i++) {
        if (devices->terminal_cells[i] != ' ') {
            changed = true;
        }
        devices->terminal_cells[i] = ' ';
    }
    devices->terminal_cursor_x = 0;
    devices->terminal_cursor_y = 0;
    if (changed) {
        emu_terminal_mark_dirty(devices);
    }
}

static void terminal_newline(EmuDeviceBus *devices) {
    if (devices->terminal_cursor_x != 0) {
        devices->terminal_cursor_x = 0;
        emu_terminal_mark_dirty(devices);
    }
    devices->terminal_cursor_y++;
    emu_terminal_mark_dirty(devices);
    terminal_wrap_or_scroll(devices);
}

static void terminal_carriage_return(EmuDeviceBus *devices) {
    if (devices->terminal_cursor_x != 0) {
        devices->terminal_cursor_x = 0;
        emu_terminal_mark_dirty(devices);
    }
}

static void terminal_advance_cursor(EmuDeviceBus *devices) {
    devices->terminal_cursor_x++;
    if (devices->terminal_cursor_x >= devices->terminal_width) {
        devices->terminal_cursor_x = 0;
        devices->terminal_cursor_y++;
        terminal_wrap_or_scroll(devices);
    }
    emu_terminal_mark_dirty(devices);
}

void emu_terminal_put_byte(EmuDeviceBus *devices, uint8_t value) {
    if (value == '\n') {
        terminal_newline(devices);
        return;
    }
    if (value == '\r') {
        terminal_carriage_return(devices);
        return;
    }
    uint32_t index = devices->terminal_cursor_y * devices->terminal_width + devices->terminal_cursor_x;
    if (devices->terminal_cells[index] != value) {
        devices->terminal_cells[index] = value;
        emu_terminal_mark_dirty(devices);
    }
    terminal_advance_cursor(devices);
}

bool memory_terminal_configure(Memory *memory, uint32_t width, uint32_t height, char *error, size_t error_size) {
    if (memory == NULL) {
        snprintf(error, error_size, "memory pointer is null");
        return false;
    }
    if (!emu_terminal_dimensions_valid(width, height)) {
        snprintf(error, error_size, "invalid screen size: width must be %u..%u and height must be %u..%u",
                 EMU_TERM_MIN_WIDTH, EMU_TERM_MAX_WIDTH, EMU_TERM_MIN_HEIGHT, EMU_TERM_MAX_HEIGHT);
        return false;
    }
    terminal_reset_state(&memory->devices, width, height);
    return true;
}

uint32_t memory_terminal_width(const Memory *memory) {
    return memory != NULL ? memory->devices.terminal_width : 0;
}

uint32_t memory_terminal_height(const Memory *memory) {
    return memory != NULL ? memory->devices.terminal_height : 0;
}

uint32_t memory_terminal_cursor_x(const Memory *memory) {
    return memory != NULL ? memory->devices.terminal_cursor_x : 0;
}

uint32_t memory_terminal_cursor_y(const Memory *memory) {
    return memory != NULL ? memory->devices.terminal_cursor_y : 0;
}

bool memory_terminal_dirty(const Memory *memory) {
    return memory != NULL && memory->devices.terminal_dirty;
}

const uint8_t *memory_terminal_cells(const Memory *memory) {
    return memory != NULL ? memory->devices.terminal_cells : NULL;
}

void memory_advance_frame(Memory *memory) {
    if (memory == NULL) {
        return;
    }
    memory->devices.frame_counter++;
    memory->devices.frame_ready = true;
}

uint64_t memory_frame_counter(const Memory *memory) {
    return memory != NULL ? memory->devices.frame_counter : 0;
}

bool memory_frame_ready(const Memory *memory) {
    return memory != NULL && memory->devices.frame_ready;
}

uint32_t emu_random_next(uint32_t state) {
    return state * 1664525u + 1013904223u;
}

uint32_t emu_exception_control_bits(const EmuExceptionController *exceptions) {
    if (exceptions == NULL) {
        return 0;
    }
    uint32_t bits = 0;
    if (exceptions->vector_configured) {
        bits |= EMU_EXCEPTION_CONTROL_VECTOR_ENABLE;
    }
    if (exceptions->interrupts_enabled) {
        bits |= EMU_EXCEPTION_CONTROL_INTERRUPTS_ENABLE;
    }
    if (exceptions->pending_timer_interrupt) {
        bits |= EMU_EXCEPTION_CONTROL_QUEUE_TIMER;
    }
    return bits;
}

void memory_reset_devices(Memory *memory) {
    if (memory == NULL) {
        return;
    }
    FILE *output = memory->devices.uart_output != NULL ? memory->devices.uart_output : stdout;
    emu_devices_install_default(memory);
    memory->devices.uart_output = output;
}

void memory_set_uart_output(Memory *memory, FILE *stream) {
    if (memory == NULL) {
        return;
    }
    memory->devices.uart_output = stream != NULL ? stream : stdout;
}

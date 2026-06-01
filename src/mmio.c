#include "mmio.h"

#include "devices.h"
#include "emu_util.h"

#include <inttypes.h>
#include <stdio.h>

static bool device_contains_range(const EmuDeviceRange *device, uint64_t address, uint64_t width) {
    uint64_t end = 0;
    if (device == NULL || width == 0 || !emu_checked_add_u64(address, width, &end)) {
        return false;
    }
    return address >= device->start && end <= device->start + device->size;
}

bool mmio_check_access(const EmuDeviceRange *device, uint64_t address, uint64_t width, uint8_t required,
                       char *error, size_t error_size) {
    if (device == NULL) {
        return false;
    }
    if (!device_contains_range(device, address, width)) {
        snprintf(error, error_size,
                 "device fault: access crosses device boundary: address=0x%016" PRIx64
                 " width=0x%016" PRIx64 " device=%s range=0x%016" PRIx64 "-0x%016" PRIx64,
                 address, width, device->name, device->start, device->start + device->size);
        return false;
    }
    if ((device->permissions & required) != required) {
        char have[4];
        memory_format_permissions(device->permissions, have, sizeof(have));
        snprintf(error, error_size,
                 "device fault: permission denied: address=0x%016" PRIx64 " width=0x%016" PRIx64
                 " device=%s permissions=%s",
                 address, width, device->name, have);
        return false;
    }

    return true;
}

static bool device_reject_width(const EmuDeviceRange *device, uint64_t offset, uint64_t width, const char *operation,
                                char *error, size_t error_size) {
    snprintf(error, error_size,
             "device fault: unsupported %s width: device=%s offset=0x%03" PRIx64 " width=0x%016" PRIx64,
             operation, device->name, offset, width);
    return false;
}

static bool device_reject_unaligned(const EmuDeviceRange *device, uint64_t offset, uint64_t width,
                                    const char *operation, char *error, size_t error_size) {
    snprintf(error, error_size,
             "device fault: unaligned %s access: device=%s offset=0x%03" PRIx64 " width=0x%016" PRIx64,
             operation, device->name, offset, width);
    return false;
}

static bool device_reject_readonly(const EmuDeviceRange *device, uint64_t offset, char *error, size_t error_size) {
    snprintf(error, error_size, "device fault: write to read-only register: device=%s offset=0x%03" PRIx64,
             device->name, offset);
    return false;
}

static bool device_reject_writeonly(const EmuDeviceRange *device, uint64_t offset, char *error, size_t error_size) {
    snprintf(error, error_size, "device fault: read from write-only register: device=%s offset=0x%03" PRIx64,
             device->name, offset);
    return false;
}

static bool device_reject_invalid_register(const EmuDeviceRange *device, uint64_t offset, uint64_t width,
                                           const char *operation, char *error, size_t error_size) {
    snprintf(error, error_size,
             "device fault: invalid %s register: device=%s offset=0x%03" PRIx64 " width=0x%016" PRIx64,
             operation, device->name, offset, width);
    return false;
}

static bool device_reject_if_unaligned(const EmuDeviceRange *device, uint64_t offset, uint64_t width,
                                       const char *operation, char *error, size_t error_size) {
    if (width > 1 && (offset % width) != 0) {
        return device_reject_unaligned(device, offset, width, operation, error, error_size);
    }
    return true;
}

static bool exception_vector_is_valid_for_device(const Memory *memory, uint64_t vector_base, char *error,
                                                 size_t error_size) {
    if ((vector_base & 0x3ull) != 0) {
        snprintf(error, error_size, "device fault: exception vector must be 4-byte aligned: 0x%016" PRIx64,
                 vector_base);
        return false;
    }
    if (memory_find_device(memory, vector_base) != NULL) {
        snprintf(error, error_size, "device fault: exception vector cannot point at a device range: 0x%016" PRIx64,
                 vector_base);
        return false;
    }
    if (!memory_check_execute(memory, vector_base, sizeof(uint32_t), error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "device fault: exception vector is not executable: 0x%016" PRIx64 " (%s)",
                 vector_base, detail);
        return false;
    }
    return true;
}

bool mmio_read(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width, uint64_t *out,
                        char *error, size_t error_size) {
    uint64_t offset = address - device->start;

    if (!mmio_check_access(device, address, width, EMU_MAP_READ, error, error_size)) {
        return false;
    }

    switch (device->kind) {
    case EMU_DEVICE_UART:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (offset == EMU_UART_DATA_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        if (offset == EMU_UART_STATUS_OFFSET && width == 4) {
            *out = 0x1u;
            return true;
        }
        if (offset == EMU_UART_STATUS_OFFSET) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_TIMER:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        if (offset == EMU_TIMER_TICKS_LO_OFFSET) {
            *out = (uint32_t)(memory->devices.timer_ticks & 0xffffffffu);
            memory->devices.timer_ticks++;
            return true;
        }
        if (offset == EMU_TIMER_TICKS_HI_OFFSET) {
            *out = (uint32_t)((memory->devices.timer_ticks >> 32u) & 0xffffffffu);
            memory->devices.timer_ticks++;
            return true;
        }
        if (offset == EMU_TIMER_RESET_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_RANDOM:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        if (offset == EMU_RANDOM_VALUE_OFFSET) {
            memory->devices.random_state = emu_random_next(memory->devices.random_state);
            *out = memory->devices.random_state;
            return true;
        }
        if (offset == EMU_RANDOM_SEED_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_KEYBOARD:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        if (offset == EMU_KBD_STATUS_OFFSET) {
            *out = emu_keyboard_status_bits(&memory->devices);
            return true;
        }
        if (offset == EMU_KBD_DATA_OFFSET) {
            *out = emu_keyboard_pop(&memory->devices);
            return true;
        }
        if (offset == EMU_KBD_CONTROL_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_TERMINAL:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (offset == EMU_TERM_STATUS_OFFSET && width == 4) {
            *out = memory->devices.terminal_dirty ? EMU_TERM_STATUS_DIRTY : 0u;
            return true;
        }
        if (offset == EMU_TERM_WIDTH_OFFSET && width == 4) {
            *out = memory->devices.terminal_width;
            return true;
        }
        if (offset == EMU_TERM_HEIGHT_OFFSET && width == 4) {
            *out = memory->devices.terminal_height;
            return true;
        }
        if (offset == EMU_TERM_CURSOR_X_OFFSET && width == 4) {
            *out = memory->devices.terminal_cursor_x;
            return true;
        }
        if (offset == EMU_TERM_CURSOR_Y_OFFSET && width == 4) {
            *out = memory->devices.terminal_cursor_y;
            return true;
        }
        if (offset == EMU_TERM_INDEX_OFFSET && width == 4) {
            *out = memory->devices.terminal_index;
            return true;
        }
        if (offset == EMU_TERM_CELL_OFFSET && (width == 1 || width == 4)) {
            uint32_t index = memory->devices.terminal_index;
            *out = index < emu_terminal_cell_count(&memory->devices) ? memory->devices.terminal_cells[index] : 0u;
            return true;
        }
        if (offset == EMU_TERM_DATA_OFFSET || offset == EMU_TERM_CONTROL_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        if (offset == EMU_TERM_STATUS_OFFSET || offset == EMU_TERM_WIDTH_OFFSET || offset == EMU_TERM_HEIGHT_OFFSET ||
            offset == EMU_TERM_CURSOR_X_OFFSET || offset == EMU_TERM_CURSOR_Y_OFFSET || offset == EMU_TERM_INDEX_OFFSET ||
            offset == EMU_TERM_CELL_OFFSET) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_FRAME:
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "read", error, error_size);
        }
        if (offset == EMU_FRAME_STATUS_OFFSET) {
            *out = memory->devices.frame_ready ? EMU_FRAME_STATUS_READY : 0u;
            return true;
        }
        if (offset == EMU_FRAME_COUNTER_LO_OFFSET) {
            *out = (uint32_t)(memory->devices.frame_counter & 0xffffffffu);
            return true;
        }
        if (offset == EMU_FRAME_COUNTER_HI_OFFSET) {
            *out = (uint32_t)((memory->devices.frame_counter >> 32u) & 0xffffffffu);
            return true;
        }
        if (offset == EMU_FRAME_CONTROL_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);

    case EMU_DEVICE_EXCEPTION: {
        EmuExceptionController *exceptions = memory->devices.exceptions;
        if (exceptions == NULL) {
            snprintf(error, error_size, "device fault: exception controller is not connected");
            return false;
        }
        if (!device_reject_if_unaligned(device, offset, width, "read", error, error_size)) {
            return false;
        }
        if (offset == EMU_EXCEPTION_VECTOR_OFFSET && width == 8) {
            *out = exceptions->vector_base;
            return true;
        }
        if (offset == EMU_EXCEPTION_CONTROL_OFFSET && width == 4) {
            *out = emu_exception_control_bits(exceptions);
            return true;
        }
        if (offset == EMU_EXCEPTION_TIMER_INTERVAL_OFFSET && width == 8) {
            *out = exceptions->timer_interval;
            return true;
        }
        if (offset == EMU_EXCEPTION_PENDING_OFFSET && width == 4) {
            *out = exceptions->pending_timer_interrupt ? 1u : 0u;
            return true;
        }
        if (offset == EMU_EXCEPTION_CAUSE_OFFSET && width == 4) {
            *out = (uint32_t)exceptions->context.cause;
            return true;
        }
        if (offset == EMU_EXCEPTION_FAULT_ADDRESS_OFFSET && width == 8) {
            *out = exceptions->context.fault_address;
            return true;
        }
        if (offset == EMU_EXCEPTION_INTERRUPTED_PC_OFFSET && width == 8) {
            *out = exceptions->context.interrupted_pc;
            return true;
        }
        if (offset == EMU_EXCEPTION_RESUME_PC_OFFSET && width == 8) {
            *out = exceptions->context.resume_pc;
            return true;
        }
        if (offset == EMU_EXCEPTION_DEPTH_OFFSET && width == 4) {
            *out = exceptions->context.depth;
            return true;
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);
    }
    }

    snprintf(error, error_size, "device fault: unknown device kind");
    return false;
}

bool mmio_write(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width,
                         uint64_t value, char *error, size_t error_size) {
    uint64_t offset = address - device->start;

    if (!mmio_check_access(device, address, width, EMU_MAP_WRITE, error, error_size)) {
        return false;
    }

    switch (device->kind) {
    case EMU_DEVICE_UART:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (offset == EMU_UART_DATA_OFFSET && width == 1) {
            FILE *stream = memory->devices.uart_output != NULL ? memory->devices.uart_output : stdout;
            if (fputc((int)(value & 0xffu), stream) == EOF || fflush(stream) != 0) {
                snprintf(error, error_size, "device fault: uart write failed");
                return false;
            }
            return true;
        }
        if (offset == EMU_UART_DATA_OFFSET) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        if (offset == EMU_UART_STATUS_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_TIMER:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        if (offset == EMU_TIMER_RESET_OFFSET) {
            (void)value;
            memory->devices.timer_ticks = 0;
            return true;
        }
        if (offset == EMU_TIMER_TICKS_LO_OFFSET || offset == EMU_TIMER_TICKS_HI_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_RANDOM:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        if (offset == EMU_RANDOM_SEED_OFFSET) {
            memory->devices.random_state = (uint32_t)value;
            return true;
        }
        if (offset == EMU_RANDOM_VALUE_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_KEYBOARD:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        if (offset == EMU_KBD_CONTROL_OFFSET) {
            if (((uint32_t)value & EMU_KBD_CONTROL_CLEAR_OVERFLOW) != 0) {
                memory->devices.keyboard_overflow = false;
            }
            return true;
        }
        if (offset == EMU_KBD_STATUS_OFFSET || offset == EMU_KBD_DATA_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_TERMINAL:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (offset == EMU_TERM_DATA_OFFSET && (width == 1 || width == 4)) {
            emu_terminal_put_byte(&memory->devices, (uint8_t)value);
            return true;
        }
        if (offset == EMU_TERM_CURSOR_X_OFFSET && width == 4) {
            emu_terminal_set_cursor(&memory->devices, (uint32_t)value, memory->devices.terminal_cursor_y);
            return true;
        }
        if (offset == EMU_TERM_CURSOR_Y_OFFSET && width == 4) {
            emu_terminal_set_cursor(&memory->devices, memory->devices.terminal_cursor_x, (uint32_t)value);
            return true;
        }
        if (offset == EMU_TERM_CONTROL_OFFSET && width == 4) {
            uint32_t bits = (uint32_t)value;
            if ((bits & EMU_TERM_CONTROL_CLEAR) != 0) {
                emu_terminal_clear_screen(&memory->devices);
            }
            if ((bits & EMU_TERM_CONTROL_HOME) != 0) {
                emu_terminal_home(&memory->devices);
            }
            if ((bits & EMU_TERM_CONTROL_CLEAR_DIRTY) != 0) {
                memory->devices.terminal_dirty = false;
            }
            return true;
        }
        if (offset == EMU_TERM_INDEX_OFFSET && width == 4) {
            memory->devices.terminal_index = (uint32_t)value;
            return true;
        }
        if (offset == EMU_TERM_CELL_OFFSET && (width == 1 || width == 4)) {
            uint32_t index = memory->devices.terminal_index;
            if (index < emu_terminal_cell_count(&memory->devices)) {
                uint8_t byte = (uint8_t)value;
                if (memory->devices.terminal_cells[index] != byte) {
                    memory->devices.terminal_cells[index] = byte;
                    emu_terminal_mark_dirty(&memory->devices);
                }
            }
            return true;
        }
        if (offset == EMU_TERM_STATUS_OFFSET || offset == EMU_TERM_WIDTH_OFFSET || offset == EMU_TERM_HEIGHT_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        if (offset == EMU_TERM_DATA_OFFSET || offset == EMU_TERM_CURSOR_X_OFFSET || offset == EMU_TERM_CURSOR_Y_OFFSET ||
            offset == EMU_TERM_CONTROL_OFFSET || offset == EMU_TERM_INDEX_OFFSET || offset == EMU_TERM_CELL_OFFSET) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_FRAME:
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (width != 4) {
            return device_reject_width(device, offset, width, "write", error, error_size);
        }
        if (offset == EMU_FRAME_CONTROL_OFFSET) {
            if (((uint32_t)value & EMU_FRAME_CONTROL_CLEAR_READY) != 0u) {
                memory->devices.frame_ready = false;
            }
            return true;
        }
        if (offset == EMU_FRAME_STATUS_OFFSET || offset == EMU_FRAME_COUNTER_LO_OFFSET ||
            offset == EMU_FRAME_COUNTER_HI_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);

    case EMU_DEVICE_EXCEPTION: {
        EmuExceptionController *exceptions = memory->devices.exceptions;
        if (exceptions == NULL) {
            snprintf(error, error_size, "device fault: exception controller is not connected");
            return false;
        }
        if (!device_reject_if_unaligned(device, offset, width, "write", error, error_size)) {
            return false;
        }
        if (offset == EMU_EXCEPTION_VECTOR_OFFSET && width == 8) {
            exceptions->vector_base = value;
            return true;
        }
        if (offset == EMU_EXCEPTION_CONTROL_OFFSET && width == 4) {
            uint32_t bits = (uint32_t)value;
            if ((bits & EMU_EXCEPTION_CONTROL_VECTOR_ENABLE) != 0) {
                if (!exception_vector_is_valid_for_device(memory, exceptions->vector_base, error, error_size)) {
                    return false;
                }
                exceptions->vector_configured = true;
            } else {
                exceptions->vector_configured = false;
            }
            exceptions->interrupts_enabled = (bits & EMU_EXCEPTION_CONTROL_INTERRUPTS_ENABLE) != 0;
            if ((bits & EMU_EXCEPTION_CONTROL_QUEUE_TIMER) != 0) {
                exceptions->pending_timer_interrupt = true;
            }
            if ((bits & EMU_EXCEPTION_CONTROL_CLEAR_PENDING) != 0) {
                exceptions->pending_timer_interrupt = false;
            }
            return true;
        }
        if (offset == EMU_EXCEPTION_TIMER_INTERVAL_OFFSET && width == 8) {
            exceptions->timer_interval = value;
            exceptions->next_timer_deadline = 0;
            exceptions->timer_deadline_relative_pending = value != 0;
            exceptions->pending_timer_interrupt = false;
            return true;
        }
        if (offset == EMU_EXCEPTION_PENDING_OFFSET && width == 4) {
            exceptions->pending_timer_interrupt = value != 0;
            return true;
        }
        if (offset == EMU_EXCEPTION_CAUSE_OFFSET || offset == EMU_EXCEPTION_FAULT_ADDRESS_OFFSET ||
            offset == EMU_EXCEPTION_INTERRUPTED_PC_OFFSET || offset == EMU_EXCEPTION_RESUME_PC_OFFSET ||
            offset == EMU_EXCEPTION_DEPTH_OFFSET) {
            return device_reject_readonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "write", error, error_size);
    }
    }

    snprintf(error, error_size, "device fault: unknown device kind");
    return false;
}

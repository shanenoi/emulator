#include "emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *out = left + right;
    return true;
}

static void memory_install_default_devices(Memory *memory) {
    memory->devices.range_count = 3;
    memory->devices.ranges[0] = (EmuDeviceRange){EMU_DEVICE_UART_BASE, EMU_DEVICE_SIZE,
                                                  EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_UART, "uart"};
    memory->devices.ranges[1] = (EmuDeviceRange){EMU_DEVICE_TIMER_BASE, EMU_DEVICE_SIZE,
                                                   EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_TIMER, "timer"};
    memory->devices.ranges[2] = (EmuDeviceRange){EMU_DEVICE_RANDOM_BASE, EMU_DEVICE_SIZE,
                                                   EMU_MAP_READ | EMU_MAP_WRITE, EMU_DEVICE_RANDOM, "random"};
    memory->devices.timer_ticks = 0;
    memory->devices.random_state = 0x00c0ffeeu;
    memory->devices.uart_output = stdout;
}

static bool device_contains_range(const EmuDeviceRange *device, uint64_t address, uint64_t width) {
    uint64_t end = 0;
    if (device == NULL || width == 0 || !checked_add_u64(address, width, &end)) {
        return false;
    }
    return address >= device->start && end <= device->start + device->size;
}

static bool device_check_access(const EmuDeviceRange *device, uint64_t address, uint64_t width, uint8_t required,
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

static uint32_t random_next(uint32_t state) {
    return state * 1664525u + 1013904223u;
}

static bool device_read(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width, uint64_t *out,
                        char *error, size_t error_size) {
    uint64_t offset = address - device->start;

    if (!device_check_access(device, address, width, EMU_MAP_READ, error, error_size)) {
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
            memory->devices.random_state = random_next(memory->devices.random_state);
            *out = memory->devices.random_state;
            return true;
        }
        if (offset == EMU_RANDOM_SEED_OFFSET) {
            return device_reject_writeonly(device, offset, error, error_size);
        }
        return device_reject_invalid_register(device, offset, width, "read", error, error_size);
    }

    snprintf(error, error_size, "device fault: unknown device kind");
    return false;
}

static bool device_write(Memory *memory, const EmuDeviceRange *device, uint64_t address, uint64_t width,
                         uint64_t value, char *error, size_t error_size) {
    uint64_t offset = address - device->start;

    if (!device_check_access(device, address, width, EMU_MAP_WRITE, error, error_size)) {
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
    }

    snprintf(error, error_size, "device fault: unknown device kind");
    return false;
}

static bool check_bounds(const Memory *memory, uint64_t address, uint64_t width, EmuMemoryFaultKind *fault_kind,
                         char *error, size_t error_size) {
    if (memory == NULL || memory->bytes == NULL) {
        if (fault_kind != NULL) {
            *fault_kind = EMU_MEMORY_FAULT_BOUNDS;
        }
        snprintf(error, error_size, "memory is not initialized");
        return false;
    }

    uint64_t end = 0;
    if (!checked_add_u64(address, width, &end) || address > (uint64_t)memory->size || width > (uint64_t)memory->size ||
        end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "memory access out of bounds: address=0x%016" PRIx64 " width=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, width, memory->size);
        if (fault_kind != NULL) {
            *fault_kind = EMU_MEMORY_FAULT_BOUNDS;
        }
        return false;
    }

    return true;
}

static const char *fault_name_for_required(uint8_t required) {
    if ((required & EMU_MAP_EXEC) != 0) {
        return "execute";
    }
    if ((required & EMU_MAP_WRITE) != 0) {
        return "write";
    }
    return "read";
}

static EmuMemoryFaultKind fault_kind_for_required(uint8_t required) {
    if ((required & EMU_MAP_EXEC) != 0) {
        return EMU_MEMORY_FAULT_EXEC_PERMISSION;
    }
    if ((required & EMU_MAP_WRITE) != 0) {
        return EMU_MEMORY_FAULT_WRITE_PERMISSION;
    }
    return EMU_MEMORY_FAULT_READ_PERMISSION;
}

bool memory_check_access(const Memory *memory, uint64_t address, uint64_t width, uint8_t required,
                         EmuMemoryFaultKind *fault_kind, char *error, size_t error_size) {
    if (fault_kind != NULL) {
        *fault_kind = EMU_MEMORY_FAULT_NONE;
    }

    if ((required & EMU_MAP_EXEC) != 0) {
        const EmuDeviceRange *device = memory_find_device(memory, address);
        if (device != NULL) {
            if (fault_kind != NULL) {
                *fault_kind = EMU_MEMORY_FAULT_EXEC_PERMISSION;
            }
            snprintf(error, error_size,
                     "memory fault: execute from reserved non-executable device range: address=0x%016" PRIx64
                     " width=0x%016" PRIx64 " device=%s range=0x%016" PRIx64 "-0x%016" PRIx64,
                     address, width, device->name[0] == '\0' ? "device" : device->name, device->start,
                     device->start + device->size);
            return false;
        }
    }

    if ((required & EMU_MAP_EXEC) == 0) {
        const EmuDeviceRange *device = memory_find_device(memory, address);
        if (device != NULL) {
            if (!device_check_access(device, address, width, required, error, error_size)) {
                if (fault_kind != NULL) {
                    *fault_kind = (required & EMU_MAP_WRITE) != 0 ? EMU_MEMORY_FAULT_WRITE_PERMISSION
                                                                  : EMU_MEMORY_FAULT_READ_PERMISSION;
                }
                return false;
            }
            return true;
        }
    }

    if (!check_bounds(memory, address, width, fault_kind, error, error_size)) {
        return false;
    }

    if (!memory->permissions_enabled || width == 0) {
        return true;
    }

    uint64_t end = address + width;
    uint64_t cursor = address;
    const char *fault_name = fault_name_for_required(required);
    while (cursor < end) {
        const EmuMemoryMapping *mapping = memory_find_mapping(memory, cursor);
        if (mapping == NULL) {
            if (fault_kind != NULL) {
                *fault_kind = EMU_MEMORY_FAULT_UNMAPPED;
            }
            snprintf(error, error_size,
                     "memory fault: unmapped access: address=0x%016" PRIx64 " width=0x%016" PRIx64,
                     address, width);
            return false;
        }
        if ((mapping->permissions & required) != required) {
            if (fault_kind != NULL) {
                *fault_kind = fault_kind_for_required(required);
            }
            char have[4];
            memory_format_permissions(mapping->permissions, have, sizeof(have));
            snprintf(error, error_size,
                     "memory fault: %s permission denied: address=0x%016" PRIx64 " width=0x%016" PRIx64
                     " mapping=0x%016" PRIx64 "-0x%016" PRIx64 " permissions=%s name=%s",
                     fault_name, address, width, mapping->start, mapping->start + mapping->size, have,
                     mapping->name[0] == '\0' ? "mapping" : mapping->name);
            return false;
        }

        cursor = mapping->start + mapping->size;
    }

    return true;
}

bool memory_init(Memory *memory, size_t size, char *error, size_t error_size) {
    if (memory == NULL) {
        snprintf(error, error_size, "memory pointer is null");
        return false;
    }

    memset(memory, 0, sizeof(*memory));
    memory->bytes = calloc(size, 1);
    memory->size = size;
    if (memory->bytes == NULL) {
        snprintf(error, error_size, "failed to allocate %zu bytes of memory: %s", size, strerror(errno));
        memory->size = 0;
        return false;
    }

    /*
     * Early lessons use Memory directly as a simple flat byte array. Keep that
     * teaching path permissive until a program loader explicitly installs the
     * v1.2 page map.
     */
    memory->permissions_enabled = false;
    memory_install_default_devices(memory);
    return true;
}

void memory_free(Memory *memory) {
    if (memory == NULL) {
        return;
    }

    free(memory->bytes);
    memory->bytes = NULL;
    memory->size = 0;
    memory->mapping_count = 0;
    memory->permissions_enabled = false;
    memory->devices.range_count = 0;
}

void memory_clear_mappings(Memory *memory) {
    if (memory == NULL) {
        return;
    }
    memory->mapping_count = 0;
    memset(memory->mappings, 0, sizeof(memory->mappings));
    memory->permissions_enabled = true;
}

void memory_reset_devices(Memory *memory) {
    if (memory == NULL) {
        return;
    }
    FILE *output = memory->devices.uart_output != NULL ? memory->devices.uart_output : stdout;
    memory_install_default_devices(memory);
    memory->devices.uart_output = output;
}

void memory_set_uart_output(Memory *memory, FILE *stream) {
    if (memory == NULL) {
        return;
    }
    memory->devices.uart_output = stream != NULL ? stream : stdout;
}

bool memory_map_range(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name,
                      char *error, size_t error_size) {
    if (memory == NULL || memory->bytes == NULL) {
        snprintf(error, error_size, "memory is not initialized");
        return false;
    }
    if (length == 0) {
        snprintf(error, error_size, "memory map error: zero-length mapping is invalid");
        return false;
    }
    if ((permissions & (EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC)) == 0) {
        snprintf(error, error_size, "memory map error: mapping must have at least one permission");
        return false;
    }
    if ((address & ((uint64_t)EMU_PAGE_SIZE - 1ull)) != 0) {
        snprintf(error, error_size,
                 "memory map error: mapping base must be page-aligned: address=0x%016" PRIx64
                 " page_size=0x%04x",
                 address, EMU_PAGE_SIZE);
        return false;
    }
    if ((length & ((uint64_t)EMU_PAGE_SIZE - 1ull)) != 0) {
        snprintf(error, error_size,
                 "memory map error: mapping length must be page-sized: length=0x%016" PRIx64
                 " page_size=0x%04x",
                 length, EMU_PAGE_SIZE);
        return false;
    }

    uint64_t end = 0;
    if (!checked_add_u64(address, length, &end)) {
        snprintf(error, error_size,
                 "memory map error: mapping outside memory: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }

    for (size_t i = 0; i < memory->devices.range_count; i++) {
        const EmuDeviceRange *device = &memory->devices.ranges[i];
        uint64_t device_end = device->start + device->size;
        if (address < device_end && device->start < end) {
            snprintf(error, error_size,
                     "memory map error: mapping overlaps reserved device range: 0x%016" PRIx64
                     "-0x%016" PRIx64 " overlaps %s 0x%016" PRIx64 "-0x%016" PRIx64,
                     address, end, device->name[0] == '\0' ? "device" : device->name, device->start, device_end);
            return false;
        }
    }

    if (end > (uint64_t)memory->size) {
        snprintf(error, error_size,
                 "memory map error: mapping outside memory: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx",
                 address, length, memory->size);
        return false;
    }

    if (memory->mapping_count >= EMU_MAX_MEMORY_MAPPINGS) {
        snprintf(error, error_size, "memory map error: too many mappings; max=%u", EMU_MAX_MEMORY_MAPPINGS);
        return false;
    }

    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *existing = &memory->mappings[i];
        if (address < existing->start + existing->size && existing->start < end) {
            snprintf(error, error_size,
                     "memory map error: overlapping mappings are not allowed: 0x%016" PRIx64
                     "-0x%016" PRIx64 " overlaps 0x%016" PRIx64 "-0x%016" PRIx64 " name=%s",
                     address, end, existing->start, existing->start + existing->size,
                     existing->name[0] == '\0' ? "mapping" : existing->name);
            return false;
        }
    }

    EmuMemoryMapping *mapping = &memory->mappings[memory->mapping_count++];
    mapping->start = address;
    mapping->size = length;
    mapping->permissions = permissions;
    snprintf(mapping->name, sizeof(mapping->name), "%s", name != NULL && name[0] != '\0' ? name : "mapping");
    memory->permissions_enabled = true;
    return true;
}

bool memory_map_stack(Memory *memory, uint64_t stack_top, uint64_t stack_size, char *error, size_t error_size) {
    uint64_t guard_size = (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE;
    if (stack_size == 0 || (stack_size & ((uint64_t)EMU_PAGE_SIZE - 1ull)) != 0) {
        snprintf(error, error_size, "memory map error: stack size must be a nonzero multiple of page size");
        return false;
    }
    if (stack_top < stack_size || stack_top - stack_size < guard_size) {
        snprintf(error, error_size,
                 "memory map error: stack and guard page do not fit: stack_top=0x%016" PRIx64
                 " stack_size=0x%016" PRIx64,
                 stack_top, stack_size);
        return false;
    }
    return memory_map_range(memory, stack_top - stack_size, stack_size, EMU_MAP_READ | EMU_MAP_WRITE, "stack", error,
                            error_size);
}

bool memory_check_read(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return memory_check_access(memory, address, length, EMU_MAP_READ, NULL, error, error_size);
}

bool memory_check_write(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return memory_check_access(memory, address, length, EMU_MAP_WRITE, NULL, error, error_size);
}

bool memory_check_execute(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size) {
    return memory_check_access(memory, address, length, EMU_MAP_EXEC, NULL, error, error_size);
}

bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        uint64_t value = 0;
        if (!device_read((Memory *)memory, device, address, 1, &value, error, error_size)) {
            return false;
        }
        *out = (uint8_t)value;
        return true;
    }
    if (!memory_check_read(memory, address, 1, error, error_size)) {
        return false;
    }
    *out = memory->bytes[address];
    return true;
}

bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        return device_write(memory, device, address, 1, value, error, error_size);
    }
    if (!memory_check_write(memory, address, 1, error, error_size)) {
        return false;
    }
    memory->bytes[address] = value;
    return true;
}

bool memory_read16(const Memory *memory, uint64_t address, uint16_t *out, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        uint64_t value = 0;
        if (!device_read((Memory *)memory, device, address, 2, &value, error, error_size)) {
            return false;
        }
        *out = (uint16_t)value;
        return true;
    }
    if (!memory_check_read(memory, address, 2, error, error_size)) {
        return false;
    }
    *out = (uint16_t)((uint16_t)memory->bytes[address] | ((uint16_t)memory->bytes[address + 1u] << 8u));
    return true;
}

bool memory_write16(Memory *memory, uint64_t address, uint16_t value, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        return device_write(memory, device, address, 2, value, error, error_size);
    }
    if (!memory_check_write(memory, address, 2, error, error_size)) {
        return false;
    }

    memory->bytes[address] = (uint8_t)(value & 0xffu);
    memory->bytes[address + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    return true;
}

bool memory_read32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        uint64_t value = 0;
        if (!device_read((Memory *)memory, device, address, 4, &value, error, error_size)) {
            return false;
        }
        *out = (uint32_t)value;
        return true;
    }
    if (!memory_check_read(memory, address, 4, error, error_size)) {
        return false;
    }

    *out = (uint32_t)memory->bytes[address] | ((uint32_t)memory->bytes[address + 1] << 8) |
           ((uint32_t)memory->bytes[address + 2] << 16) | ((uint32_t)memory->bytes[address + 3] << 24);
    return true;
}

bool memory_fetch32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size) {
    if (!memory_check_execute(memory, address, 4, error, error_size)) {
        return false;
    }

    *out = (uint32_t)memory->bytes[address] | ((uint32_t)memory->bytes[address + 1] << 8) |
           ((uint32_t)memory->bytes[address + 2] << 16) | ((uint32_t)memory->bytes[address + 3] << 24);
    return true;
}

bool memory_write32(Memory *memory, uint64_t address, uint32_t value, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        return device_write(memory, device, address, 4, value, error, error_size);
    }
    if (!memory_check_write(memory, address, 4, error, error_size)) {
        return false;
    }

    memory->bytes[address] = (uint8_t)(value & 0xffu);
    memory->bytes[address + 1] = (uint8_t)((value >> 8) & 0xffu);
    memory->bytes[address + 2] = (uint8_t)((value >> 16) & 0xffu);
    memory->bytes[address + 3] = (uint8_t)((value >> 24) & 0xffu);
    return true;
}

bool memory_read64(const Memory *memory, uint64_t address, uint64_t *out, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        return device_read((Memory *)memory, device, address, 8, out, error, error_size);
    }
    if (!memory_check_read(memory, address, 8, error, error_size)) {
        return false;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) {
        value |= ((uint64_t)memory->bytes[address + i]) << (8u * i);
    }
    *out = value;
    return true;
}

bool memory_write64(Memory *memory, uint64_t address, uint64_t value, char *error, size_t error_size) {
    const EmuDeviceRange *device = memory_find_device(memory, address);
    if (device != NULL) {
        return device_write(memory, device, address, 8, value, error, error_size);
    }
    if (!memory_check_write(memory, address, 8, error, error_size)) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        memory->bytes[address + i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
    return true;
}

void memory_format_permissions(uint8_t permissions, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%c%c%c", (permissions & EMU_MAP_READ) != 0 ? 'r' : '-',
             (permissions & EMU_MAP_WRITE) != 0 ? 'w' : '-',
             (permissions & EMU_MAP_EXEC) != 0 ? 'x' : '-');
}

const EmuMemoryMapping *memory_find_mapping(const Memory *memory, uint64_t address) {
    if (memory == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *mapping = &memory->mappings[i];
        if (address >= mapping->start && address < mapping->start + mapping->size) {
            return mapping;
        }
    }
    return NULL;
}

const EmuDeviceRange *memory_find_device(const Memory *memory, uint64_t address) {
    if (memory == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < memory->devices.range_count; i++) {
        const EmuDeviceRange *device = &memory->devices.ranges[i];
        if (address >= device->start && address < device->start + device->size) {
            return device;
        }
    }
    return NULL;
}

bool memory_find_stack_guard(const Memory *memory, uint64_t address, EmuMemoryMapping *out) {
    if (memory == NULL) {
        return false;
    }
    uint64_t guard_size = (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE;
    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *mapping = &memory->mappings[i];
        if (strcmp(mapping->name, "stack") != 0 || mapping->start < guard_size) {
            continue;
        }
        uint64_t guard_start = mapping->start - guard_size;
        if (address >= guard_start && address < mapping->start) {
            if (out != NULL) {
                out->start = guard_start;
                out->size = guard_size;
                out->permissions = 0;
                snprintf(out->name, sizeof(out->name), "stack-guard");
            }
            return true;
        }
    }
    return false;
}

void memory_print_mappings(const Memory *memory, FILE *stream) {
    fprintf(stream, "mappings: %zu\n", memory != NULL ? memory->mapping_count : 0u);
    if (memory == NULL) {
        return;
    }
    for (size_t i = 0; i < memory->mapping_count; i++) {
        const EmuMemoryMapping *mapping = &memory->mappings[i];
        char perms[4];
        memory_format_permissions(mapping->permissions, perms, sizeof(perms));
        fprintf(stream, "  [%zu] %s 0x%016" PRIx64 "-0x%016" PRIx64 " size=0x%016" PRIx64 " name=%s\n", i,
                perms, mapping->start, mapping->start + mapping->size, mapping->size,
                mapping->name[0] == '\0' ? "mapping" : mapping->name);
        if (strcmp(mapping->name, "stack") == 0 && mapping->start >= (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE) {
            uint64_t guard_size = (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE;
            fprintf(stream, "      guard --- 0x%016" PRIx64 "-0x%016" PRIx64
                            " size=0x%016" PRIx64 " name=stack-guard\n",
                    mapping->start - guard_size, mapping->start, guard_size);
        }
    }
    memory_print_devices(memory, stream);
}

void memory_print_devices(const Memory *memory, FILE *stream) {
    fprintf(stream, "devices: %zu\n", memory != NULL ? memory->devices.range_count : 0u);
    if (memory == NULL) {
        return;
    }
    for (size_t i = 0; i < memory->devices.range_count; i++) {
        const EmuDeviceRange *device = &memory->devices.ranges[i];
        char perms[4];
        memory_format_permissions(device->permissions, perms, sizeof(perms));
        fprintf(stream, "  [%zu] %s 0x%016" PRIx64 "-0x%016" PRIx64 " size=0x%016" PRIx64 " name=%s\n", i,
                perms, device->start, device->start + device->size, device->size,
                device->name[0] == '\0' ? "device" : device->name);
    }
}

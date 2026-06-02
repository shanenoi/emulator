#ifndef EMU_DEVICES_H
#define EMU_DEVICES_H

#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void emu_devices_install_default(Memory *memory);

uint32_t emu_keyboard_status_bits(const EmuDeviceBus *devices);
uint8_t emu_keyboard_pop(EmuDeviceBus *devices);

bool emu_terminal_dimensions_valid(uint32_t width, uint32_t height);
uint32_t emu_terminal_cell_count(const EmuDeviceBus *devices);
void emu_terminal_mark_dirty(EmuDeviceBus *devices);
void emu_terminal_set_cursor(EmuDeviceBus *devices, uint32_t x, uint32_t y);
void emu_terminal_home(EmuDeviceBus *devices);
void emu_terminal_clear_screen(EmuDeviceBus *devices);
void emu_terminal_put_byte(EmuDeviceBus *devices, uint8_t value);

uint32_t emu_random_next(uint32_t state);
uint32_t emu_exception_control_bits(const EmuExceptionController *exceptions);

#endif

#ifndef EMULATOR_GUEST_H
#define EMULATOR_GUEST_H

#include <stdint.h>

#define EMU_GUEST_UART_BASE 0x09000000ull
#define EMU_GUEST_KBD_BASE 0x09040000ull
#define EMU_GUEST_TERM_BASE 0x09050000ull

#define EMU_GUEST_UART_DATA 0x00ull
#define EMU_GUEST_UART_STATUS 0x04ull

#define EMU_GUEST_KBD_STATUS 0x00ull
#define EMU_GUEST_KBD_DATA 0x04ull
#define EMU_GUEST_KBD_CONTROL 0x08ull
#define EMU_GUEST_KBD_STATUS_READY 0x1u
#define EMU_GUEST_KBD_STATUS_OVERFLOW 0x2u
#define EMU_GUEST_KBD_CONTROL_CLEAR_OVERFLOW 0x1u
#define EMU_GUEST_KEY_ESC 0x1bu
#define EMU_GUEST_KEY_UP 0x80u
#define EMU_GUEST_KEY_DOWN 0x81u
#define EMU_GUEST_KEY_LEFT 0x82u
#define EMU_GUEST_KEY_RIGHT 0x83u

#define EMU_GUEST_TERM_STATUS 0x00ull
#define EMU_GUEST_TERM_WIDTH 0x04ull
#define EMU_GUEST_TERM_HEIGHT 0x08ull
#define EMU_GUEST_TERM_CURSOR_X 0x0cull
#define EMU_GUEST_TERM_CURSOR_Y 0x10ull
#define EMU_GUEST_TERM_DATA 0x14ull
#define EMU_GUEST_TERM_CONTROL 0x18ull
#define EMU_GUEST_TERM_INDEX 0x20ull
#define EMU_GUEST_TERM_CELL 0x24ull
#define EMU_GUEST_TERM_STATUS_DIRTY 0x1u
#define EMU_GUEST_TERM_CONTROL_CLEAR 0x1u
#define EMU_GUEST_TERM_CONTROL_HOME 0x2u
#define EMU_GUEST_TERM_CONTROL_CLEAR_DIRTY 0x4u

static inline uint32_t emu_guest_mmio_read32(uint64_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static inline void emu_guest_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static inline void emu_guest_mmio_write8(uint64_t address, uint8_t value) {
    *(volatile uint8_t *)(uintptr_t)address = value;
}

static inline uint32_t emu_guest_kbd_status(void) {
    return emu_guest_mmio_read32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_STATUS);
}

static inline int emu_guest_kbd_has_key(void) {
    return (emu_guest_kbd_status() & EMU_GUEST_KBD_STATUS_READY) != 0u;
}

static inline uint8_t emu_guest_kbd_read(void) {
    return (uint8_t)emu_guest_mmio_read32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_DATA);
}

static inline void emu_guest_kbd_clear_overflow(void) {
    emu_guest_mmio_write32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_CONTROL,
                           EMU_GUEST_KBD_CONTROL_CLEAR_OVERFLOW);
}

static inline void emu_guest_uart_putc(uint8_t value) {
    emu_guest_mmio_write8(EMU_GUEST_UART_BASE + EMU_GUEST_UART_DATA, value);
}

static inline uint32_t emu_guest_term_width(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_WIDTH);
}

static inline uint32_t emu_guest_term_height(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_HEIGHT);
}

static inline void emu_guest_term_clear(void) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CONTROL, EMU_GUEST_TERM_CONTROL_CLEAR);
}

static inline void emu_guest_term_home(void) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CONTROL, EMU_GUEST_TERM_CONTROL_HOME);
}

static inline void emu_guest_term_set_cursor(uint32_t x, uint32_t y) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_X, x);
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_Y, y);
}

static inline void emu_guest_term_putc(uint8_t value) {
    emu_guest_mmio_write8(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_DATA, value);
}

static inline void emu_guest_term_putc_at(uint32_t x, uint32_t y, uint8_t value) {
    uint32_t width = emu_guest_term_width();
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_INDEX, y * width + x);
    emu_guest_mmio_write8(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CELL, value);
}

static inline uint8_t emu_guest_term_read_cell(uint32_t x, uint32_t y) {
    uint32_t width = emu_guest_term_width();
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_INDEX, y * width + x);
    return (uint8_t)emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CELL);
}

#endif

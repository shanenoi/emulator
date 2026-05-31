#ifndef EMULATOR_GUEST_H
#define EMULATOR_GUEST_H

#include <stdint.h>

#define EMU_GUEST_UART_BASE 0x09000000ull
#define EMU_GUEST_KBD_BASE 0x09040000ull

#define EMU_GUEST_UART_DATA 0x00ull
#define EMU_GUEST_UART_STATUS 0x04ull

#define EMU_GUEST_KBD_STATUS 0x00ull
#define EMU_GUEST_KBD_DATA 0x04ull
#define EMU_GUEST_KBD_CONTROL 0x08ull
#define EMU_GUEST_KBD_STATUS_READY 0x1u
#define EMU_GUEST_KBD_STATUS_OVERFLOW 0x2u
#define EMU_GUEST_KBD_CONTROL_CLEAR_OVERFLOW 0x1u

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

#endif

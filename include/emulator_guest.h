#ifndef EMULATOR_GUEST_H
#define EMULATOR_GUEST_H

/*
 * Tiny freestanding guest helper API for emulator programs.
 *
 * This header is intentionally not a libc.  It provides small static-inline
 * wrappers around the emulator's stable MMIO devices and toy exit syscall.
 * It assumes only freestanding C headers and does not require malloc, printf,
 * argv/envp, interrupts, or blocking host services.
 */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* MMIO helpers/base constants                                               */
/* ------------------------------------------------------------------------- */

#define EMU_GUEST_UART_BASE 0x09000000ull
#define EMU_GUEST_TIMER_BASE 0x09010000ull
#define EMU_GUEST_RANDOM_BASE 0x09020000ull
#define EMU_GUEST_KBD_BASE 0x09040000ull
#define EMU_GUEST_TERM_BASE 0x09050000ull
#define EMU_GUEST_FRAME_BASE 0x09060000ull

#define EMU_GUEST_UART_DATA 0x00ull
#define EMU_GUEST_UART_STATUS 0x04ull
#define EMU_GUEST_UART_STATUS_WRITABLE 0x1u

#define EMU_GUEST_RANDOM_VALUE 0x00ull
#define EMU_GUEST_RANDOM_SEED 0x04ull

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

#define EMU_GUEST_FRAME_STATUS 0x00ull
#define EMU_GUEST_FRAME_COUNTER_LO 0x04ull
#define EMU_GUEST_FRAME_COUNTER_HI 0x08ull
#define EMU_GUEST_FRAME_CONTROL 0x0cull
#define EMU_GUEST_FRAME_STATUS_READY 0x1u
#define EMU_GUEST_FRAME_CONTROL_CLEAR_READY 0x1u

#define EMU_GUEST_SYSCALL_EXIT 93u

static inline uint8_t emu_guest_mmio_read8(uint64_t address) {
    return *(volatile const uint8_t *)(uintptr_t)address;
}

static inline uint32_t emu_guest_mmio_read32(uint64_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static inline void emu_guest_mmio_write8(uint64_t address, uint8_t value) {
    *(volatile uint8_t *)(uintptr_t)address = value;
}

static inline void emu_guest_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

/* ------------------------------------------------------------------------- */
/* Tiny formatting helpers                                                   */
/* ------------------------------------------------------------------------- */

static inline char emu_guest_hex_digit(uint32_t value) {
    value &= 0x0fu;
    return (char)(value < 10u ? ('0' + value) : ('a' + (value - 10u)));
}

static inline size_t emu_guest_format_u32_dec(char *buffer, size_t buffer_size, uint32_t value) {
    char tmp[10];
    size_t count = 0;
    size_t out = 0;

    if (buffer_size == 0u) {
        return 0u;
    }
    if (value == 0u) {
        buffer[0] = '0';
        return 1u;
    }
    while (value != 0u && count < sizeof(tmp)) {
        uint32_t q = value / 10u;
        uint32_t r = value - (q * 10u);
        tmp[count++] = (char)('0' + r);
        value = q;
    }
    while (count != 0u && out < buffer_size) {
        buffer[out++] = tmp[--count];
    }
    return out;
}

static inline size_t emu_guest_format_u32_hex8(char *buffer, size_t buffer_size, uint32_t value) {
    if (buffer_size == 0u) {
        return 0u;
    }
    buffer[0] = emu_guest_hex_digit(value >> 28u);
    if (buffer_size == 1u) {
        return 1u;
    }
    buffer[1] = emu_guest_hex_digit(value >> 24u);
    if (buffer_size == 2u) {
        return 2u;
    }
    buffer[2] = emu_guest_hex_digit(value >> 20u);
    if (buffer_size == 3u) {
        return 3u;
    }
    buffer[3] = emu_guest_hex_digit(value >> 16u);
    if (buffer_size == 4u) {
        return 4u;
    }
    buffer[4] = emu_guest_hex_digit(value >> 12u);
    if (buffer_size == 5u) {
        return 5u;
    }
    buffer[5] = emu_guest_hex_digit(value >> 8u);
    if (buffer_size == 6u) {
        return 6u;
    }
    buffer[6] = emu_guest_hex_digit(value >> 4u);
    if (buffer_size == 7u) {
        return 7u;
    }
    buffer[7] = emu_guest_hex_digit(value);
    return 8u;
}

/* ------------------------------------------------------------------------- */
/* UART helpers                                                              */
/* ------------------------------------------------------------------------- */

static inline uint32_t emu_guest_uart_status(void) {
    return emu_guest_mmio_read32(EMU_GUEST_UART_BASE + EMU_GUEST_UART_STATUS);
}

static inline void emu_guest_uart_putc(char c) {
    emu_guest_mmio_write8(EMU_GUEST_UART_BASE + EMU_GUEST_UART_DATA, (uint8_t)c);
}

static inline void emu_guest_uart_puts(const char *s) {
    if (s == (const char *)0) {
        return;
    }
    while (*s != '\0') {
        emu_guest_uart_putc(*s++);
    }
}

static inline void emu_guest_uart_write(const char *s, size_t length) {
    if (s == (const char *)0) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        emu_guest_uart_putc(s[i]);
    }
}

static inline void emu_guest_uart_put_u32_dec(uint32_t value) {
    char buffer[10];
    size_t length = emu_guest_format_u32_dec(buffer, sizeof(buffer), value);
    emu_guest_uart_write(buffer, length);
}

/* Fixed-width, lowercase, eight-digit hexadecimal without a 0x prefix. */
static inline void emu_guest_uart_put_u32_hex(uint32_t value) {
    char buffer[8];
    size_t length = emu_guest_format_u32_hex8(buffer, sizeof(buffer), value);
    emu_guest_uart_write(buffer, length);
}

/* ------------------------------------------------------------------------- */
/* Keyboard helpers                                                          */
/* ------------------------------------------------------------------------- */

static inline uint32_t emu_guest_key_status(void) {
    return emu_guest_mmio_read32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_STATUS);
}

static inline int emu_guest_key_available(void) {
    return (int)(emu_guest_key_status() & EMU_GUEST_KBD_STATUS_READY);
}

static inline uint8_t emu_guest_key_read(void) {
    return (uint8_t)emu_guest_mmio_read32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_DATA);
}

static inline int emu_guest_key_overflowed(void) {
    return (int)(emu_guest_key_status() & EMU_GUEST_KBD_STATUS_OVERFLOW);
}

static inline void emu_guest_key_clear_overflow(void) {
    emu_guest_mmio_write32(EMU_GUEST_KBD_BASE + EMU_GUEST_KBD_CONTROL,
                           EMU_GUEST_KBD_CONTROL_CLEAR_OVERFLOW);
}

/* Backward-compatible aliases from the first keyboard helper pass. */
static inline uint32_t emu_guest_kbd_status(void) { return emu_guest_key_status(); }
static inline int emu_guest_kbd_has_key(void) { return emu_guest_key_available(); }
static inline uint8_t emu_guest_kbd_read(void) { return emu_guest_key_read(); }
static inline void emu_guest_kbd_clear_overflow(void) { emu_guest_key_clear_overflow(); }

/* ------------------------------------------------------------------------- */
/* Terminal/screen helpers                                                   */
/* ------------------------------------------------------------------------- */

static inline uint32_t emu_guest_screen_status(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_STATUS);
}

static inline uint32_t emu_guest_screen_width(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_WIDTH);
}

static inline uint32_t emu_guest_screen_height(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_HEIGHT);
}

static inline void emu_guest_screen_clear(void) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CONTROL, EMU_GUEST_TERM_CONTROL_CLEAR);
}

static inline void emu_guest_screen_home(void) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CONTROL, EMU_GUEST_TERM_CONTROL_HOME);
}

static inline void emu_guest_screen_clear_dirty(void) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CONTROL, EMU_GUEST_TERM_CONTROL_CLEAR_DIRTY);
}

static inline void emu_guest_screen_set_cursor(uint32_t x, uint32_t y) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_X, x);
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_Y, y);
}

static inline uint32_t emu_guest_screen_cursor_x(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_X);
}

static inline uint32_t emu_guest_screen_cursor_y(void) {
    return emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CURSOR_Y);
}

static inline void emu_guest_screen_putc(char c) {
    emu_guest_mmio_write8(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_DATA, (uint8_t)c);
}

static inline void emu_guest_screen_puts(const char *s) {
    if (s == (const char *)0) {
        return;
    }
    while (*s != '\0') {
        emu_guest_screen_putc(*s++);
    }
}

static inline uint32_t emu_guest_screen_cell_index(uint32_t x, uint32_t y) {
    return (y * emu_guest_screen_width()) + x;
}

static inline void emu_guest_screen_putc_at(uint32_t x, uint32_t y, char c) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_INDEX,
                           emu_guest_screen_cell_index(x, y));
    emu_guest_mmio_write8(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CELL, (uint8_t)c);
}

static inline void emu_guest_screen_puts_at(uint32_t x, uint32_t y, const char *s) {
    if (s == (const char *)0) {
        return;
    }
    while (*s != '\0') {
        emu_guest_screen_putc_at(x, y, *s++);
        x++;
    }
}

static inline uint8_t emu_guest_screen_get_cell(uint32_t x, uint32_t y) {
    emu_guest_mmio_write32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_INDEX,
                           emu_guest_screen_cell_index(x, y));
    return (uint8_t)emu_guest_mmio_read32(EMU_GUEST_TERM_BASE + EMU_GUEST_TERM_CELL);
}

static inline void emu_guest_screen_put_u32_dec(uint32_t value) {
    char buffer[10];
    size_t length = emu_guest_format_u32_dec(buffer, sizeof(buffer), value);
    for (size_t i = 0; i < length; i++) {
        emu_guest_screen_putc(buffer[i]);
    }
}

/* Fixed-width, lowercase, eight-digit hexadecimal without a 0x prefix. */
static inline void emu_guest_screen_put_u32_hex(uint32_t value) {
    char buffer[8];
    size_t length = emu_guest_format_u32_hex8(buffer, sizeof(buffer), value);
    for (size_t i = 0; i < length; i++) {
        emu_guest_screen_putc(buffer[i]);
    }
}

/* Backward-compatible aliases from the first terminal helper pass. */
static inline uint32_t emu_guest_term_width(void) { return emu_guest_screen_width(); }
static inline uint32_t emu_guest_term_height(void) { return emu_guest_screen_height(); }
static inline void emu_guest_term_clear(void) { emu_guest_screen_clear(); }
static inline void emu_guest_term_home(void) { emu_guest_screen_home(); }
static inline void emu_guest_term_set_cursor(uint32_t x, uint32_t y) { emu_guest_screen_set_cursor(x, y); }
static inline void emu_guest_term_putc(uint8_t value) { emu_guest_screen_putc((char)value); }
static inline void emu_guest_term_putc_at(uint32_t x, uint32_t y, uint8_t value) {
    emu_guest_screen_putc_at(x, y, (char)value);
}
static inline uint8_t emu_guest_term_read_cell(uint32_t x, uint32_t y) {
    return emu_guest_screen_get_cell(x, y);
}

/* ------------------------------------------------------------------------- */
/* Frame/tick helpers                                                        */
/* ------------------------------------------------------------------------- */

static inline uint32_t emu_guest_frame_status(void) {
    return emu_guest_mmio_read32(EMU_GUEST_FRAME_BASE + EMU_GUEST_FRAME_STATUS);
}

static inline int emu_guest_frame_ready(void) {
    return (int)emu_guest_frame_status();
}

static inline uint64_t emu_guest_frame_counter(void) {
    uint32_t hi1;
    uint32_t lo;
    uint32_t hi2;
    do {
        hi1 = emu_guest_mmio_read32(EMU_GUEST_FRAME_BASE + EMU_GUEST_FRAME_COUNTER_HI);
        lo = emu_guest_mmio_read32(EMU_GUEST_FRAME_BASE + EMU_GUEST_FRAME_COUNTER_LO);
        hi2 = emu_guest_mmio_read32(EMU_GUEST_FRAME_BASE + EMU_GUEST_FRAME_COUNTER_HI);
    } while (hi1 != hi2);
    return ((uint64_t)hi2 << 32u) | lo;
}

static inline void emu_guest_frame_ack(void) {
    emu_guest_mmio_write32(EMU_GUEST_FRAME_BASE + EMU_GUEST_FRAME_CONTROL,
                           EMU_GUEST_FRAME_CONTROL_CLEAR_READY);
}

static inline uint64_t emu_guest_wait_frame(void) {
    while (emu_guest_frame_ready() == 0) {
    }
    uint64_t counter = emu_guest_frame_counter();
    emu_guest_frame_ack();
    return counter;
}

/* ------------------------------------------------------------------------- */
/* Random helpers                                                            */
/* ------------------------------------------------------------------------- */

static inline uint32_t emu_guest_random_u32(void) {
    return emu_guest_mmio_read32(EMU_GUEST_RANDOM_BASE + EMU_GUEST_RANDOM_VALUE);
}

static inline void emu_guest_random_seed(uint32_t seed) {
    emu_guest_mmio_write32(EMU_GUEST_RANDOM_BASE + EMU_GUEST_RANDOM_SEED, seed);
}

/* Tiny/simple modulo range helper; max == 0 deterministically returns 0. */
static inline uint32_t emu_guest_random_range(uint32_t max) {
    if (max == 0u) {
        return 0u;
    }
    return emu_guest_random_u32() % max;
}

/* ------------------------------------------------------------------------- */
/* Exit/syscall helpers                                                      */
/* ------------------------------------------------------------------------- */

static inline void emu_guest_exit(int code) {
#if defined(__aarch64__)
    register uint64_t x0 __asm__("x0") = (uint32_t)code;
    register uint64_t x8 __asm__("x8") = EMU_GUEST_SYSCALL_EXIT;
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
#else
    (void)code;
#endif
    for (;;) {
    }
}

#endif

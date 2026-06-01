#include "emulator_guest.h"

#include <stdint.h>

_Static_assert(EMU_GUEST_UART_BASE == 0x09000000ull, "uart base remains stable");
_Static_assert(EMU_GUEST_RANDOM_BASE == 0x09020000ull, "random base remains stable");
_Static_assert(EMU_GUEST_KBD_BASE == 0x09040000ull, "keyboard base remains stable");
_Static_assert(EMU_GUEST_TERM_BASE == 0x09050000ull, "terminal base remains stable");
_Static_assert(EMU_GUEST_FRAME_BASE == 0x09060000ull, "frame base remains stable");
_Static_assert(EMU_GUEST_KEY_ESC == 0x1bu, "escape key remains stable");
_Static_assert(EMU_GUEST_KEY_UP == 0x80u, "up key remains stable");
_Static_assert(EMU_GUEST_KEY_DOWN == 0x81u, "down key remains stable");
_Static_assert(EMU_GUEST_KEY_LEFT == 0x82u, "left key remains stable");
_Static_assert(EMU_GUEST_KEY_RIGHT == 0x83u, "right key remains stable");

int main(void) {
    char dec[10];
    char hex[8];

    if (emu_guest_format_u32_dec(dec, sizeof(dec), 0u) != 1u || dec[0] != '0') {
        return 1;
    }
    if (emu_guest_format_u32_dec(dec, sizeof(dec), 4294967295u) != 10u) {
        return 2;
    }
    if (dec[0] != '4' || dec[9] != '5') {
        return 3;
    }
    if (emu_guest_format_u32_hex8(hex, sizeof(hex), 0x1a2b3c4du) != 8u) {
        return 4;
    }
    if (hex[0] != '1' || hex[1] != 'a' || hex[7] != 'd') {
        return 5;
    }
    if (emu_guest_hex_digit(10u) != 'a' || emu_guest_hex_digit(15u) != 'f') {
        return 6;
    }
    return 0;
}

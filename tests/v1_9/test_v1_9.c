#include "emulator_guest.h"

#include <stdint.h>
#include <stdio.h>

#define EXPECT_U32_EQ(actual, expected) \
    do { \
        if ((uint32_t)(actual) != (uint32_t)(expected)) { \
            fprintf(stderr, "expected %s == 0x%02x, got 0x%02x\n", #actual, (unsigned)(expected), (unsigned)(actual)); \
            return 1; \
        } \
    } while (0)

int main(void) {
    EXPECT_U32_EQ(EMU_GUEST_KEY_ESC, 0x1bu);
    EXPECT_U32_EQ(EMU_GUEST_KEY_UP, 0x80u);
    EXPECT_U32_EQ(EMU_GUEST_KEY_DOWN, 0x81u);
    EXPECT_U32_EQ(EMU_GUEST_KEY_LEFT, 0x82u);
    EXPECT_U32_EQ(EMU_GUEST_KEY_RIGHT, 0x83u);
    return 0;
}

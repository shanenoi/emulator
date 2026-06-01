#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_11/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_guest_helpers.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "skipping guest helper C integration tests: clang or ld.lld is not available"
    exit 0
fi

cat >"$TMP/start.s" <<'EOF_START'
.text
.global _start
.extern main
_start:
    bl main
    movz x8, #93
    svc #0
EOF_START

cat >"$TMP/linker.ld" <<'EOF_LINKER'
ENTRY(_start)
SECTIONS
{
  . = 0x1000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  . = 0x3000;
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
EOF_LINKER

clang --target=aarch64-none-elf -c "$TMP/start.s" -o "$TMP/start.o"

build_guest() {
    name="$1"
    clang --target=aarch64-none-elf -Iinclude -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -Werror -c "$TMP/$name.c" -o "$TMP/$name.o"
    ld.lld -static -nostdlib -T "$TMP/linker.ld" "$TMP/start.o" "$TMP/$name.o" -o "$TMP/$name.elf"
}

cat >"$TMP/compile_only.c" <<'EOF_C'
#include "emulator_guest.h"
_Static_assert(EMU_GUEST_KEY_UP == 0x80u, "up mapping");
_Static_assert(EMU_GUEST_KEY_DOWN == 0x81u, "down mapping");
_Static_assert(EMU_GUEST_KEY_LEFT == 0x82u, "left mapping");
_Static_assert(EMU_GUEST_KEY_RIGHT == 0x83u, "right mapping");
_Static_assert(EMU_GUEST_KEY_ESC == 0x1bu, "esc mapping");
void touch_helpers(void) {
    (void)emu_guest_uart_status();
    emu_guest_uart_puts("");
    emu_guest_uart_put_u32_dec(0);
    emu_guest_uart_put_u32_hex(0);
    (void)emu_guest_key_available();
    (void)emu_guest_key_read();
    (void)emu_guest_key_overflowed();
    emu_guest_key_clear_overflow();
    (void)emu_guest_screen_width();
    (void)emu_guest_screen_height();
    emu_guest_screen_clear();
    emu_guest_screen_home();
    emu_guest_screen_set_cursor(0, 0);
    emu_guest_screen_puts("");
    emu_guest_screen_puts_at(0, 0, "");
    (void)emu_guest_screen_get_cell(0, 0);
    emu_guest_screen_put_u32_dec(0);
    emu_guest_screen_put_u32_hex(0);
    (void)emu_guest_frame_counter();
    (void)emu_guest_frame_ready();
    emu_guest_frame_ack();
    (void)emu_guest_random_u32();
    emu_guest_random_seed(1);
    (void)emu_guest_random_range(1);
}
EOF_C
clang --target=aarch64-none-elf -Iinclude -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -Werror -c "$TMP/compile_only.c" -o "$TMP/compile_only.o"

cat >"$TMP/uart_random_exit.c" <<'EOF_C'
#include "emulator_guest.h"
int main(void) {
    emu_guest_uart_puts("U=");
    emu_guest_uart_put_u32_dec(1234u);
    emu_guest_uart_putc(' ');
    emu_guest_uart_put_u32_hex(0x1a2b3c4du);
    emu_guest_random_seed(1u);
    if (emu_guest_random_u32() == 0x3c88596cu) {
        emu_guest_uart_puts(" R");
    } else {
        emu_guest_uart_puts(" BAD");
    }
    emu_guest_uart_putc('\n');
    emu_guest_exit(7);
    return 0;
}
EOF_C
build_guest uart_random_exit
set +e
./emulator run "$TMP/uart_random_exit.elf" >"$TMP/uart_random_exit.out" 2>"$TMP/uart_random_exit.err"
status=$?
set -e
[ "$status" -eq 7 ] || fail "expected exit status 7, got $status"
[ ! -s "$TMP/uart_random_exit.err" ] || fail "uart helper stderr was not empty"
printf 'U=1234 1a2b3c4d R\n' >"$TMP/uart_random_exit.expected"
cmp "$TMP/uart_random_exit.expected" "$TMP/uart_random_exit.out" || fail "uart/random helper output mismatch"

cat >"$TMP/screen_keyboard.c" <<'EOF_C'
#include "emulator_guest.h"
int main(void) {
    emu_guest_screen_clear();
    emu_guest_screen_puts_at(1u, 0u, "HI");
    emu_guest_screen_set_cursor(0u, 1u);
    emu_guest_screen_puts("K:");
    (void)emu_guest_key_available();
    emu_guest_screen_putc((char)emu_guest_key_read());
    emu_guest_screen_set_cursor(0u, 2u);
    emu_guest_screen_put_u32_dec(42u);
    emu_guest_screen_putc(' ');
    emu_guest_screen_put_u32_hex(0x2au);
    return 0;
}
EOF_C
build_guest screen_keyboard
./emulator run "$TMP/screen_keyboard.elf" --input Z --screen-size 12x3 --screen-dump --screen-border none >"$TMP/screen_keyboard.out" 2>"$TMP/screen_keyboard.err"
[ ! -s "$TMP/screen_keyboard.err" ] || fail "screen helper stderr was not empty"
python3 - <<'PY'
from pathlib import Path
expected = b" HI         \nK:Z         \n42 0000002a \n"
actual = Path('tests/v1_11/tmp/screen_keyboard.out').read_bytes()
if actual != expected:
    raise SystemExit(f'screen/keyboard helper output mismatch: {actual!r}')
PY

cat >"$TMP/frame_wait.c" <<'EOF_C'
#include "emulator_guest.h"
int main(void) {
    uint64_t frame = emu_guest_wait_frame();
    emu_guest_uart_putc((char)('0' + (uint32_t)frame));
    emu_guest_uart_putc('\n');
    return 0;
}
EOF_C
build_guest frame_wait
./emulator run "$TMP/frame_wait.elf" --frames 2 --instructions-per-frame 256 >"$TMP/frame_wait.out" 2>"$TMP/frame_wait.err"
[ ! -s "$TMP/frame_wait.err" ] || fail "frame helper stderr was not empty"
printf '1\n' >"$TMP/frame_wait.expected"
cmp "$TMP/frame_wait.expected" "$TMP/frame_wait.out" || fail "frame helper output mismatch"

./emulator help >"$TMP/help.out" 2>"$TMP/help.err"
[ ! -s "$TMP/help.err" ] || fail "help stderr was not empty"
contains README.md "emu_guest_uart_puts"
contains README.md "emu_guest_screen_puts_at"
contains README.md "emu_guest_wait_frame"
contains README.md "not a libc"

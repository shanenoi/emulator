#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_12/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_instruction_coverage.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "skipping v1.12 C integration tests: clang or ld.lld is not available"
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
    clang --target=aarch64-none-elf -Iinclude -ffreestanding -nostdlib -fno-builtin \
        -fno-stack-protector -fno-pic -fno-pie -mgeneral-regs-only -O2 \
        -Wall -Wextra -Werror -c "$TMP/$name.c" -o "$TMP/$name.o"
    ld.lld -static -nostdlib -T "$TMP/linker.ld" "$TMP/start.o" "$TMP/$name.o" -o "$TMP/$name.elf"
}

cat >"$TMP/array_struct.c" <<'EOF_C'
#include "emulator_guest.h"

struct Item {
    signed char delta;
    short bias;
    unsigned value;
};

static volatile struct Item items[4] = {
    { 3, -2, 10u },
    { -4, 5, 20u },
    { 7, -6, 30u },
    { -8, 9, 40u },
};

static unsigned mix(unsigned x, unsigned y) {
    return (x * 3u) + y;
}

int main(void) {
    unsigned start = (unsigned)emu_guest_key_read() & 3u;
    int total = 0;
    unsigned acc = 1u;

    for (unsigned n = 0; n < 4u; n++) {
        unsigned index = (start + n) & 3u;
        total += (int)items[index].delta;
        total += (int)items[n].bias;
        acc = mix(acc, items[index].value);
    }

    switch ((unsigned)total & 3u) {
    case 0u:
        acc ^= 0x11u;
        break;
    case 1u:
        acc ^= 0x22u;
        break;
    case 2u:
        acc ^= 0x44u;
        break;
    default:
        acc ^= 0x88u;
        break;
    }

    unsigned selected = (total < 0) ? acc : (acc + 7u);
    if ((selected & 1u) != 0u) {
        selected += 5u;
    }

    emu_guest_uart_puts("C=");
    emu_guest_uart_put_u32_dec(selected);
    emu_guest_uart_putc('\n');
    return 0;
}
EOF_C
build_guest array_struct
./emulator run "$TMP/array_struct.elf" --input B >"$TMP/array_struct.out" 2>"$TMP/array_struct.err"
[ ! -s "$TMP/array_struct.err" ] || fail "array_struct stderr was not empty: $(cat "$TMP/array_struct.err")"
printf 'C=1296\n' >"$TMP/array_struct.expected"
cmp "$TMP/array_struct.expected" "$TMP/array_struct.out" || fail "array/struct output mismatch"

cat >"$TMP/screen_frame.c" <<'EOF_C'
#include "emulator_guest.h"

int main(void) {
    emu_guest_screen_clear();
    emu_guest_screen_puts_at(0u, 0u, "demo");
    emu_guest_screen_set_cursor(0u, 1u);
    uint64_t frame = emu_guest_wait_frame();
    emu_guest_screen_puts("F");
    emu_guest_screen_put_u32_dec((uint32_t)frame);
    emu_guest_screen_putc(' ');
    emu_guest_screen_putc((char)emu_guest_key_read());
    return 0;
}
EOF_C
build_guest screen_frame
./emulator run "$TMP/screen_frame.elf" --input Z --frames 2 --instructions-per-frame 512 --screen-size 8x2 --screen-dump --screen-border ascii >"$TMP/screen_frame.out" 2>"$TMP/screen_frame.err"
[ ! -s "$TMP/screen_frame.err" ] || fail "screen_frame stderr was not empty: $(cat "$TMP/screen_frame.err")"
python3 - <<'PY'
from pathlib import Path
expected = b"+--------+\n|demo    |\n|F1 Z    |\n+--------+\n"
actual = Path('tests/v1_12/tmp/screen_frame.out').read_bytes()
if actual != expected:
    raise SystemExit(f'screen/frame output mismatch: {actual!r}')
PY

# The integration guests are meant to discover common compiler output. Keep the
# disassembly checks broad so they catch regressions without pinning every opcode.
llvm-objdump -d "$TMP/array_struct.elf" >"$TMP/array_struct.dis"
contains "$TMP/array_struct.dis" "ldrsb"
contains "$TMP/array_struct.dis" "ldrsh"
contains "$TMP/array_struct.dis" "csel"
contains "$TMP/array_struct.dis" "umull"
contains README.md "v1.12 Test Plan"
contains README.md "targeted ARM64 subset"
contains README.md "-mgeneral-regs-only"

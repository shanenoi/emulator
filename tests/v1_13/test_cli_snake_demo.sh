#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_13/tmp
mkdir -p "$TMP"

fail() { echo "test_cli_snake_demo.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }

if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "skipping snake demo test: clang or ld.lld is not available"
    exit 0
fi

rm -f examples/demos/snake.elf examples/demos/snake.o
make guest-demos >/dev/null
[ -x examples/demos/snake.elf ] || fail "snake.elf was not built"

./emulator run examples/demos/snake.elf --input q --frames 2 --instructions-per-frame 100000 \
    --screen-size 24x12 --screen-dump --screen-border ascii \
    >"$TMP/snake.out" 2>"$TMP/snake.err"
[ ! -s "$TMP/snake.err" ] || fail "snake stderr was not empty: $(cat "$TMP/snake.err")"

python3 - <<'PY'
from pathlib import Path
expected = b"""+------------------------+\n|Snake score:0  WASD/arro|\n|ws move, Esc quits      |\n|+----------------------+|\n||                      ||\n||                      ||\n||                      ||\n||        oo@           ||\n||                      ||\n||         *            ||\n||                      ||\n||                      ||\n|+----------------------+|\n+------------------------+\n"""
actual = Path("tests/v1_13/tmp/snake.out").read_bytes()
if actual != expected:
    raise SystemExit(f"snake screen output mismatch:\n{actual!r}")
PY

contains examples/demos/README.md "make examples/demos/snake.elf"
contains examples/demos/README.md "--interactive"
contains examples/demos/snake.c "emu_guest_wait_frame"
contains examples/demos/snake.c "emu_guest_key_available"
contains examples/demos/snake.c "emu_guest_screen_putc_at"

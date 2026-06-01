# Guest demos

This directory contains optional freestanding guest programs that exercise the
emulator's guest-facing MMIO/runtime APIs. They are examples, not emulator core
features.

## Snake

`snake.c` is a tiny terminal/screen Snake demo. It uses only
`include/emulator_guest.h` helpers for keyboard input, screen drawing, frame
pacing, deterministic random values, and exit.

Build it with:

```sh
make examples/demos/snake.elf
```

Run a deterministic screen-dump smoke test:

```sh
./emulator run examples/demos/snake.elf --input q --frames 2 \
  --instructions-per-frame 100000 --screen-size 24x12 \
  --screen-dump --screen-border ascii
```

Run interactively from a real TTY:

```sh
./emulator run examples/demos/snake.elf --interactive --fps 8 \
  --instructions-per-frame 100000 --screen-size 40x20 --screen-border ascii
```

Controls:

- `W`, `A`, `S`, `D` or arrow keys move.
- `Esc` or `q` exits.

The demo intentionally stays simple: no libc, no `printf`, no malloc, no ANSI
parsing in the guest, and no game-specific behavior in emulator internals.

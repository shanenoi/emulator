# v1.13 Test Plan — Optional Freestanding Guest Demo: Snake

This pass adds an optional guest demo under `examples/demos/`. The demo is not
an emulator core feature; it validates that the existing guest-facing keyboard,
terminal/screen, frame, random, and exit helpers can support a small interactive
program without adding game-specific behavior to the emulator internals.

## Scope

- Build `examples/demos/snake.c` as a freestanding AArch64 ELF guest.
- Keep the demo dependent only on `include/emulator_guest.h` and freestanding C.
- Run a deterministic non-TTY smoke test with scripted quit input, `--frames`,
  `--screen-size`, and `--screen-dump`.
- Document the optional interactive command for humans.

## Out of scope

- Emulator-core game logic.
- Guest ANSI parsing, colors, sprites, mouse input, or terminal resizing.
- Requiring a real TTY in automated tests.
- Full gameplay automation.

## Checks

- `make guest-demos` builds `examples/demos/snake.elf` when clang/ld.lld are
  available.
- `tests/v1_13/test_cli_snake_demo.sh` verifies the deterministic final screen
  dump for a known screen size and scripted quit input.
- README/docs mention how to run the demo interactively and deterministically.

# v1.11 Test Plan — Freestanding Guest Runtime Helpers

## Goal

Provide a tiny guest-facing helper API for emulator programs without turning the project into a hosted C runtime or libc.

The helper layer lives in `include/emulator_guest.h` and remains freestanding-friendly: no malloc, no printf, no argv/envp, no dynamic linking, and no blocking syscalls. Helpers are static inline wrappers over the existing UART, keyboard, terminal/screen, frame, random, and toy-exit contracts.

## Scope

- Organize `include/emulator_guest.h` into clear sections.
- Provide MMIO base/register constants.
- Provide UART helpers for characters, strings, decimal `uint32_t`, and fixed-width hexadecimal `uint32_t`.
- Provide keyboard helpers and stable arrow-key constants.
- Provide terminal/screen helpers for dimensions, clear/home, cursor movement, strings, positioned writes, cell reads, and number formatting.
- Provide frame helpers for counter reads, ready checks, acknowledgement, and polling wait.
- Provide random helpers for deterministic word reads, seeding, and a tiny modulo range helper.
- Provide a toy `emu_guest_exit()` helper that issues the existing exit syscall and then loops as a fallback.

## Out of Scope

- General libc.
- `printf`, `malloc`, argv/envp, hosted startup, or dynamic linking.
- Blocking input syscalls.
- Interrupt-driven input or frame waiting.
- ANSI parsing, colors, mouse input, or a terminal UI framework.
- Any specific demo/game behavior.

## Required Tests

1. Header constants compile and match the stable MMIO/key mappings.
2. Formatting helpers handle zero, decimal `uint32_t`, and fixed-width lowercase hexadecimal output.
3. A freestanding guest program uses UART helpers to print a string and numbers, then exits through `emu_guest_exit()` with an observable status.
4. A freestanding guest program uses screen helpers to clear the screen, write positioned text, print decimal/hex values, and read scripted keyboard input through the existing FIFO.
5. A freestanding guest program uses `emu_guest_wait_frame()` under deterministic `--frames` mode and observes the frame counter.
6. Random helpers compile and deterministic random reads remain observable through the existing random MMIO device.
7. Existing keyboard, terminal, frame, and interactive validation tests remain deterministic and do not require a real TTY.

## Determinism Policy

Scripted input and deterministic `--frames` runs are the required test path for guest-helper programs. Interactive mode remains optional and host-paced for human use only; it should not be required for v1.11 automated tests.

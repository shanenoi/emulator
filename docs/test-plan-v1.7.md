# v1.7 Test Plan — Deterministic Keyboard Input Device

This feature adds a deterministic guest-visible keyboard/input MMIO device for future interactive demos such as Snake. It remains a device/bus concern and does not add terminal rendering, blocking stdin syscalls, interrupts, or raw interactive terminal mode.

## MMIO map

Keyboard MMIO starts at `0x09040000` and uses the same fixed 4 KiB device range style as the UART, timer, random, and exception-controller devices.

| Register | Offset | Access | Behavior |
| --- | ---: | --- | --- |
| `KBD_STATUS` | `0x00` | read 32-bit | bit 0 means a queued input byte is available; bit 1 means overflow occurred |
| `KBD_DATA` | `0x04` | read 32-bit | returns and pops one queued byte; returns `0` when empty |
| `KBD_CONTROL` | `0x08` | write 32-bit | bit 0 clears the overflow flag |

The keyboard queue is FIFO with a fixed deterministic capacity. Incoming bytes beyond capacity are dropped and set the overflow bit.

## CLI input

`--input <text>` queues the bytes from `<text>` before guest execution. `--input-file <path>` queues bytes read from a file. Both feed the same internal queue that a later interactive terminal mode can use.

## Required coverage

- Empty keyboard: status ready bit is clear and data returns zero.
- Single byte: injected byte sets ready, data returns that byte, and ready clears after pop.
- FIFO: multiple bytes are read in injection order.
- Overflow: excess input is dropped, overflow status is set, and control bit 0 clears overflow.
- Integration: a small guest program reads keyboard MMIO data and echoes the byte to UART through scripted CLI input.

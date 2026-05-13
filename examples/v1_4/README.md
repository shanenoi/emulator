# v1.4 Examples — Exceptions, Traps, and Interrupt Skeleton

These examples are generated from `generate_exception_fixtures.py`. They are
small raw AArch64 programs that demonstrate the opt-in v1.4 teaching exception
model without relying on a cross compiler.

Device bases:

```text
UART                 = 0x09000000
Timer                = 0x09010000
Random               = 0x09020000
Exception controller = 0x09030000
```

The generated fixtures use vector address `0x1080`.

Cause names used by the examples and tests include `BREAKPOINT_OR_TRAP`,
`SVC_TRAP`, `DEVICE_FAULT`, and `TIMER_INTERRUPT`. The fixture set keeps `BRK`
as the primary explicit trap example; the v1.4 tests add a nonzero `SVC #1`
fixture to pin the `SVC_TRAP` policy while preserving `SVC #0` fake syscalls.
This is not a full ARM exception-level, GIC, or production-kernel model.

Generated fixtures:

```text
cli_handled_brk.bin        expects --exception-vector 0x1080, traps with BRK, returns with ERET
mmio_handled_brk.bin       configures the vector through MMIO, traps with BRK, returns with ERET
mmio_skip_device_fault.bin configures the vector through MMIO, catches a UART read fault, skips it, then halts
mmio_timer_once.bin        configures a deterministic timer interrupt through MMIO, handles one interrupt, disables it, then halts
```

Run them with:

```sh
make examples/v1_4/cli_handled_brk.bin
./emulator trace examples/v1_4/cli_handled_brk.bin --exception-vector 0x1080
./emulator trace examples/v1_4/mmio_handled_brk.bin
./emulator trace examples/v1_4/mmio_skip_device_fault.bin
./emulator trace examples/v1_4/mmio_timer_once.bin
```

The CLI fixture demonstrates host-side configuration:

```text
--exception-vector 0x1080 -> BRK #imm -> vector -> ERET -> next instruction
```

The MMIO fixtures demonstrate guest-side configuration:

```text
STR Xvector, [0x09030000 + 0x00]  -> set vector base
STR Wcontrol, [0x09030000 + 0x08] -> enable vector and interrupts
```

Exception entry uses the teaching ABI described in the lesson:

```text
x0 = cause code
x1 = fault address or trap immediate
x2 = interrupted pc
x3 = resume pc
```

`ERET` returns to the current value in `x3`, so a handler can skip a faulting
instruction by adding 4 to `x3` before returning. That is how
`mmio_skip_device_fault.bin` turns a read from UART DATA, which is write-only,
into a handled and skippable device fault.

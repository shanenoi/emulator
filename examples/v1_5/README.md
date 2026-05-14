# v1.5 Toy Kernel Examples

v1.5 introduces an opt-in toy-kernel profile for cooperative task scheduling.
The first development pass exposes the profile through CLI flags and public C
APIs; deterministic binary fixtures and automated tests are intentionally still
to be added.

## CLI surface

```sh
./emulator run <program> --kernel
./emulator run <program> --kernel --kernel-boot-info
./emulator trace <program> --kernel --kernel-task 0x1000 --kernel-task 0x1040
./emulator info <program> --kernel --kernel-task 0x1000
```

## Toy-kernel traps

```text
BRK #0x150   yield
BRK #0x151   task_exit(x0)
BRK #0x152   panic(x0)
BRK #0x153   console_write(x0=buffer, x1=length)
```

These traps are recognized only after `--kernel` is enabled and scheduling has
started. Outside toy-kernel mode, `BRK` keeps the v1.4 exception behavior.

## Current status

- The profile is opt-in and does not change older raw, ELF64, Mach-O, syscall,
  or exception-vector runs.
- Host-created tasks are supported through repeatable `--kernel-task` flags and
  `emulator_toy_kernel_add_task()`.
- Each task receives its own fixed 16 KiB stack.
- The scheduler is deterministic round-robin and cooperative only.
- Tests and generated fixtures are the next step.

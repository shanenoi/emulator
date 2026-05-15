# v1.5 Toy Kernel Examples

v1.5 introduces an opt-in toy-kernel profile for cooperative task scheduling.
The profile is intentionally small and deterministic: it is a teaching bridge
between v1.4 exceptions/traps and later toy OS experiments, not a production
operating system.

## CLI surface

```sh
./emulator run <program> --kernel
./emulator run <program> --kernel --kernel-boot-info
./emulator trace <program> --kernel --kernel-task 0x1040 --kernel-task 0x1080
./emulator trace <program> --kernel --timer-interrupt 8 --kernel-task 0x1040
./emulator info <program> --kernel --kernel-task 0x1040
./emulator debug <program> --kernel --kernel-task 0x1040
```

A toy-kernel image runs at the normal program entry first. It must explicitly
hand off to the scheduler with `BRK #0x154`. This keeps boot/init code visible
instead of silently jumping straight into the first task.

## Toy-kernel traps

```text
BRK #0x150   yield from the running task
BRK #0x151   task_exit(x0)
BRK #0x152   panic(x0)
BRK #0x153   console_write(x0=buffer, x1=length)
BRK #0x154   start host-configured tasks from kernel boot code
BRK #0x155   sleep current task until x0 toy-kernel timer ticks pass
```

Task traps are recognized only after `--kernel` is enabled and scheduling has
started. Boot traps are recognized while the kernel entry code is still running.
Outside toy-kernel mode, `BRK` keeps the v1.4 exception behavior.

## Current behavior

- The profile is opt-in and does not change older raw, ELF64, Mach-O, syscall,
  or exception-vector runs.
- Host-created tasks are supported through repeatable `--kernel-task` flags and
  `emulator_toy_kernel_add_task()`.
- Each task receives its own fixed 16 KiB stack.
- The scheduler is deterministic round-robin.
- A task can cooperatively yield, exit, sleep, panic, or write to the toy console.
- If no exception vector is configured, unhandled task exceptions mark only the
  current task as `FAULTED`; other ready tasks continue.
- `--timer-interrupt <interval>` increments a toy-kernel tick counter and can
  request scheduler switches at deterministic instruction-count boundaries.
- `emulator info` and debugger command `kernel` show task state, wake ticks, and
  fault metadata.

## Fixture generator

`generate_kernel_fixtures.py` writes tiny deterministic raw binaries for manual
experimentation and the v1.5 tests. Generated `.bin` files are intentionally
ignored by git; regenerate them when needed.

```sh
python3 examples/v1_5/generate_kernel_fixtures.py --output-dir /tmp/v1_5_fixtures
./emulator trace /tmp/v1_5_fixtures/two_task_yield.bin --kernel \
  --kernel-task 0x1008 --kernel-task 0x1014
```

Generated fixtures:

- `single_task_exit.bin` — kernel starts one task, task exits with status in `x0`.
- `two_task_yield.bin` — task 0 yields, task 1 exits, then task 0 resumes and exits.
- `sleep_then_exit.bin` — task 0 sleeps until a deterministic toy timer tick wakes it.
- `task_fault_then_exit.bin` — task 0 faults while task 1 still exits; CLI status is `71`.
- `eret_task_fault_then_exit.bin` — task 0 executes `ERET` outside an active exception and is isolated as `FAULTED`.
- `three_task_round_robin.bin` — three tasks demonstrate deterministic round-robin ordering and exited-task skipping.
- `kernel_panic.bin` — kernel panics before tasks start; CLI status is `70`.
- `console_write.bin` — kernel writes `KERN` through `BRK #0x153` then starts a task.
- `sleep_deadlock.bin` — one task sleeps with no runnable task left, producing a stable deadlock error.
- `infinite_task.bin` — task loops forever for instruction-limit coverage.

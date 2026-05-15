# v1.6 examples — Tiny OS Lab and Guest-Managed Tasks

These examples are generated raw AArch64 fixtures for the v1.6 Tiny OS Lab
implementation pass. They do not require an assembler or cross compiler.

Generate them with:

```sh
python3 examples/v1_6/generate_tiny_os_fixtures.py --output-dir examples/v1_6
```

Run the minimal guest-created task demo:

```sh
./emulator trace examples/v1_6/guest_create_task_exit.bin --kernel --kernel-boot-info
```

Run the mailbox demo:

```sh
./emulator run examples/v1_6/mailbox_ping_pong.bin --kernel --kernel-boot-info
```

Expected mailbox output:

```text
OK
```

Run the mixed host/guest demo from the test fixture generator:

```sh
python3 tests/fixtures/tiny_os_fixture_writer.py --output-dir tests/v1_6/tmp
./emulator trace tests/v1_6/tmp/host_guest_mixed.bin --kernel --kernel-boot-info --kernel-task 0x1024
```

The key v1.6 convention is `BRK #0x160` with `x8` as the service ID. The current
implementation supports guest task creation, task information lookup,
current-task ID lookup, task exit/yield/sleep, console write, kernel panic, and
nonblocking mailbox send/receive.

Important ABI and memory rules:

- `x0` returns either a nonnegative success value or a negative service error.
- `TASK_RECV` also returns the sender task ID in `x1` on success.
- Guest code should treat other argument registers as volatile across
  `BRK #0x160`.
- Boot-info and task descriptors are guest-readable and guest-read-only; the
  emulator refreshes them internally after state changes.
- If `--kernel-boot-info` is omitted, boot-info and descriptors are absent;
  `TASK_GET_INFO` returns `BAD_ARGUMENT` rather than an unmapped descriptor
  pointer.
- `supported_services` in boot-info is a bitmask of available service IDs.
- Explicit task stacks must be writable guest RAM and must not overlap devices,
  executable mappings, boot-info, descriptors, or another live task stack.
- Mailboxes are FIFO, fixed-size, and nonblocking. Empty receive/full send return
  `WOULD_BLOCK`; undersized receive buffers return `BAD_ARGUMENT` without
  dequeuing the message.
- Timer interrupts before scheduler handoff are counted by the toy kernel. If
  all tasks sleep with `--timer-interrupt` enabled, the scheduler idles to the
  next wake tick; without a timer, sleeping all tasks is a deadlock.

Generated `.bin` files are build artifacts and are removed by `make clean`.

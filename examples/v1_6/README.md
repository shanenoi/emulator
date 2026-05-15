# v1.6 examples — Tiny OS Lab and Guest-Managed Tasks

These examples are generated raw AArch64 fixtures for the first v1.6
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

The key v1.6 convention is `BRK #0x160` with `x8` as the service ID. The first
pass supports guest task creation, task information lookup, current-task ID,
task exit/yield/sleep, console write, kernel panic, and nonblocking mailbox
send/receive.

Generated `.bin` files are build artifacts and are removed by `make clean` once
v1.6 tests are wired in.

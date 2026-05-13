# v1.3 Examples — Memory-Mapped Devices

These examples are generated from `generate_device_fixtures.py`. They are small
raw AArch64 programs that access the fixed v1.3 device map without relying on a
cross compiler.

Device bases:

```text
UART   = 0x09000000
Timer  = 0x09010000
Random = 0x09020000
```

Generated fixtures:

```text
mmio_uart_hello.bin   writes "hello, v1.3 mmio!\n" through UART DATA
mmio_timer_read.bin   reads TIMER TICKS_LO, resets the timer, then halts
mmio_random_read.bin  seeds the random device, reads one VALUE, then halts
```

Run them with:

```sh
make examples/v1_3/mmio_uart_hello.bin
./emulator run examples/v1_3/mmio_uart_hello.bin
./emulator info examples/v1_3/mmio_uart_hello.bin
```

The UART program demonstrates the main v1.3 idea:

```text
STRB to 0x09000000 -> device write -> stdout byte
```

The timer and random fixtures are intentionally quiet and deterministic. They
are useful for tracing, debugger stepping, and tests that inspect register
results without depending on host wall-clock time or host entropy.

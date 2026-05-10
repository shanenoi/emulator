# Examples

The `examples/` directory contains small ARM64 assembly programs and debugger scripts used by the lessons and tests.

## Build examples

From the repository root:

```sh
make examples
```

This assembles every checked-in `.s` file into a raw `.bin` file. The `.bin` files are generated artifacts and are ignored by Git.

The build flow is:

```text
example.s
  -> clang --target=aarch64-none-elf
example.o
  -> llvm-objcopy -O binary -j .text
example.bin
```

The emulator v0.7 still runs **raw binary instruction bytes**. It does not load ELF, Mach-O, DWARF, or source-level debug information.

## Run an example

```sh
./emulator run examples/v0_1/add.bin
```

`run` executes the program until `HLT`, an error, or the instruction limit.

## Trace an example

```sh
./emulator trace examples/v0_1/add.bin
```

v0.7 trace lines show:

```text
trace pc=<current-pc> <address>: <raw-opcode>  <decoded-instruction>
```

Example:

```text
trace pc=0x0000000000001000 0x0000000000001000: 0xd2800040  movz x0, #0x2
```

This makes examples easier to connect back to the lesson documents.

## Print final registers only

```sh
./emulator regs examples/v0_1/add.bin
```

`regs` runs the program to completion and prints the final CPU state without the extra `halted` line used by `run`.

## Dump memory after running

```sh
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
```

`dump` executes the program first, then prints the selected memory range.

## Debug an example

```sh
./emulator debug examples/v0_1/add.bin
```

The debugger can also run script files:

```sh
./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt
```

## Run a toy-syscall example

```sh
make examples/v0_7/hello.bin
./emulator run examples/v0_7/hello.bin
```

v0.7 supports a tiny fake syscall ABI through `svc #0`:

```text
write = 64
exit = 93
```

- `x8 = 64`: `write(fd, buffer, length)` with `x0 = fd`, `x1 = guest buffer address`, and `x2 = length`.
- `x8 = 93`: `exit(status)` with `x0 = status`.
- `fd = 1` writes to host stdout; `fd = 2` writes to host stderr.

## Versioned layout

```text
examples/v0_1/    instruction sandbox examples
examples/v0_2/    branch and loop examples
examples/v0_3/    memory and stack examples
examples/v0_4/    function-call examples
examples/v0_5/    debugger script examples
examples/v0_7/    toy-syscall standalone examples
```

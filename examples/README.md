# Examples

The `examples/` directory contains small ARM64 assembly programs and debugger scripts used by the lessons and tests.

## Build examples

From the repository root:

```sh
make examples
```

This assembles the raw-binary examples, links the v0.8 ELF examples, builds the v0.9 freestanding C ELF examples, and generates the later binary fixtures. Generated `.bin`, `.o`, and `.elf` files are ignored by Git. v0.9 C examples use `clang --target=aarch64-none-elf` and `ld.lld` when available; if those tools are missing, their recipes print a skip message instead of failing. A skip message means the requested v0.9 `.elf` file was **not** produced, so install or expose the tools before running that particular example.

The build flow is:

```text
example.s
  -> clang --target=aarch64-none-elf
example.o
  -> llvm-objcopy -O binary -j .text
example.bin

v0.8 ELF example.s
  -> clang --target=aarch64-none-elf
example.o
  -> ld.lld -static -nostdlib -T examples/v0_8/linker.ld
example.elf

v0.9 freestanding C example.c
  -> clang --target=aarch64-none-elf -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0
example.o + examples/v0_9/start.o
  -> ld.lld -static -nostdlib -T examples/v0_9/linker.ld
example.elf

optional guest demo C program
  -> clang --target=aarch64-none-elf -Iinclude -ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only -O0
demo.o + examples/demos/start.o
  -> ld.lld -static -nostdlib -T examples/demos/linker.ld
demo.elf
```

The emulator supports three input formats:

```text
raw .bin files             loaded at 0x1000
ELF64 ET_EXEC/DYN files    loaded from PT_LOAD program headers
Mach-O arm64 MH_EXECUTE    loaded from supported LC_SEGMENT_64 records
```

It still does not load DWARF, source-level debug information, dynamically linked Linux programs, shared libraries, or PIE-style host-runtime dependencies.

## v1.4 exception examples

The `examples/v1_4/` directory contains generated raw fixtures for the v1.4 exception, trap, and interrupt runtime. Build and run them with:

```sh
make examples/v1_4/cli_handled_brk.bin
./emulator trace examples/v1_4/cli_handled_brk.bin --exception-vector 0x1080
./emulator trace examples/v1_4/mmio_handled_brk.bin
./emulator trace examples/v1_4/mmio_skip_device_fault.bin
./emulator trace examples/v1_4/mmio_timer_once.bin
```

## Run an example

```sh
./emulator run examples/v0_1/add.bin
```

`run` executes the program until `HLT`, an error, or the instruction limit.

## Trace an example

```sh
./emulator trace examples/v0_1/add.bin
```

Trace lines show:

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

## Run an ELF example

```sh
make examples/v0_8/hello_elf.elf
./emulator run examples/v0_8/hello_elf.elf
```

v0.8 detects ELF files by their `\x7fELF` magic bytes. For supported ELF files, the loader:

- validates the ELF64 little-endian AArch64 header,
- rejects unsupported dynamic-linking features such as `PT_INTERP`,
- loads each `PT_LOAD` segment at its guest virtual address,
- zero-fills `.bss` bytes when segment memory size is larger than file size,
- starts execution at `e_entry` instead of forcing `pc = 0x1000`.

The v0.8 examples are intentionally tiny hand-written ELF executables. They use the same supported instructions and v0.7 fake syscalls; they do not require libc.


## Run a tiny freestanding C example

```sh
make examples/v0_9/fib.elf
./emulator run examples/v0_9/fib.elf
echo $?
```

v0.9 adds a tiny C-facing runtime:

```text
_start -> main -> fake exit syscall
```

The `_start` stub lives in `examples/v0_9/start.s`. It calls C `main`, then exits through the v0.7 fake syscall ABI:

```text
x0 = main return value
x8 = 93
svc #0
```

These examples are freestanding C. They are compiled without libc, without dynamic linking, without PIE, and without normal operating-system startup files. Programs that use `printf`, `malloc`, `argv`, environment variables, or shared libraries are still out of scope. `examples/v0_9/hosted_printf.c` is included only as an unsupported counterexample and is intentionally not built by `make examples`.

## Versioned layout

```text
examples/v0_1/    instruction sandbox examples
examples/v0_2/    branch and loop examples
examples/v0_3/    memory and stack examples
examples/v0_4/    function-call examples
examples/v0_5/    debugger script examples
examples/v0_7/    toy-syscall standalone examples
examples/v0_8/    simple static ELF64 examples
examples/v0_9/    tiny freestanding C ELF examples
examples/v1_0/    release smoke manifest and stability notes
examples/v1_1/    generated Mach-O loader fixtures
examples/v1_2/    generated virtual-memory fixtures
examples/v1_3/    generated MMIO device fixtures
examples/v1_4/    generated exception/trap/interrupt fixtures
examples/demos/   optional freestanding guest demos using include/emulator_guest.h
```

`examples/v1_0/smoke_manifest.txt` is not a new program format. It is a compact checklist of representative examples that should keep working together for the stable v1.0 learning release.

## Optional guest demos

`examples/demos/` contains freestanding guest programs that are useful for
manual exploration but are not emulator core features. Build the Snake demo
with:

```sh
make examples/demos/snake.elf
```

Run a deterministic non-TTY smoke screen dump:

```sh
./emulator run examples/demos/snake.elf --input q --frames 2 \
  --instructions-per-frame 100000 --screen-size 24x12 \
  --screen-dump --screen-border ascii
```

Run it interactively from a real TTY:

```sh
make run-snake-demo
```

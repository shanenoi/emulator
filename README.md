# emulator

A tiny ARM64/AArch64 learning emulator written in C.

It runs small raw binaries, supported ELF64 executables, and a narrow Mach-O
teaching profile. It also includes a debugger, deterministic MMIO devices, a toy
kernel mode, and freestanding guest demos such as Snake.

This is not a production ARM64/Linux/macOS emulator and not a real OS. It is a
compact systems-programming project for learning instruction execution, binary
loading, memory permissions, device MMIO, traps, and debugger behavior.

## Quick start

```sh
make
./emulator help
make examples
```

Run a raw binary:

```sh
./emulator run examples/v0_1/add.bin
./emulator trace examples/v0_2/trace_loop.bin
./emulator regs examples/v0_1/add.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
```

The readable trace output includes `pc`, raw opcode, and decoded instruction text.

Run loader-backed examples:

```sh
make examples/v0_8/hello_elf.elf
./emulator run examples/v0_8/hello_elf.elf

make examples/v1_1/hello.macho
./emulator info examples/v1_1/hello.macho
./emulator run examples/v1_1/hello.macho
```

Use the scriptable debugger:

```sh
./emulator debug examples/v0_1/add.bin
printf 'break 0x1008\nrun\nregs\ncontinue\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

Run the freestanding Snake guest demo:

```sh
make run-snake-demo
./emulator run examples/demos/snake.elf --frames 8 --instructions-per-frame 20000 --screen-dump
```

## What works

- CPU execution for a practical teaching subset of AArch64 integer, branch,
  load/store, call/return, conditional-select, bitfield, multiply/divide, and
  compiler-oriented instructions, including logical-immediate forms and aliases
  such as `UXTB` and `SXTW`.
- Raw binary loading at `0x1000`.
- ELF64 AArch64 `ET_EXEC` loading with `PT_LOAD` segments, `.bss` zero-fill,
  entry validation, and page permissions.
- Tiny freestanding C examples use a small `_start` and flags such as
  `-ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0`;
  normal hosted C remains out of scope.
- Thin little-endian arm64 Mach-O `MH_EXECUTE` loading with `LC_SEGMENT_64`,
  `LC_MAIN`, bounded symbol metadata, segment permissions, and no dynamic
  linking.
- A fixed 1 MiB guest memory model with page mappings, `r--` / `rw-` / `r-x` /
  `rwx` permissions, a stack mapping, and a stack guard.
- Deterministic MMIO devices: UART `0x09000000`, timer `0x09010000`,
  pseudo-random generator `0x09020000`, exception controller `0x09030000`,
  keyboard FIFO, terminal screen buffer, and frame pacing.
- Fake `SVC #0` syscalls for `write = 64` and `exit = 93`; invalid fds return
  `-EBADF`, unknown syscalls return `-ENOSYS`, and exit uses the low 8 bits.
- Exception, `BRK`, `ERET`, timer-interrupt, and catchable memory-fault teaching
  paths.
- Optional toy-kernel mode with cooperative tasks, guest task creation,
  task descriptors, sleep/yield/exit/panic services, and fixed-size mailbox IPC
  through `BRK #0x160`.
- `include/emulator_guest.h`, a tiny freestanding guest helper API for UART,
  keyboard polling, terminal drawing, frame waiting, deterministic random reads,
  toy exit, decimal/hex formatting, and simple demo programs. Helpers include
  `emu_guest_uart_puts`, `emu_guest_screen_puts_at`, and
  `emu_guest_wait_frame`. It is not a libc.

## Common commands

```sh
./emulator run <program> [options]
./emulator trace <program> [options]
./emulator regs <program> [options]
./emulator dump <program> <address> <length> [options]
./emulator info <program> [options]
./emulator debug <program> [options]
```

Useful options include:

```text
--kernel
--exception-vector <address>
--timer-interrupt <N>
--input <text>
--input-file <path>
--screen-dump
--screen-size <WIDTH>x<HEIGHT>
--screen-border <unicode|ascii|none>
--frames <N>
--instructions-per-frame <N>
--interactive
```

Debugger commands include `help`, `run`/`r`, `step`/`s`, `continue`/`c`,
`regs`, `mem`/`x <address> <length>`, `maps`, `map <address>`, `break`/`b`,
`delete`, `breakpoints`, `trace on`, `trace off`, and `quit`/`q`.

## Project map

- `src/cpu.c` and `src/disasm.c` own CPU state, instruction decode, execution,
  and formatting.
- `src/memory.c` owns RAM, page mappings, permissions, and typed memory faults.
- `src/mmio.c` and `src/devices.c` own device address routing and stateful MMIO
  behavior.
- `src/emulator.c`, `src/exceptions.c`, `src/syscall.c`, and `src/toy_kernel.c`
  own stepping orchestration, traps, syscalls, exceptions, and toy-kernel mode.
- `src/loader_*.c` owns raw, ELF64, Mach-O, mapping policy, metadata, file I/O,
  and stack setup.
- `src/cli_*.c`, `src/debugger*.c`, `src/output_format.c`, and
  `src/terminal_ui.c` own the command line, debugger REPL, terminal rendering,
  and user-facing output.
- `include/emulator.h` is the public umbrella include. Internal code should use
  narrower subsystem headers when practical.

More detail lives in [Module Ownership and Header Boundaries](docs/module-ownership.md).

## Tests and release checks

```sh
make test
make release-check
make release-archive
```

`make test` runs the deterministic v0.1 through v1.13 regression suite.
`make release-check` adds documentation, repository hygiene, clean-artifact, and
fresh archive checks.

Optional local gates:

```sh
make test-asan
make test-ubsan
make test-cc-matrix
```

## Documentation

The detailed milestone history is kept outside the README:

- `docs/test-plan-v*.md` — versioned test plans and release criteria.
- `lessons/` — tutorial-style notes for earlier milestones.
- `examples/README.md` and `examples/*/README.md` — build/run notes for demos and
  generated fixtures.
- `docs/module-ownership.md` — subsystem boundaries after the refactor passes.

## Limits

The emulator intentionally does not support:

- dynamic linking, `PT_INTERP`, `ET_DYN`/PIE load bias, or relocations;
- No dynamic linker and no `PT_INTERP`.
- No `ET_DYN`/PIE load-bias policy.
- No relocations.
- hosted C startup, `printf`, `malloc`, libc, argv/envp, or aux vectors;
- real Linux/Darwin syscalls beyond the documented fake teaching ABI;
- real ARM MMU/page tables, production isolation, interrupts, DMA, host entropy,
  or wall-clock hardware behavior;
- floating point, SIMD, or NEON;
- normal macOS/iOS applications, fat Mach-O slice selection, `dyld`, shared
  libraries, code signing, Objective-C/Swift runtimes, or real Darwin syscalls.

## IDE setup

The repository includes `.clangd` and `compile_flags.txt` so clangd-based IDEs
see the same important flags as the Makefile, especially `-Iinclude` and
`-std=c11`.

The command-line build is the source of truth:

```sh
make clean && make test
```

## License

MIT. See [LICENSE](LICENSE).

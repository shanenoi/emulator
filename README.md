# emulator

A tiny ARM64/AArch64 emulator built in **C** and **assembly** as a hobby systems-programming project.

The goal is to grow this project version by version:

1. Start with a minimal instruction sandbox.
2. Add branches, loops, memory, and stack support.
3. Add function calls and a debugger REPL.
4. Add fake syscalls for small standalone programs.
5. Add an ELF loader for compiled ARM64 binaries.
6. Eventually support toy kernel experiments.

This project is for learning CPU emulation, binary loading, low-level debugging, and ARM64 architecture basics.

## Test Plans

- [v0.1 Test Plan — Instruction Sandbox](docs/test-plan-v0.1.md)
- [v0.2 Test Plan — Branches and Loops](docs/test-plan-v0.2.md)
- [v0.3 Test Plan — Memory and Stack](docs/test-plan-v0.3.md)
- [v0.4 Test Plan — Functions](docs/test-plan-v0.4.md)
- [v0.5 Test Plan — Debugger REPL](docs/test-plan-v0.5.md)
- [v0.6 Test Plan — Assembler-Friendly Runtime](docs/test-plan-v0.6.md)
- [v0.7 Test Plan — Toy Syscalls and Standalone Programs](docs/test-plan-v0.7.md)
- [v0.8 Test Plan — ELF64 Loader](docs/test-plan-v0.8.md)
- [v0.9 Test Plan — Tiny Freestanding C Programs](docs/test-plan-v0.9.md)
- [v1.0 Test Plan — Stable Learning Emulator](docs/test-plan-v1.0.md)
- [v1.1 Test Plan — Mach-O Loader](docs/test-plan-v1.1.md)
- [v1.2 Test Plan — Virtual Memory and Page Permissions](docs/test-plan-v1.2.md)

## Lessons

- [v0.1 Lesson — Instruction Sandbox](lessons/v0.1-instruction-sandbox.md)
- [v0.2 Lesson — Branches and Loops](lessons/v0.2-branches-and-loops.md)
- [v0.3 Lesson — Memory and Stack](lessons/v0.3-memory-and-stack.md)
- [v0.4 Lesson — Functions and Returns](lessons/v0.4-functions-and-returns.md)
- [v0.5 Lesson — Debugger REPL](lessons/v0.5-debugger-repl.md)
- [v0.6 Lesson — Readable Traces and Disassembly](lessons/v0.6-assembler-friendly-runtime.md)
- [v0.7 Lesson — SVC, Write, Exit, and Toy Syscalls](lessons/v0.7-toy-syscalls.md)
- [v0.8 Lesson — ELF64 Loader](lessons/v0.8-elf-loader.md)
- [v0.9 Lesson — Tiny Freestanding C Programs](lessons/v0.9-tiny-c-programs.md)
- [v1.0 Lesson — Stable Learning Emulator](lessons/v1.0-stable-learning-emulator.md)
- [v1.1 Lesson — Mach-O Loader](lessons/v1.1-mach-o-loader.md)
- [v1.2 Lesson — Virtual Memory and Page Permissions](lessons/v1.2-virtual-memory.md)

## Current Implementation Status

The repository currently contains the runtime implementation through **v0.9 — Tiny Freestanding C Programs**, **v1.0 — Stable Learning Emulator** release polish, the implemented/tested teaching profile for **v1.1 — Mach-O Loader**, and the implemented/tested teaching profile for **v1.2 — Virtual Memory and Page Permissions**. v1.2 adds non-overlapping loader mapping ranges, read/write/execute permission checks, stack mapping with a visible guard page, deterministic permission-fault examples, structured memory-fault categories, mapping inspection, and v1.2 unit/CLI/debugger/docs coverage.

Implemented now:

- C-based emulator core.
- Program runner CLI for raw `.bin` files, supported ELF64 executables, and the initial supported Mach-O profile:

```sh
./emulator run <program>
./emulator trace <program>
./emulator regs <program>
./emulator dump <program> <address> <length>
./emulator info <program>
./emulator debug <program>
```

- Fixed 1 MiB guest memory with v1.2 page-mapping metadata for loaded programs.
- Raw binary load address: `0x1000`.
- Raw binary initial `pc`: `0x1000`.
- ELF64 initial `pc`: the ELF header entry point.
- Mach-O initial `pc`: the `LC_MAIN` entry offset resolved through a mapped `LC_SEGMENT_64` file range.
- Initial `sp`: top of memory, currently `0x100000`; loaded programs receive a 64 KiB `rw-` stack mapping below it.
- Stable final register dump.
- Instruction execution limit to avoid accidental infinite runs.
- Supported instructions:
  - `NOP`
  - `HLT`
  - `MOVZ`
  - `ADD` immediate
  - `SUB` immediate
  - `B`
  - `B.cond`
  - `CBZ`
  - `CBNZ`
  - `CMP` immediate
  - `CMP` register, unshifted
  - `LDR` / `STR` unsigned-offset, pre-index, and post-index forms
  - `LDUR` / `STUR` signed unscaled offset forms
  - `LDP` / `STP` 64-bit pair forms for simple stack usage
  - `BL`
  - `RET`
  - `RET Xn`
  - `SVC #0` for the tiny fake-syscall runtime
  - v0.9 compiler-oriented instructions: `MOVN`, `MOVK`, `ADD`/`SUB` register and flag-setting immediate/register forms, `AND`/`ORR`/`EOR` register forms, `LSL`/`LSR`/`ASR` immediate aliases, `MUL`, `UDIV`, `SDIV`, `ADR`, `ADRP`, and byte/halfword `LDR`/`STR` forms
- Basic examples in `examples/v0_1/`.
- Branch and loop examples in `examples/v0_2/`, including:
  - forward unconditional branch
  - backward unconditional branch inside a terminating loop
  - `CBZ` / `CBNZ` conditional branches
  - `CMP` + `B.cond` conditional branches
  - `CMP` + `B.cond` loop
  - trace-mode loop
- Memory and stack examples in `examples/v0_3/`.
- Function examples in `examples/v0_4/`, including:
  - simple function call and return
  - multiple sequential calls to the same function
  - explicit `RET X30`
  - `RET Xn` through a custom register
  - nested calls with saved/restored `X30`
  - stack-frame style `X29` / `X30` save and restore
  - invalid return target handling
  - unsaved nested-call negative example
- Scriptable debugger REPL command:
  - `help`
  - `run` / `r`
  - `step` / `s`
  - `continue` / `c`
  - `regs`
  - `mem` / `x <address> <length>`
  - `maps`
  - `map <address>`
  - `break` / `b <address>`
  - `delete <breakpoint-id-or-address>`
  - `breakpoints`
  - `trace on` / `trace off`
  - `quit` / `q`
- Debugger command parsing rejects unexpected extra arguments and reports overlong input lines cleanly.
- Debugger scripts in `examples/v0_5/`:
  - `debug_add_script.txt`
  - `debug_function_script.txt`
  - `debug_memory_script.txt`
- v0.6 assembler-friendly runtime polish:
  - readable trace lines include `pc`, raw opcode, and decoded instruction text
  - CLI trace and debugger `trace on` share the same trace style
  - `regs <program>` runs a program and prints only the final register state
  - `cpu_format_instruction()` formats supported instructions for lessons and tests
  - `examples/README.md` documents how example assembly is built and used
  - memory-access runtime errors include instruction `pc` and raw opcode context
- v0.7 toy syscall runtime:
  - `x8 = 64` implements fake `write(fd, buffer, length)` for fd `1` and fd `2`
  - `x8 = 93` implements fake `exit(status)` and maps the low 8 bits to the CLI exit status
  - syscall arguments use `x0` through `x2`; return values are written to `x0`
  - invalid write fds return fake `-EBADF`; host stream write failures return fake `-EIO`; unknown syscalls return fake `-ENOSYS`
  - non-zero `SVC` immediates and invalid guest write buffers are emulator runtime errors
  - standalone examples live in `examples/v0_7/`
- v0.8 ELF64 loader:
  - loader auto-detects ELF files by `\x7fELF` magic and keeps non-ELF files on the raw-binary path
  - `emulator_load_program()` is the preferred v0.8+ loader entry point for CLI/debugger code; `load_raw_binary()` remains as the legacy raw-only helper for older behavior and tests
  - supports little-endian AArch64 `ET_EXEC` ELF64 files
  - rejects `ET_DYN`/PIE, `PT_INTERP`, wrong architecture, wrong endian, truncated headers, invalid program-header tables, invalid segment ranges, overlapping `PT_LOAD` segments, unmapped entry points, and misaligned entry points
  - loads `PT_LOAD` segments at their guest virtual addresses
  - zero-fills segment memory when `p_memsz > p_filesz`, which is how simple `.bss` works
  - records segment bounds and permissions for inspection and v1.2 page mapping
  - accepts in-bounds `PT_LOAD` segment addresses without requiring ELF page alignment; instruction fetch still requires 4-byte-aligned `pc`
  - v1.2 maps loaded ELF segments into the page-permission model and adds a mapped stack region
  - preserves raw `.bin` behavior for v0.1 through v0.7 examples
  - ELF examples live in `examples/v0_8/`
- v0.9 tiny freestanding C support:
  - examples live in `examples/v0_9/`
  - `examples/v0_9/start.s` provides a tiny `_start` that calls C `main` and exits through fake syscall `93`
  - C examples are compiled with `clang --target=aarch64-none-elf -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0` and linked as static `ET_EXEC` ELF files
  - normal hosted/libc C programs remain out of scope
- v1.0 stable learning release polish:
  - `emulator help`, `emulator --help`, and `emulator -h` print the supported command surface successfully
  - public usage now describes `<program>` as either a raw `.bin` file or a supported ELF64 `ET_EXEC` executable
  - `dump` address and length parsing accepts decimal and `0x`-prefixed hexadecimal values, and rejects signed-looking values before they can wrap into huge unsigned ranges
  - `make release-check` is a named release gate that runs v1.0 docs, repository hygiene, clean-artifact, and fresh-archive full deterministic-suite checks
  - `make release-archive` creates an archive from the current git `HEAD` and includes `.git`
  - `examples/v1_0/` documents a representative release smoke path across raw, debugger, syscall, ELF, and tiny-C examples
- v1.1 Mach-O loader:
  - loader auto-detects 64-bit Mach-O files by magic number after checking the ELF path
  - supports the initial little-endian arm64 `MH_EXECUTE` profile
  - parses and validates the Mach-O header and load-command table
  - rejects fat/universal Mach-O archives clearly and asks for a thin arm64 slice
  - maps `LC_SEGMENT_64` segments into the 1 MiB guest memory model
  - validates `LC_SEGMENT_64` section-table sizing and records section counts for inspection
  - zero-fills segment memory when `vmsize > filesize`
  - resolves `LC_MAIN` `entryoff` through mapped segment file ranges to initialize `pc`
  - validates optional `LC_SYMTAB` and `LC_DYSYMTAB` table ranges and records bounded symbol names/addresses for inspection
  - records segment names, file offsets, section counts, and permission bits for inspection and v1.2 page mapping
  - adds `emulator info <program>` for loader inspection without executing guest code
  - includes deterministic generated Mach-O examples in `examples/v1_1/`: `minimal_exit.macho`, `hello.macho`, and `zero_fill.macho`
  - includes an optional Mach-O fixture/toolchain smoke path through `make test` and `tests/v1_1/test_optional_macho_examples.sh`
  - rejects big-endian Mach-O, wrong CPU type, non-executable file types, malformed command tables, invalid segment ranges, overlapping mapped segments, missing `LC_MAIN`, unmapped/misaligned entries, and dynamic-linking commands such as `LC_LOAD_DYLINKER`, `LC_LOAD_DYLIB`, and `LC_DYLD_INFO`
  - normal dynamically linked macOS/iOS applications, `dyld`, shared libraries, Apple process setup, code signing, Objective-C/Swift runtimes, and real Darwin syscalls remain out of scope
- v1.2 virtual-memory teaching profile:
  - adds a fixed 4096-byte teaching page size
  - adds memory mappings with stable `r--`, `rw-`, `r-x`, and `rwx` permission labels
  - installs explicit loader-created mappings for raw, ELF64, and Mach-O programs
  - maps a 64 KiB `rw-` stack below the top of guest memory and prints the page below it as a `---` stack guard
  - rejects true overlapping mapping ranges while preserving adjacent byte ranges from earlier lessons
  - checks instruction fetches through execute permission and data loads/stores through read/write permission helpers
  - checks fake `write` syscall buffers through the readable-memory path
  - extends `emulator info <program>` with a `mappings:` section
  - adds debugger `maps` and `map <address>` commands for mapping inspection
  - adds deterministic generated v1.2 examples in `examples/v1_2/`
  - documents the current v1.2 behavior in `examples/v1_2/README.md` and `lessons/v1.2-virtual-memory.md`
  - includes v1.2 unit, CLI, debugger, docs, clean, and fresh-archive release tests
- Automated test suites following `docs/test-plan-v0.1.md` through `docs/test-plan-v1.0.md`:
  - v0.1 unit tests for CPU, memory, loader, fetch, and decode behavior
  - v0.1 integration tests for supported instructions and edge cases
  - v0.1 CLI tests for success, usage errors, loader errors, and decode errors
  - v0.2 unit/integration tests for branch decoding, condition checks, CMP flags, branch execution, edge cases, and acceptance programs
  - v0.2 CLI/trace tests for loop examples, trace output, usage errors, and instruction-limit failures
  - v0.3 unit/integration tests for load/store decoding, memory execution, stack write-back behavior, pair operations, register-31 semantics, edge cases, and acceptance programs
  - v0.3 CLI/memory tests for memory examples, invalid accesses, `dump`, decimal/hex dump arguments, out-of-bounds dump ranges, and trace output
  - v0.4 unit/integration tests for `BL`, `RET`, `RET Xn`, link-register behavior, nested calls, stack-frame calls, invalid return/call targets, recursion limits, and acceptance programs
  - v0.4 CLI/function tests for simple calls, sequential calls, nested calls, frame calls, invalid returns, unsaved nested-call behavior, and trace output
  - v0.5 unit/integration tests for debugger initialization, reset behavior, breakpoint helpers, stepping, continuing, runtime errors, and instruction limits
  - v0.5 CLI/debugger tests for scripted REPL workflows, command parsing, aliases, breakpoints, register/memory inspection, trace toggling, EOF handling, and acceptance scripts
  - v0.6 unit/integration tests for instruction formatting, readable disassembly text, formatting edge cases, and improved error context
  - v0.6 CLI/runtime tests for usage, examples, readable traces, `regs`, error messages, docs, lessons, and acceptance workflows
  - v0.7 unit/integration tests for `SVC` decode/formatting, fake syscall ABI, guest exit, write output, error cases, debugger stepping, and memory-boundary edge cases
  - v0.7 CLI/syscall tests for stdout, stderr, guest exit status propagation, trace ordering, dump compatibility, debugger workflows, docs, and regression commands
  - v0.8 unit/integration tests for ELF detection, header validation, program-header validation, segment loading, `.bss` zero-fill, entry/stack initialization, ELF execution, syscalls from ELF, debugger behavior, and malformed-file edge cases
  - v0.8 CLI/ELF tests for `run`, `regs`, `trace`, `dump`, `debug`, dynamic-file rejection, malformed-file errors, raw-binary compatibility, docs, and acceptance workflows
  - v0.9 unit/integration tests for compiler-oriented decode/formatting, ALU/logical/shift/multiply/divide behavior, address generation, byte/halfword memory access, `sp` handling, and instruction-limit/error context
  - v0.9 CLI/C-program tests for generated ELF fixtures, `_start -> main -> exit`, stack locals, nested calls, global data, zero-filled storage, fake syscall wrappers, hosted/libc rejection, docs, and regressions
  - optional v0.9 real-toolchain smoke tests that build and run the actual freestanding C examples when `clang` and `ld.lld` are available, and skip clearly otherwise
  - v1.0 release tests for CLI stability, docs consistency, optional release examples, repository hygiene, clean-artifact validation, and fresh-archive full deterministic-suite validation
  - v1.1 Mach-O tests for deterministic fixture generation, loader metadata, segment mapping, zero-fill behavior, entry resolution, unsupported runtime command rejection, CLI `run`/`trace`/`regs`/`dump`/`debug`/`info` behavior, docs consistency, and optional Mach-O toolchain smoke checks
  - v1.2 virtual-memory tests for page mapping, R/W/X permission enforcement, loader-created mappings, stack guard behavior, CPU fault ordering, CLI `info`/`run`/`trace`/`regs`/`dump`, debugger `maps`/`map`, docs consistency, generated fixtures, clean behavior, and fresh-archive release coverage

The full v0.1 through v1.2 deterministic test suite runs with `make test`. The release gate runs with `make release-check`; it checks docs, repository hygiene, clean-artifact behavior, and a fresh archive that runs the full deterministic suite after extraction.

## Build and Run

Build the emulator:

```sh
make
```

Print the stable command surface:

```sh
./emulator help
```

Build the example raw ARM64 binaries, v0.8 ELF demos, v0.9 freestanding C demos, and generated v1.1 Mach-O demos:

```sh
make examples
```

Run the v0.1 add demo:

```sh
./emulator run examples/v0_1/add.bin
```

Run the v0.2 countdown-loop demo:

```sh
./emulator run examples/v0_2/cbnz_countdown.bin
```

Run the same style of program with trace output:

```sh
./emulator trace examples/v0_2/trace_loop.bin
```

Trace output includes address, opcode, and decoded instruction text:

```text
trace pc=0x0000000000001000 0x0000000000001000: 0xd2800040  movz x0, #0x2
```

Print only the final register state after running a program:

```sh
./emulator regs examples/v0_1/add.bin
```

Run the v0.3 memory/stack demo:

```sh
./emulator run examples/v0_3/memory_store_load.bin
```

Dump memory after a v0.3 program runs:

```sh
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
```

Run the v0.4 simple-call demo:

```sh
./emulator run examples/v0_4/simple_call.bin
```

Start the v0.5 debugger:

```sh
./emulator debug examples/v0_1/add.bin
```

The debugger is stdin-scriptable, so it can also be used non-interactively:

```sh
printf 'break 0x1008\nrun\nregs\ncontinue\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

Run a checked-in debugger script:

```sh
./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt
./emulator debug examples/v0_4/simple_call.bin < examples/v0_5/debug_function_script.txt
./emulator debug examples/v0_3/memory_store_load.bin < examples/v0_5/debug_memory_script.txt
```

Run the v0.7 toy-syscall hello demo:

```sh
make examples/v0_7/hello.bin
./emulator run examples/v0_7/hello.bin
```

Run the v0.8 ELF hello demo:

```sh
make examples/v0_8/hello_elf.elf
./emulator run examples/v0_8/hello_elf.elf
```

The same CLI commands work for raw binaries, supported ELF64 executables, and the supported Mach-O profile:

```sh
./emulator trace examples/v0_8/hello_elf.elf
./emulator regs examples/v0_8/exit_status_elf.elf
./emulator dump examples/v0_8/hello_elf.elf 0x2000 13
```

Generate and inspect/run the v1.1 Mach-O examples:

```sh
make examples/v1_1/hello.macho
./emulator info examples/v1_1/hello.macho
./emulator run examples/v1_1/hello.macho
```

Or build and run the main demo in one command:

```sh
make run-demo
```

Run the current automated test suite:

```sh
make test
```

Run the named v1.0 release gate for the current deterministic suite:

```sh
make release-check
```

The test target builds the emulator, assembles the regression examples through v0.8, compiles the v0.1 through v0.9 C test runners, and runs all v0.1 through v1.0 CLI/release checks. The v0.9 and v1.0 CLI tests generate deterministic ELF fixtures directly, so `make test` does not require the optional freestanding-C cross toolchain.

`make release-check` checks v1.0 documentation links/status, repository hygiene, clean-artifact behavior after generating representative artifacts, and a fresh archive that runs the full deterministic suite after extraction. Use `make test` when you want to run the same deterministic suite directly in the current checkout.

The v1.0 smoke manifest in `examples/v1_0/smoke_manifest.txt` lists representative raw, debugger, syscall, ELF, and tiny-C examples to try manually.

Optional release-quality gates are available and skip clearly when unsupported:

```sh
make test-asan
make test-ubsan
make test-cc-matrix
```

These are not required for a normal local release check, but they give maintainers a standard way to run sanitizer and host-compiler checks before a handoff. The `/dev/full` host-stream failure check is treated as best-effort because not every platform exposes the same failing-stream behavior.

Create a release archive from the current git `HEAD` using the project packaging convention:

```sh
make release-archive
```

This target writes `emulator_<timestamp>.zip` and includes `.git`. It intentionally packages committed `HEAD`, so commit your changes before using it as a handoff archive.

## Known Limitations

This is a stable learning emulator, not a complete ARM64/Linux emulator. The current project intentionally does not support:

- dynamic linking or a dynamic loader,
- PIE / `ET_DYN`,
- relocations,
- normal hosted C startup,
- `printf`, `malloc`, or libc,
- real Linux syscall handling beyond the fake `write = 64` and `exit = 93` ABI,
- `argv`, `envp`, or auxiliary-vector setup,
- MMU/page tables or memory protection enforcement,
- floating point, SIMD, or NEON,
- normal macOS/iOS Mach-O applications, fat/universal Mach-O slice selection, `dyld`, shared libraries, rebasing/binding, code signing behavior, Objective-C/Swift runtimes, and real Darwin syscalls.

## IDE and Language Server Setup

The project includes two small configuration files for IDEs that use `clangd` or a Clang-based language server:

```text
.clangd
compile_flags.txt
```

Both files teach the IDE to use the same important compile flags as the Makefile, especially:

```text
-Iinclude
-std=c11
```

Without these flags, an IDE may open a single `.c` file in isolation and incorrectly report errors such as:

```text
'emulator.h' file not found
Unknown type name 'bool'
Unknown type name 'Emulator'
```

Those are IDE configuration errors, not project build errors. From the repository root, the source of truth remains:

```sh
make clean && make test
```

Expected result includes:

```text
halted
x0  = 0x0000000000000002
x1  = 0x0000000000000003
x2  = 0x0000000000000005
```

## v0.1 Implementation Decisions

These decisions come from the v0.1 test plan:

- `HLT` counts as an executed instruction.
- Raw binaries are accepted even when their size is not divisible by 4.
- Instruction fetch still requires a valid 4-byte aligned instruction address.
- Misaligned `pc` is rejected.
- Output uses fixed-width hexadecimal values for registers, `sp`, `pc`, and instruction count.
- `xzr` semantics are supported by treating register index `31` as the zero register:
  - reads return `0`
  - writes are ignored
- `MOVZ` shifted immediates are supported.
- `w`-register writes are supported for implemented instructions by zero-extending to the corresponding `x` register.
- A default instruction limit prevents runaway execution.

Known v0.1 limitations:

- Branches are supported starting in v0.2.
- No load/store ARM64 instructions yet.
- No stack operations yet.
- No ELF loader until v0.8, and no Mach-O loader yet.
- No syscalls yet.
- No debugger REPL yet.

## v0.2 Implementation Decisions

These decisions come from the v0.2 test plan and current implementation pass:

- Branch offsets are decoded as signed byte offsets relative to the branch instruction address.
- Taken branch targets must be 4-byte aligned and inside emulator memory.
- Not-taken conditional branches advance `pc` by `4`.
- `CMP` updates NZCV flags and does not write a general-purpose destination register.
- Supported condition checks include the full common ARM64 condition set: `EQ`, `NE`, `CS`, `CC`, `MI`, `PL`, `VS`, `VC`, `HI`, `LS`, `GE`, `LT`, `GT`, `LE`, and `AL`.
- `CBZ` and `CBNZ` support both 64-bit `x` and 32-bit `w` forms; 32-bit forms compare only the lower 32 bits.
- Trace mode uses `./emulator trace <program>` and prints each executed `pc` before the final register dump. Starting in v0.6, each trace line also includes the raw opcode and decoded instruction text; starting in v0.8, `<program>` may be either a raw binary or supported ELF64 file.
- `ADD register` is intentionally deferred because v0.2 examples use immediate addition.
- Full sum-loop acceptance using `add x1, x1, x0` is deferred with `ADD register`; v0.2 covers the immediate-counting loop variant instead.
- `CMP` immediate supports the ARM64 immediate shift forms accepted by the decoder: unshifted and `lsl #12`.
- `CMP` register is intentionally limited to unshifted register operands in v0.2.

## v0.3 Implementation Decisions

These decisions come from the v0.3 test plan and current implementation pass:

- Memory operations use the existing flat 1 MiB byte-addressed memory.
- Register field `31` means `SP` when used as a load/store base register.
- Register field `31` still behaves as `XZR` / `WZR` when used as a load/store source or destination register.
- `LDR Wt` zero-extends the loaded 32-bit value into the corresponding `X` register.
- `STR Wt` writes only the low 32 bits of the source register.
- Unsigned-offset `LDR` / `STR` offsets are scaled by access size.
- `LDUR` / `STUR`, pre-index, and post-index offsets are signed byte offsets.
- `STP` / `LDP` are implemented for 64-bit offset, pre-index, and post-index pair forms.
- Failed loads/stores do not update destination registers or write-back bases.
- Failed stores and pair operations do not partially modify memory.
- Data load/store operations allow unaligned addresses because memory is byte-addressed; instruction fetch remains 4-byte aligned.
- Memory dump uses `./emulator dump <program> <address> <length>` and prints memory after successful program execution.
- Byte/halfword loads and stores, sign-extending loads, atomic/exclusive instructions, and unprivileged load/store variants remain out of scope.

## v0.4 Implementation Decisions

These decisions come from the v0.4 test plan and current implementation pass:

- `BL` decodes a signed 26-bit branch immediate scaled by 4 bytes.
- `BL` validates the target before changing CPU state.
- A successful `BL` writes `old_pc + 4` to `X30`, also known as the link register or `LR`.
- `RET` defaults to returning through `X30`.
- `RET Xn` is supported and returns through the selected register.
- `RET XZR` and return targets below the raw-binary load address are rejected cleanly.
- Return targets must be 4-byte aligned and must point to a fetchable instruction inside emulator memory.
- `RET` uses but does not modify `X30` by itself.
- Nested functions must save/restore `X30` manually, usually with the existing v0.3 `STP` / `LDP` stack operations.
- Trace mode prints each executed `pc`. Starting in v0.6, it also shows raw opcode and decoded instruction text. No symbolic function-name or call-stack tracing is added yet.
- `ADD register` remains deferred. v0.4 examples use `ADD` immediate so function-call behavior does not depend on a new arithmetic form.

## Planned Versions

The project evolves in small, demo-driven releases. Each version should be useful on its own and should include a small assembly or C example, a reproducible command, and updated documentation.

### v0.1 — Instruction Sandbox

**Goal:** execute tiny hand-written ARM64 instruction sequences from a raw binary.

Core features:

- CPU state:
  - general-purpose registers `x0` through `x30`
  - stack pointer `sp`
  - program counter `pc`
  - NZCV condition flags storage, even if not fully used yet
- Flat memory buffer, initially around 1 MiB.
- Raw binary loader:
  - load code at a fixed base address, such as `0x1000`
  - initialize `pc` to the load address
  - initialize `sp` to the top of memory
- Minimal fetch/decode/execute loop.
- Minimal instruction support:
  - `NOP`
  - `HLT`
  - `MOVZ`
  - `ADD` immediate
  - `SUB` immediate
- Exit-time register dump.

Demo program:

```asm
movz x0, #2
movz x1, #3
add  x2, x0, #3
hlt #0
```

Expected result:

```text
x0 = 2
x1 = 3
x2 = 5
halted
```

Definition of done:

- The emulator can load a raw `.bin` file.
- The emulator can run the demo above.
- The emulator prints final register state and instruction count.

### v0.2 — Branches and Loops

**Goal:** support loops and simple conditional control flow.

Add instruction support:

- `CMP`
- `B`
- `B.cond`
- `CBZ`
- `CBNZ`
- additional `ADD`/`SUB` register forms if needed by examples

Add behavior:

- Correct NZCV flag updates for compare/subtract operations.
- Conditional branch evaluation for common conditions:
  - `EQ`
  - `NE`
  - `LT`
  - `LE`
  - `GT`
  - `GE`
- Basic instruction tracing mode.

Demo program idea:

```asm
movz x0, #5     // counter
movz x1, #0     // loop count

loop:
add  x1, x1, #1
sub  x0, x0, #1
cbnz x0, loop

hlt #0
```

Expected result:

```text
x0 = 0
x1 = 5
```

Definition of done:

- The emulator can execute a counting loop.
- Conditional branches behave correctly for at least equality and non-equality.
- Trace mode can print each executed instruction address.
- Example coverage includes a backward unconditional branch loop and a `CMP` + `B.cond` loop.

### v0.3 — Memory and Stack

**Goal:** support memory reads/writes and basic stack usage.

Add memory features:

- `read8`, `read16`, `read32`, `read64`
- `write8`, `write16`, `write32`, `write64`
- bounds checking for every access
- readable error messages for invalid memory access

Add instruction support:

- `LDR`
- `STR`
- `LDUR`
- `STUR`
- `LDP`
- `STP`

Support addressing modes gradually:

- `[base]`
- `[base, #offset]`
- `[base, #offset]!` pre-index
- `[base], #offset` post-index

Demo program:

```asm
mov x0, #42
str x0, [sp, #-8]!
ldr x1, [sp], #8
hlt #0
```

Expected result:

```text
x1 = 42
```

Definition of done:

- Stack push/pop style addressing works.
- Invalid memory accesses stop execution with a clear emulator error.
- The CLI can dump a memory range.

### v0.4 — Functions

**Goal:** support function calls and returns.

Add instruction support:

- `BL`
- `RET`
- more branch immediate decoding as needed

Add behavior:

- Correct link register behavior through `x30`.
- Simple call tracing.
- Optional stack-frame display for common `x29`/`x30` frame patterns.

Demo program:

```asm
movz x0, #2
bl add_three
hlt #0

add_three:
add x0, x0, #3
ret
```

Expected result:

```text
x0 = 5
```

Definition of done:

- The emulator can call a function and return to the caller.
- Nested calls work when the caller saves/restores `X30` before making another call.
- Trace output can show the `pc` path through calls and returns.

### v0.5 — Debugger REPL

**Goal:** make the emulator interactive and useful for learning.

Add command:

```text
emulator debug <program.bin>
```

Debugger commands:

```text
run
step
continue
regs
mem <addr> <len>
break <addr>
delete <breakpoint>
trace on
trace off
quit
```

Debugger behavior:

- Break before executing an instruction at a breakpoint address.
- Step exactly one instruction.
- Continue until breakpoint, halt, or error.
- Print registers in a stable, readable format.
- Print memory as hex bytes.
- Reject unexpected extra command arguments with usage errors.
- Report overlong input lines cleanly without executing partial commands.
- Provide script examples in `examples/v0_5/`.

Definition of done:

- A user can load a raw binary, set a breakpoint, run, inspect registers, step, and continue.
- Breakpoints survive across `run`/`continue` in the same session.
- Debugger errors do not crash the emulator process.

### v0.6 — Assembler-Friendly Runtime

**Goal:** make examples easier to build, run, inspect, and document.

Add CLI polish:

- `emulator run <program.bin>`
- `emulator trace <program.bin>`
- `emulator debug <program.bin>`
- `emulator regs <program.bin>` final-state view

Add developer tooling:

- `Makefile` targets for examples.
- `examples/` organized by version.
- `examples/README.md` for the raw-binary example workflow.
- Better error messages with instruction address and raw opcode.
- Decoded instruction text in traces for every currently supported instruction family.
- `cpu_format_instruction()` for stable instruction formatting.

Deferred:

- Symbol map support.
- Generated listing/disassembly files.

Example trace output:

```text
trace pc=0x0000000000001000 0x0000000000001000: 0xd2800040  movz x0, #0x2
trace pc=0x0000000000001004 0x0000000000001004: 0xd2800061  movz x1, #0x3
trace pc=0x0000000000001008 0x0000000000001008: 0x91000c02  add x2, x0, #0x3
```

Definition of done:

- All examples can be built with one command.
- Trace output is readable enough to use in README demos.
- Unknown instructions report the raw opcode and address.

### v0.7 — Toy Syscalls and Standalone Programs

**Goal:** allow small raw ARM64 programs to interact with the host through a small deterministic emulator ABI.

Implemented instruction support:

- `SVC #0`

Implemented fake syscall dispatcher:

```text
x8 = syscall number
x0-x2 = arguments for write(fd, ptr, len)
x0 = return value
```

Implemented syscalls:

```text
write = 64
exit = 93
```

`write` supports fd `1` for stdout and fd `2` for stderr. Unsupported fds return fake `-EBADF` in `x0`. Host stream write or flush failures return fake `-EIO` in `x0`. Unknown syscall numbers return fake `-ENOSYS` in `x0`. Invalid guest write buffers are runtime errors because they are invalid emulated memory accesses rather than recoverable syscall failures.

Demo program idea:

```asm
mov x0, #1          // stdout
mov x1, #message    // guest address of message bytes
mov x2, #13
mov x8, #64         // write
svc #0

mov x0, #0
mov x8, #93         // exit
svc #0

message:
.ascii "hello world!\n"
```

Expected result:

```text
hello world!
```

Definition of done:

- A program can print text through `write`.
- A program can terminate through `exit`.
- Guest exit status maps to the host CLI exit status.
- Unsupported fds and syscall numbers produce documented fake errno-style return values.
- Invalid guest memory ranges produce clear runtime errors.

### v0.8 — ELF Loader

**Goal:** load real AArch64 ELF64 executable files instead of only raw binaries.

Implemented ELF support:

- Validate ELF magic.
- Validate class is ELF64.
- Validate target is AArch64.
- Validate little-endian encoding.
- Load `PT_LOAD` segments into emulator memory.
- Respect entry point from ELF header.
- Zero-fill `.bss` through segment memory size handling.
- Initialize `sp` to the same top-of-memory stack used by raw programs.
- Preserve all raw `.bin` loading behavior.
- Reject unsupported dynamic-linking features clearly.

Current limitations:

- Support static `ET_EXEC` first.
- No dynamic linker and no `PT_INTERP`.
- No libc requirement.
- No `ET_DYN`/PIE load-bias policy.
- No relocations.
- Segment permissions are represented but not enforced yet.

Example command:

```sh
make examples/v0_8/hello_elf.elf
./emulator run examples/v0_8/hello_elf.elf
```

Definition of done:

- The emulator can load a simple static AArch64 ELF.
- The emulator starts at the ELF entry point.
- Segment bounds and permissions are represented internally, even if permissions are not enforced yet.
- Raw v0.1 through v0.7 binaries still load exactly as before.

### v0.9 — Tiny Freestanding C Programs

**Goal:** run small freestanding C programs compiled to static AArch64 ELF64.

Implemented development support:

- New compiler-oriented instruction support:
  - `MOVN` / `MOVK`
  - `ADD` / `SUB` register
  - `ADD` / `SUB` immediate with correct `sp` handling for compiler stack frames
  - flag-setting `ADDS` / `SUBS` immediate/register forms used by loops
  - `AND` / `ORR` / `EOR` register
  - `LSL` / `LSR` / `ASR` immediate aliases
  - `MUL`, `UDIV`, `SDIV`
  - `ADR`, `ADRP`
  - byte and halfword `LDR` / `STR` forms
  - deliberate exclusions: logical-immediate instructions and sign-extension aliases such as `UXTB`/`UXTH`/`SXTW` are outside the selected v0.9 example profile unless added later
- A tiny `_start` assembly stub in `examples/v0_9/start.s`:
  - calls C `main` with `BL`,
  - leaves `main`'s return value in `x0`,
  - exits through fake syscall `x8 = 93`, `SVC #0`.
- Freestanding C examples:
  - `return_42.c`
  - `fib.c`
  - `sum_array.c`
  - `string_len.c`
  - `hello_c.c`
  - `nested_calls.c`
  - `stack_locals.c`
  - `byte_copy.c`
  - `static_local.c`
  - `stderr_c.c`
  - `bad_fd_c.c`
  - `unknown_syscall_c.c`
  - `invalid_write_c.c`
  - `hosted_printf.c` as an intentionally unsupported hosted/libc example that is not built by `make examples`

Documented compile profile:

```sh
clang --target=aarch64-none-elf -ffreestanding -nostdlib \
  -fno-stack-protector -fno-pic -fno-pie -O0 \
  -c examples/v0_9/fib.c -o examples/v0_9/fib.o

ld.lld -static -nostdlib -T examples/v0_9/linker.ld \
  examples/v0_9/start.o examples/v0_9/fib.o \
  -o examples/v0_9/fib.elf
```

Example command:

```sh
make examples/v0_9/fib.elf
./emulator run examples/v0_9/fib.elf
echo $?
```

Expected host exit status:

```text
55
```

Important limitation: v0.9 is still freestanding C only. It does not run normal hosted C programs that depend on libc startup, `printf`, `malloc`, dynamic linking, `argv`, environment variables, or auxiliary vectors. The repository includes `examples/v0_9/hosted_printf.c` only as a teaching counterexample; it is intentionally not built.

Build behavior: v0.9 example targets use `clang --target=aarch64-none-elf` and `ld.lld` when they are available. If those tools are missing, the v0.9 example recipes print a clear skip message instead of failing the build. Older raw/assembly examples still use the pre-existing toolchain path.

If a v0.9 example recipe prints a skip message, the requested `.elf` was not produced. Install or expose `clang` plus `ld.lld`, then rerun the `make examples/v0_9/<name>.elf` command before trying to execute that example.

Definition of done:

- Dedicated v0.9 automated tests from `docs/test-plan-v0.9.md` are included in `make test`.
- The optional real-toolchain C smoke tests run when the toolchain is available and skip clearly otherwise.
- The exact v0.9 instruction subset and edge-case behavior are documented.
- v0.1 through v0.9 behavior passes unchanged.

### v1.0 — Stable Learning Emulator

**Goal:** publish a polished first stable release for learning ARM64 emulation.

Release focus:

- Keep raw binary loading, ELF64 loading, the debugger, instruction tracing, memory dumping, fake syscalls, and freestanding C examples stable.
- Keep `make test` as the local deterministic release gate.
- Include release-level tests for CLI stability, docs consistency, examples, packaging hygiene, and common learner mistakes.
- Keep optional real-toolchain C smoke checks as pass-or-skip workflows.
- Document supported behavior and known limitations in one place.

Suggested demos:

- add two numbers
- loop and sum numbers
- stack push/pop
- function call
- recursive Fibonacci
- hello world through fake syscall
- simple string length
- simple memory copy

Definition of done:

- A new user can clone the repository, build the emulator, run examples, debug a program, and understand what is happening from the docs.
- CI or a local `make test` target verifies the supported instruction set and the v1.0 release-quality checks.

### v1.1 — Mach-O Loader

**Goal:** add Apple-focused binary-format learning without trying to boot iOS or macOS. This milestone is implemented for the documented teaching profile.

Add Mach-O support:

- Parse Mach-O arm64 headers.
- Parse `LC_SEGMENT_64`.
- Map `__TEXT` and `__DATA` segments.
- Parse entry point from `LC_MAIN` when available.
- Basic symbol/table inspection, including bounded Mach-O symbol names and addresses when an `LC_SYMTAB` is present.
- Clear unsupported-feature errors for dynamic linking and complex relocations.

Current limitations:

- Prefer simple, static or freestanding Mach-O examples.
- Do not attempt to run normal macOS/iOS dynamically linked apps.
- Treat this as a binary loader and inspection milestone, not an OS compatibility milestone.

Definition of done:

- The emulator can inspect a simple arm64 Mach-O file.
- The emulator can load and run deliberately simple Mach-O examples.
- The emulator rejects unsupported runtime features clearly.
- `make test` and `make release-check` include v1.1 deterministic coverage.

### v1.2 — Virtual Memory

**Goal:** teach page-based memory, permissions, and fault handling.

Planning reference: [v1.2 Test Plan — Virtual Memory and Page Permissions](docs/test-plan-v1.2.md). The implementation and deterministic tests for the teaching profile are in place.

Added memory model pieces:

- Fixed 4096-byte teaching pages.
- Loader-created mappings for raw, ELF64, and Mach-O inputs.
- Read/write/execute permission labels.
- A mapped `rw-` stack region below the top of guest memory.
- A visible `---` stack-guard page below the stack mapping.
- True mapping-overlap rejection, while adjacent byte ranges remain allowed.
- Deterministic generated examples in `examples/v1_2/`.
- Structured memory-fault categories used by v1.2 tests.
- Instruction fetch checks through execute permission.
- Data read/write checks through read/write permission helpers.
- `info` mapping output.
- Debugger `maps` and `map <address>` inspection commands.

Example behavior available through generated v1.2 fixtures:

```text
write to RX code page -> permission fault
execute RW data page  -> permission fault
read unmapped page    -> unmapped memory fault
stack underflow       -> guard-page fault
```

Generate and inspect them with:

```sh
make examples/v1_2/simple_raw.bin
./emulator info examples/v1_2/simple_raw.bin
./emulator run examples/v1_2/write_code_page.bin
./emulator run examples/v1_2/execute_unmapped.bin
```

Current limitations:

- This is still a teaching VM, not a real ARMv8 MMU.
- There are no page tables, TLBs, signals, demand paging, `mmap`, copy-on-write, threads, ASLR, or process isolation.

Definition of done for the v1.2 teaching profile:

- ELF and Mach-O segment permissions are represented and tested.
- Invalid memory behavior is deterministic and easy to debug.
- Stack guard behavior is covered by tests and examples.
- The project has documentation explaining the virtual memory model.

### v1.3 — Memory-Mapped Devices

**Goal:** introduce simple hardware-device emulation.

Add device bus:

- Route memory accesses to RAM or devices.
- Register devices at fixed address ranges.
- Provide clear errors for unmapped device addresses.

Initial devices:

- UART console.
- Timer.
- Random-number device.

Suggested memory map:

```text
0x0000_1000 - code / program area
0x0001_0000 - data area
0x0008_0000 - stack area
0x0900_0000 - UART
0x0901_0000 - timer
0x0902_0000 - random device
```

UART behavior:

```text
write byte to 0x09000000 -> print character
```

Definition of done:

- A program can print through memory-mapped UART without using fake syscalls.
- Device reads/writes are tested.
- The memory map is documented.

### v1.4 — Toy Kernel Mode

**Goal:** support a tiny educational kernel running above the emulator platform.

Add simplified privilege support:

- EL0 and EL1 mode tracking.
- A small subset of system registers:
  - `CurrentEL`
  - `SPSR_EL1`
  - `ELR_EL1`
  - `SP_EL0`
  - `SP_EL1`
  - `VBAR_EL1`
- Exception vector address handling.
- `SVC` trap from user mode into kernel mode.
- Return from exception through a simplified `ERET` implementation.

Toy kernel capabilities:

- Print through UART.
- Install an exception vector.
- Handle a syscall from user code.
- Return to user code.

Definition of done:

- A tiny kernel binary can boot at a fixed address.
- The kernel can handle one user `SVC` call and resume execution.
- The control transfer is visible in trace/debug output.

### v1.5 — Tiny OS Lab

**Goal:** turn the emulator into a small operating-systems playground.

Add OS-lab features:

- Load multiple user programs.
- Maintain per-task CPU state.
- Simple cooperative scheduler.
- Basic task switching.
- Per-task memory regions or simplified address spaces.
- Syscall table managed by the toy kernel.

Demo idea:

```text
kernel booting...
task 1: hello
task 2: counter 1
task 1: hello
task 2: counter 2
task 1: done
task 2: done
```

Definition of done:

- The toy kernel can switch between at least two tasks.
- Each task has its own saved registers and stack.
- The docs explain how the scheduler, task state, and syscall path work.

## Suggested Directory Layout

```text
emulator/
├── src/
│   ├── main.c
│   ├── cpu.c
│   ├── cpu.h
│   ├── memory.c
│   ├── memory.h
│   ├── decoder.c
│   ├── decoder.h
│   ├── loader.c
│   └── loader.h
├── examples/
│   └── v0_1_add.s
├── tests/
├── docs/
│   ├── roadmap.md
│   ├── instruction-support.md
│   └── memory-map.md
├── Makefile
└── README.md
```

## Build Philosophy

Every version should include:

1. A working feature.
2. A small assembly demo.
3. A test or reproducible check.
4. Updated documentation.
5. Clear demo output.

## License

Not chosen yet.

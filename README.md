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

## Lessons

- [v0.1 Lesson — Instruction Sandbox](lessons/v0.1-instruction-sandbox.md)
- [v0.2 Lesson — Branches and Loops](lessons/v0.2-branches-and-loops.md)
- [v0.3 Lesson — Memory and Stack](lessons/v0.3-memory-and-stack.md)
- [v0.4 Lesson — Functions and Returns](lessons/v0.4-functions-and-returns.md)
- [v0.5 Lesson — Debugger REPL](lessons/v0.5-debugger-repl.md)
- [v0.6 Lesson — Assembler-Friendly Runtime](lessons/v0.6-assembler-friendly-runtime.md)

## Current Implementation Status

The repository currently contains the runtime implementation for **v0.6 — Assembler-Friendly Runtime**.

Implemented now:

- C-based emulator core.
- Raw binary runner CLI:

```sh
./emulator run <raw-binary>
./emulator trace <raw-binary>
./emulator regs <raw-binary>
./emulator dump <raw-binary> <address> <length>
./emulator debug <raw-binary>
```

- Fixed 1 MiB flat memory.
- Raw binary load address: `0x1000`.
- Initial `pc`: `0x1000`.
- Initial `sp`: top of memory, currently `0x100000`.
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
  - `regs <raw-binary>` runs a program and prints only the final register state
  - `cpu_format_instruction()` formats supported instructions for lessons and tests
  - `examples/README.md` documents how example assembly is built and used
  - memory-access runtime errors include instruction `pc` and raw opcode context
- Automated test suites following `docs/test-plan-v0.1.md`, `docs/test-plan-v0.2.md`, `docs/test-plan-v0.3.md`, `docs/test-plan-v0.4.md`, and `docs/test-plan-v0.5.md`:
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

v0.6 automated tests are pending. Existing v0.1 through v0.5 tests still pass with the v0.6 runtime changes.

## Build and Run

Build the emulator:

```sh
make
```

Build the example raw ARM64 binaries:

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

v0.6 trace output includes address, opcode, and decoded instruction text:

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

Or build and run the main demo in one command:

```sh
make run-demo
```

Run the current automated test suite:

```sh
make test
```

The test target currently builds the emulator, assembles all examples, compiles the v0.1 through v0.5 C test runners, and runs all v0.1 through v0.5 CLI checks.

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
- No ELF or Mach-O loader yet.
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
- Trace mode uses `./emulator trace <raw-binary>` and prints each executed `pc` before the final register dump. Starting in v0.6, each trace line also includes the raw opcode and decoded instruction text.
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
- Memory dump uses `./emulator dump <raw-binary> <address> <length>` and prints memory after successful program execution.
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

### v0.7 — Fake Syscalls

**Goal:** allow small programs to interact with the host through a simple emulator ABI.

Add instruction support:

- `SVC`

Add fake syscall dispatcher:

```text
x8 = syscall number
x0-x5 = arguments
x0 = return value
```

Suggested initial syscalls:

```text
1 = exit(code)
2 = write(fd, ptr, len)
3 = read(fd, ptr, len)       // optional in this version
4 = time()                   // optional
5 = random()                 // optional
```

Demo program idea:

```asm
mov x0, #1          // stdout
adr x1, message
mov x2, #13
mov x8, #2          // write
svc #0

mov x0, #0
mov x8, #1          // exit
svc #0

message:
.ascii "hello world!\n"
```

Expected result:

```text
hello world!
program exited with code 0
```

Definition of done:

- A program can print text through `write`.
- A program can terminate through `exit`.
- Invalid syscall numbers produce clear errors or return a documented error code.

### v0.8 — ELF Loader

**Goal:** load real AArch64 ELF64 executable files instead of only raw binaries.

Add ELF support:

- Validate ELF magic.
- Validate class is ELF64.
- Validate target is AArch64.
- Validate little-endian encoding.
- Load `PT_LOAD` segments into emulator memory.
- Respect entry point from ELF header.
- Zero-fill `.bss` through segment memory size handling.
- Create initial stack.

Initial limitations:

- Support static `ET_EXEC` first.
- No dynamic linker.
- No libc requirement.
- No relocations unless a later example needs a small subset.

Example command:

```sh
aarch64-linux-gnu-gcc -nostdlib -static examples/v0_7_hello.s -o hello.elf
./emulator run hello.elf
```

Definition of done:

- The emulator can load a simple static AArch64 ELF.
- The emulator starts at the ELF entry point.
- Segment bounds and permissions are represented internally, even if permissions are not enforced yet.

### v0.9 — Tiny C Programs

**Goal:** run small freestanding C programs compiled to AArch64.

Add instruction support commonly emitted by compilers:

- `AND`
- `ORR`
- `EOR`
- `LSL`
- `LSR`
- `ASR`
- `MUL`
- `SDIV`
- `UDIV`
- `ADR`
- `ADRP`
- more load/store variants as needed

Add C runtime support:

- A tiny `_start` assembly stub.
- Simple call into `main`.
- Exit via fake syscall.
- No standard library required.

Demo C program:

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    return fib(10);
}
```

Expected result:

```text
program exited with code 55
```

Definition of done:

- A freestanding C program can be compiled and executed.
- Recursive function calls work.
- The README documents the exact compiler command.

### v1.0 — Stable Learning Emulator

**Goal:** publish a polished first stable release for learning ARM64 emulation.

Required features:

- Raw binary loader.
- ELF64 loader.
- Debugger REPL.
- Breakpoints.
- Register inspection.
- Memory inspection.
- Instruction tracing.
- Fake syscalls for `exit` and `write`.
- Example assembly programs.
- Example freestanding C programs.
- Automated tests for CPU, memory, loader, and syscall behavior.
- Documentation for supported instructions and known limitations.

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
- CI or a local `make test` target verifies the supported instruction set.

### v1.1 — Mach-O Loader

**Goal:** add Apple-focused binary-format learning without trying to boot iOS or macOS.

Add Mach-O support:

- Parse Mach-O arm64 headers.
- Parse `LC_SEGMENT_64`.
- Map `__TEXT` and `__DATA` segments.
- Parse entry point from `LC_MAIN` when available.
- Basic symbol/table inspection if practical.
- Clear unsupported-feature errors for dynamic linking and complex relocations.

Initial limitations:

- Prefer simple, static or freestanding Mach-O examples.
- Do not attempt to run normal macOS/iOS dynamically linked apps.
- Treat this as a binary loader and inspection milestone, not an OS compatibility milestone.

Definition of done:

- The emulator can inspect a simple arm64 Mach-O file.
- The emulator can load and run a deliberately simple Mach-O example, or clearly document why a given Mach-O requires unsupported runtime features.

### v1.2 — Virtual Memory

**Goal:** teach page-based memory, permissions, and fault handling.

Add memory model:

- Page-sized mappings.
- Mapped and unmapped regions.
- Read/write/execute permissions.
- Stack guard page.
- Memory access fault reporting.

Example behavior:

```text
write to RX code page -> permission fault
execute RW data page -> permission fault
read unmapped page    -> unmapped memory fault
```

Add debugger support:

- List memory mappings.
- Show page permissions.
- Identify the mapping for an address.

Definition of done:

- ELF segment permissions can be represented and enforced.
- Invalid memory behavior is deterministic and easy to debug.
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

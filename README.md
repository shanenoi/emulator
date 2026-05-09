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

## Planned Versions

### v0.1 — Instruction Sandbox

Initial emulator core:

- CPU register state
- Program counter
- Stack pointer
- Flat memory
- Raw binary loader
- Fetch/decode/execute loop
- Minimal instructions:
  - `NOP`
  - `HLT`
  - `MOVZ`
  - `ADD` immediate
  - `SUB` immediate

Expected demo:

```asm
mov x0, #2
mov x1, #3
add x2, x0, x1
hlt #0
```

Expected result:

```text
x0 = 2
x1 = 3
x2 = 5
halted
```

### v0.2 — Branches and Loops

Add:

- `CMP`
- `B`
- `B.cond`
- `CBZ`
- `CBNZ`
- NZCV flags
- basic instruction tracing

### v0.3 — Memory and Stack

Add:

- `LDR`
- `STR`
- `LDP`
- `STP`
- stack initialization
- memory bounds checking
- memory dump tools

### v0.4 — Functions

Add:

- `BL`
- `RET`
- link register `x30`
- call tracing

### v0.5 — Debugger REPL

Add interactive commands:

```text
run
step
continue
regs
mem <addr> <len>
break <addr>
trace on
trace off
quit
```

### v0.6 — Better CLI and Tracing

Add:

- improved command-line interface
- disassembly in traces
- symbol map support
- more example programs

### v0.7 — Fake Syscalls

Add:

- `SVC`
- fake syscall dispatcher
- `exit`
- `write`

### v0.8 — ELF Loader

Add:

- ELF64 parser
- AArch64 executable loading
- program header mapping
- entry point detection
- `.bss` zero-fill

### v0.9 — Tiny C Programs

Add enough instructions to run small freestanding C programs.

### v1.0 — Stable Learning Emulator

A polished release with:

- raw binary loader
- ELF64 loader
- debugger
- fake syscalls
- tests
- examples
- documentation

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

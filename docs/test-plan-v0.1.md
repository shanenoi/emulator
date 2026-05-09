# v0.1 Test Plan — Instruction Sandbox

## Purpose

This test plan defines the required tests for **v0.1 — Instruction Sandbox**.

The first version of the emulator should prove that a raw ARM64/AArch64 binary can be loaded into memory, executed instruction by instruction, halted, and inspected through final CPU state output.

v0.1 intentionally supports only a small subset of the architecture:

- `NOP`
- `HLT`
- `MOVZ`
- `ADD` immediate
- `SUB` immediate
- basic CPU state
- flat memory
- raw binary loading
- final register dump

The goal of this test plan is to make the first version small but reliable.

## Scope

### In scope

- CPU initialization
- register read/write behavior
- `pc` and `sp` initialization
- flat memory reads/writes
- raw binary loading
- instruction fetch
- instruction decode for supported instructions
- execution of supported instructions
- halt behavior
- instruction count behavior
- error handling for malformed or unsupported input
- command-line behavior for the initial runner

### Out of scope

- branches and loops
- conditional flags correctness beyond storing/resetting initial NZCV state
- load/store ARM64 instructions
- stack push/pop instructions
- ELF loading
- Mach-O loading
- syscalls
- debugger REPL
- virtual memory
- memory-mapped devices
- cycle accuracy
- floating point, SIMD, NEON, or system instructions

## Assumptions

- Memory size is fixed at **1 MiB** for v0.1 unless configured otherwise.
- Raw binaries are loaded at `0x1000`.
- `pc` starts at `0x1000`.
- `sp` starts at the top of memory.
- Instructions are 32-bit and little-endian.
- The emulator stops on `HLT`, unsupported instruction, invalid memory fetch, or loader error.
- The emulator exits with status code `0` for successful halt and non-zero for emulator errors.
- The emulator prints a final register dump after successful execution.

## Test artifact conventions

Each execution test should have:

- a small assembly source in `examples/`
- a raw binary generated from that source
- expected final register values
- expected halt/error behavior
- an automated test where practical

Suggested layout:

```text
examples/v0_1/
├── add.s
├── sub.s
├── movz.s
├── nop_hlt.s
└── unsupported.bin

tests/v0_1/
├── test_cpu_init.c
├── test_memory.c
├── test_loader.c
├── test_execute.c
└── test_cli.sh
```

## Required test categories

## 1. CPU initialization tests

### TC-CPU-001 — Registers start at zero

**Purpose:** verify deterministic CPU startup state.

**Steps:**

1. Create a new CPU instance.
2. Inspect `x0` through `x30`.

**Expected result:**

- All general-purpose registers are `0`.

### TC-CPU-002 — `pc` starts at load address after loading

**Purpose:** verify the emulator begins execution at the raw binary load base.

**Steps:**

1. Load a valid raw binary at `0x1000`.
2. Inspect `pc` before running.

**Expected result:**

- `pc == 0x1000`.

### TC-CPU-003 — `sp` starts at top of memory

**Purpose:** verify stack pointer initialization even though stack instructions are not supported yet.

**Steps:**

1. Initialize emulator memory with size `1 MiB`.
2. Initialize CPU state.
3. Inspect `sp`.

**Expected result:**

- `sp == memory_size`, or the documented top-of-memory address.
- The exact value must be stable and documented.

### TC-CPU-004 — NZCV flags are initialized deterministically

**Purpose:** ensure flags do not contain random values.

**Steps:**

1. Create a CPU instance.
2. Inspect NZCV state.

**Expected result:**

- `N == 0`
- `Z == 0`
- `C == 0`
- `V == 0`

## 2. Memory tests

### TC-MEM-001 — Memory starts zero-filled

**Purpose:** avoid nondeterministic behavior from uninitialized memory.

**Steps:**

1. Create flat memory.
2. Read bytes from several addresses, including beginning, middle, and final byte.

**Expected result:**

- All checked bytes are `0x00`.

### TC-MEM-002 — Valid 8-bit read/write

**Purpose:** verify byte-level memory behavior.

**Steps:**

1. Write `0xab` to address `0x2000`.
2. Read one byte from `0x2000`.

**Expected result:**

- Read value is `0xab`.

### TC-MEM-003 — Valid 32-bit little-endian read/write

**Purpose:** verify instruction-sized little-endian memory behavior.

**Steps:**

1. Write `0x12345678` as a 32-bit value at `0x2000`.
2. Read individual bytes from `0x2000` through `0x2003`.
3. Read 32-bit value from `0x2000`.

**Expected result:**

- Bytes are `0x78`, `0x56`, `0x34`, `0x12`.
- 32-bit read returns `0x12345678`.

### TC-MEM-004 — Valid 64-bit little-endian read/write

**Purpose:** verify register-sized memory helper behavior.

**Steps:**

1. Write `0x1122334455667788` at `0x3000`.
2. Read it back as 64-bit.

**Expected result:**

- Read value is `0x1122334455667788`.

### TC-MEM-005 — Read at final valid byte succeeds

**Purpose:** test boundary correctness.

**Steps:**

1. Write one byte at `memory_size - 1`.
2. Read one byte at `memory_size - 1`.

**Expected result:**

- Read succeeds and returns the written byte.

### TC-MEM-006 — Read past end fails

**Purpose:** prevent out-of-bounds memory access.

**Steps:**

1. Attempt to read one byte at `memory_size`.

**Expected result:**

- Read fails.
- Error message includes the invalid address.
- Emulator does not crash.

### TC-MEM-007 — 32-bit read crossing memory end fails

**Purpose:** catch partial out-of-bounds reads.

**Steps:**

1. Attempt to read 32 bits at `memory_size - 2`.

**Expected result:**

- Read fails because the full 4-byte range is not valid.

### TC-MEM-008 — 64-bit write crossing memory end fails

**Purpose:** catch partial out-of-bounds writes.

**Steps:**

1. Attempt to write 64 bits at `memory_size - 4`.

**Expected result:**

- Write fails because the full 8-byte range is not valid.
- No partial write occurs.

## 3. Raw binary loader tests

### TC-LOAD-001 — Load valid small binary

**Purpose:** verify basic raw binary loading.

**Input:** a binary containing `NOP; HLT`.

**Steps:**

1. Load the binary at `0x1000`.
2. Read memory at `0x1000`.

**Expected result:**

- File bytes appear exactly at `0x1000`.
- `pc == 0x1000`.

### TC-LOAD-002 — Empty binary is rejected

**Purpose:** avoid running uninitialized memory by accident.

**Steps:**

1. Try to load a zero-byte file.

**Expected result:**

- Loader returns an error.
- CLI exits non-zero.
- Error says the input file is empty.

### TC-LOAD-003 — Missing binary path is rejected

**Purpose:** verify user-facing CLI error handling.

**Steps:**

1. Run the emulator with no program path.

**Expected result:**

- CLI exits non-zero.
- Usage message is printed.

### TC-LOAD-004 — Nonexistent file is rejected

**Purpose:** verify file-open errors are clear.

**Steps:**

1. Run the emulator with a nonexistent file path.

**Expected result:**

- CLI exits non-zero.
- Error includes the path.

### TC-LOAD-005 — Binary larger than memory is rejected

**Purpose:** prevent loader overflow.

**Steps:**

1. Try to load a file larger than available memory from `0x1000` to memory end.

**Expected result:**

- Loader rejects the file.
- Error mentions file size and available memory.

### TC-LOAD-006 — Binary exactly fills remaining memory

**Purpose:** test loader boundary behavior.

**Steps:**

1. Create a file with size `memory_size - 0x1000`.
2. Load it at `0x1000`.

**Expected result:**

- Load succeeds.
- No out-of-bounds write occurs.

### TC-LOAD-007 — Binary size not divisible by 4 is accepted but unsafe to execute past full instructions

**Purpose:** document raw binary behavior.

**Steps:**

1. Load a binary whose size is 5 bytes.
2. Run if the first instruction is valid and does not require reading beyond file bytes.

**Expected result:**

- Loader behavior is documented.
- Preferred behavior: loader accepts the file, but instruction fetch still requires 4 valid memory bytes.
- If execution reaches trailing incomplete bytes, emulator reports an invalid/unsupported instruction or fetch error.

## 4. Instruction fetch tests

### TC-FETCH-001 — Fetch reads 32-bit instruction at `pc`

**Purpose:** verify basic fetch behavior.

**Steps:**

1. Write a known 32-bit instruction at `0x1000`.
2. Set `pc = 0x1000`.
3. Fetch instruction.

**Expected result:**

- Fetched value equals the 32-bit instruction.

### TC-FETCH-002 — Fetch uses little-endian order

**Purpose:** verify ARM64 instruction byte order.

**Steps:**

1. Write bytes `1f 20 03 d5` at `0x1000`.
2. Fetch 32-bit instruction.

**Expected result:**

- Fetched instruction is `0xd503201f`, the ARM64 `NOP` encoding.

### TC-FETCH-003 — Fetch beyond memory fails

**Purpose:** avoid out-of-bounds instruction access.

**Steps:**

1. Set `pc = memory_size - 2`.
2. Attempt to fetch instruction.

**Expected result:**

- Fetch fails.
- Error includes `pc`.
- Emulator exits non-zero if running through CLI.

### TC-FETCH-004 — Misaligned `pc` is rejected or clearly documented

**Purpose:** ARM64 instructions are 4-byte aligned.

**Steps:**

1. Set `pc = 0x1002`.
2. Attempt to fetch instruction.

**Expected result:**

- Preferred behavior: emulator rejects misaligned `pc` with a clear error.
- If v0.1 permits misalignment internally, the behavior must be documented and tested.

## 5. Instruction decode tests

### TC-DEC-001 — Decode `NOP`

**Purpose:** verify known fixed instruction decoding.

**Input:** opcode `0xd503201f`.

**Expected result:**

- Decoded instruction type is `NOP`.

### TC-DEC-002 — Decode `HLT`

**Purpose:** verify halt instruction decoding.

**Input:** a valid `HLT #0` encoding.

**Expected result:**

- Decoded instruction type is `HLT`.
- Immediate value is captured if the implementation stores it.

### TC-DEC-003 — Decode `MOVZ` to 64-bit register

**Purpose:** verify move-wide immediate decoding.

**Input:** assembled `movz x0, #0x1234`.

**Expected result:**

- Decoded instruction is `MOVZ`.
- Destination register is `x0`.
- Immediate is `0x1234`.

### TC-DEC-004 — Decode `MOVZ` to 32-bit register

**Purpose:** verify `w` register variant if supported in v0.1.

**Input:** assembled `movz w0, #0x1234`.

**Expected result:**

- Decoded instruction is `MOVZ`.
- Destination register maps to register index `0`.
- Width is 32-bit.

### TC-DEC-005 — Decode `ADD` immediate

**Purpose:** verify add-immediate field extraction.

**Input:** assembled `add x2, x0, #3`.

**Expected result:**

- Decoded instruction is `ADD_IMM`.
- Destination register is `x2`.
- Source register is `x0`.
- Immediate is `3`.

### TC-DEC-006 — Decode `SUB` immediate

**Purpose:** verify subtract-immediate field extraction.

**Input:** assembled `sub x2, x0, #3`.

**Expected result:**

- Decoded instruction is `SUB_IMM`.
- Destination register is `x2`.
- Source register is `x0`.
- Immediate is `3`.

### TC-DEC-007 — Unsupported opcode is rejected

**Purpose:** verify unknown instructions do not execute silently.

**Input:** `0xffffffff`.

**Expected result:**

- Decode returns unsupported-instruction error.
- Error includes raw opcode.

## 6. Instruction execution tests

### TC-EXEC-001 — `NOP` advances `pc`

**Purpose:** verify normal instruction stepping.

**Program:**

```asm
nop
hlt #0
```

**Expected result:**

- After `NOP`, `pc` advances by `4`.
- Program then halts on `HLT`.
- Instruction count is `2` if `HLT` is counted as executed.

### TC-EXEC-002 — `HLT` stops execution

**Purpose:** verify clean halt behavior.

**Program:**

```asm
hlt #0
```

**Expected result:**

- Emulator stops successfully.
- CLI exits with status code `0`.
- Final output indicates halted state.

### TC-EXEC-003 — Instructions after `HLT` are not executed

**Purpose:** verify halt is terminal.

**Program:**

```asm
hlt #0
mov x0, #99
```

**Expected result:**

- `x0 == 0`.
- Second instruction is not executed.

### TC-EXEC-004 — `MOVZ` sets low 16 bits

**Purpose:** verify basic immediate move.

**Program:**

```asm
movz x0, #0x1234
hlt #0
```

**Expected result:**

- `x0 == 0x1234`.

### TC-EXEC-005 — `MOVZ` with shifted immediate

**Purpose:** verify move-wide shift handling if included in v0.1.

**Program:**

```asm
movz x0, #0x1234, lsl #16
hlt #0
```

**Expected result:**

- `x0 == 0x12340000`.

**Note:** if shifted `MOVZ` is deferred, this test should be marked expected-fail or moved to the version that implements it.

### TC-EXEC-006 — `MOVZ w0` zero-extends into `x0`

**Purpose:** verify 32-bit write behavior if `w` registers are supported.

**Program:**

```asm
movz x0, #0xffff, lsl #16
movz w0, #1
hlt #0
```

**Expected result:**

- `x0 == 1`.

**Note:** if `w` register support is deferred, document that v0.1 only supports `x` registers.

### TC-EXEC-007 — `ADD` immediate basic case

**Purpose:** verify addition.

**Program:**

```asm
movz x0, #2
add x1, x0, #3
hlt #0
```

**Expected result:**

- `x0 == 2`.
- `x1 == 5`.

### TC-EXEC-008 — `ADD` immediate with zero immediate

**Purpose:** verify edge immediate value.

**Program:**

```asm
movz x0, #7
add x1, x0, #0
hlt #0
```

**Expected result:**

- `x1 == 7`.

### TC-EXEC-009 — `SUB` immediate basic case

**Purpose:** verify subtraction.

**Program:**

```asm
movz x0, #7
sub x1, x0, #3
hlt #0
```

**Expected result:**

- `x1 == 4`.

### TC-EXEC-010 — `SUB` immediate underflow wraps as unsigned 64-bit

**Purpose:** define arithmetic overflow behavior.

**Program:**

```asm
movz x0, #0
sub x1, x0, #1
hlt #0
```

**Expected result:**

- `x1 == 0xffffffffffffffff` for 64-bit operation.

### TC-EXEC-011 — Writes to `xzr` are ignored

**Purpose:** verify ARM64 zero-register behavior if implemented in v0.1.

**Program:**

```asm
movz xzr, #123
add x0, xzr, #5
hlt #0
```

**Expected result:**

- `x0 == 5`.
- The zero register always reads as `0`.

**Note:** if `xzr` support is not included in v0.1, document unsupported status clearly.

### TC-EXEC-012 — Maximum register index works

**Purpose:** verify decoding and execution near register-index boundaries.

**Program:**

```asm
movz x30, #9
add x0, x30, #1
hlt #0
```

**Expected result:**

- `x30 == 9`.
- `x0 == 10`.

### TC-EXEC-013 — `pc` advances by 4 after non-branch instructions

**Purpose:** verify sequential execution.

**Program:**

```asm
movz x0, #1
movz x1, #2
add x2, x0, #3
hlt #0
```

**Expected result:**

- Instruction addresses executed are `0x1000`, `0x1004`, `0x1008`, `0x100c`.

### TC-EXEC-014 — Unsupported instruction stops execution before later valid instructions

**Purpose:** verify fail-fast behavior.

**Program:**

```text
0xffffffff
valid movz x0, #1
valid hlt #0
```

**Expected result:**

- Emulator exits non-zero.
- `x0 == 0`.
- Error includes `0xffffffff` and address `0x1000`.

## 7. Full-program acceptance tests

### TC-ACC-001 — Add two numbers demo

**Purpose:** validate the primary v0.1 demo.

**Program:**

```asm
movz x0, #2
movz x1, #3
add x2, x0, #3
hlt #0
```

**Expected result:**

- `x0 == 2`
- `x1 == 3`
- `x2 == 5`
- Program halts successfully.

### TC-ACC-002 — Subtract two values

**Purpose:** validate `SUB` in a complete program.

**Program:**

```asm
movz x0, #10
sub x1, x0, #4
hlt #0
```

**Expected result:**

- `x1 == 6`.

### TC-ACC-003 — Multiple independent registers

**Purpose:** verify instructions do not accidentally corrupt unrelated registers.

**Program:**

```asm
movz x0, #1
movz x1, #2
movz x2, #3
add x3, x0, #9
sub x4, x2, #1
hlt #0
```

**Expected result:**

- `x0 == 1`
- `x1 == 2`
- `x2 == 3`
- `x3 == 10`
- `x4 == 2`

### TC-ACC-004 — Program with leading NOPs

**Purpose:** verify harmless instructions do not alter state.

**Program:**

```asm
nop
nop
movz x0, #42
hlt #0
```

**Expected result:**

- `x0 == 42`.
- Instruction count includes both `NOP` instructions.

## 8. CLI tests

### TC-CLI-001 — `run` command succeeds for valid program

**Command:**

```sh
./emulator run examples/v0_1/add.bin
```

**Expected result:**

- Exit status is `0`.
- Output includes `halted`.
- Output includes final register state.

### TC-CLI-002 — no arguments prints usage

**Command:**

```sh
./emulator
```

**Expected result:**

- Exit status is non-zero.
- Output includes usage instructions.

### TC-CLI-003 — unknown command is rejected

**Command:**

```sh
./emulator unknown examples/v0_1/add.bin
```

**Expected result:**

- Exit status is non-zero.
- Output says the command is unknown.

### TC-CLI-004 — valid file error is distinguishable from execution error

**Purpose:** make debugging easier.

**Steps:**

1. Run a missing file.
2. Run a file containing unsupported opcode.

**Expected result:**

- Missing file reports a loader/file error.
- Unsupported opcode reports an execution/decode error.
- Both exit non-zero.

## 9. Error handling and robustness tests

### TC-ERR-001 — unsupported instruction reports address and opcode

**Purpose:** make failures actionable.

**Expected result:**

- Error includes:
  - `pc`
  - raw opcode
  - short reason

### TC-ERR-002 — execution limit prevents infinite execution without `HLT`

**Purpose:** avoid hanging on binaries with no halt instruction.

**Steps:**

1. Run a binary full of `NOP` instructions with no `HLT`, or a short program that falls into zero memory.
2. Configure a maximum instruction count if v0.1 supports one.

**Expected result:**

- Preferred behavior: emulator stops after a documented instruction limit and exits non-zero.
- If no instruction limit exists in v0.1, this risk must be documented as a known limitation before release.

### TC-ERR-003 — fall-through into zero memory is handled deterministically

**Purpose:** avoid mysterious behavior after program bytes end.

**Program:** valid instruction sequence without `HLT` followed by zero-filled memory.

**Expected result:**

- Emulator eventually reports unsupported instruction `0x00000000`, fetch error, or instruction-limit error.
- Behavior is deterministic and documented.

### TC-ERR-004 — allocation failure path is handled

**Purpose:** avoid crashes on memory allocation failures.

**v0.1 status:** implementation handles allocation failure in `memory_init()` by returning `false` with a clear error message. Automated forced allocation-failure testing is deferred because v0.1 does not yet include allocator injection or a test hook for making `calloc()` fail deterministically.

**Steps:**

1. Force memory allocation failure through a test hook, if available.

**Expected result:**

- Emulator returns a clear initialization error.
- No null pointer dereference occurs.

## 10. Output format tests

### TC-OUT-001 — final register dump includes all core registers

**Purpose:** ensure users can inspect final state.

**Expected result:**

- Output includes `x0` through `x30`.
- Output includes `sp`.
- Output includes `pc`.
- Output includes instruction count.

### TC-OUT-002 — numeric output format is stable

**Purpose:** make CLI tests reliable.

**Expected result:**

- Register values are printed in a documented format.
- Recommended format: 16-digit hexadecimal for 64-bit values.

Example:

```text
x0  = 0x0000000000000002
pc  = 0x000000000000100c
```

### TC-OUT-003 — successful halt and error output are distinguishable

**Purpose:** users and tests should not confuse success with failure.

**Expected result:**

- Successful halt includes `halted` or equivalent.
- Error output includes `error` or equivalent.
- Exit status matches the outcome.

## 11. Edge case checklist

The following edge cases must be explicitly tested, documented, or marked deferred before releasing v0.1:

- empty input file
- missing input file
- input file larger than available memory
- input file exactly filling memory
- unsupported opcode at first instruction
- unsupported opcode after several valid instructions
- no `HLT` instruction
- `HLT` as first instruction
- instructions after `HLT`
- fetch at last valid aligned address
- fetch crossing memory end
- misaligned `pc`
- arithmetic underflow in `SUB`
- `ADD` with immediate `0`
- `MOVZ` immediate `0`
- `MOVZ` maximum 16-bit immediate `0xffff`
- register `x0`
- register `x30`
- zero register `xzr`, if supported
- 32-bit `w` registers, if supported
- stable output format for automated tests

## 12. Minimum release gate for v0.1

v0.1 is releasable only when all of the following are true:

- The emulator builds from a clean checkout.
- `make test` or the documented test command passes.
- The primary add demo runs successfully.
- At least one `NOP`/`HLT` test passes.
- At least one `MOVZ` test passes.
- At least one `ADD` immediate test passes.
- At least one `SUB` immediate test passes.
- Loader errors are tested.
- Unsupported instruction errors are tested.
- Out-of-bounds memory access is tested.
- Final register dump is stable enough for test assertions.
- Known limitations are documented in the README.

### Current implementation status

The v0.1 suite is implemented with:

- `tests/v0_1/test_v0_1.c` for CPU, memory, loader, fetch, decode, execution, acceptance, and edge-case coverage.
- `tests/v0_1/test_cli.sh` for CLI success and error behavior.
- `make test` as the documented release-gate command.

The C test runner creates temporary raw binaries under `tests/v0_1/tmp/` when a loader or CLI scenario needs an input file.

## 13. Suggested automated test strategy

Use three layers of tests:

### Unit tests

Test C functions directly:

- CPU initialization
- register access
- memory helpers
- decoder helpers
- loader helpers

### Integration tests

Run assembled raw binaries through the emulator core:

- `NOP; HLT`
- `MOVZ; HLT`
- `ADD; HLT`
- `SUB; HLT`
- unsupported opcode

### CLI tests

Run the compiled `emulator` executable from shell scripts:

- valid program
- missing arguments
- missing file
- unsupported instruction file
- output format checks

## 14. Example release test matrix

| Area | Required before v0.1 release | Edge cases required |
|---|---:|---:|
| CPU initialization | Yes | Yes |
| Memory helpers | Yes | Yes |
| Raw loader | Yes | Yes |
| Instruction fetch | Yes | Yes |
| `NOP` | Yes | No |
| `HLT` | Yes | Yes |
| `MOVZ` | Yes | Yes |
| `ADD` immediate | Yes | Yes |
| `SUB` immediate | Yes | Yes |
| Unsupported opcode | Yes | Yes |
| CLI usage | Yes | Yes |
| Output format | Yes | Yes |

## 15. Known decisions to make before implementation

The following behavior should be decided before coding v0.1 tests:

1. Should `HLT` count toward the instruction count?
2. Should the loader reject binaries whose size is not divisible by 4?
3. Should v0.1 support `w0-w30`, or only `x0-x30`?
4. Should v0.1 support `xzr/wzr`, or defer zero-register semantics?
5. Should shifted `MOVZ` be supported in v0.1?
6. What is the default maximum instruction count?
7. What exact output format should tests assert?

Recommended v0.1 choices:

- Count `HLT` as an executed instruction.
- Accept raw binaries whose size is not divisible by 4, but fail if execution reaches invalid bytes.
- Support `x0-x30` first.
- Support `xzr` if it is easy to include cleanly; otherwise document as deferred.
- Support unshifted `MOVZ` first; mark shifted forms as optional.
- Add a default instruction limit to prevent hangs.
- Print 64-bit values as fixed-width hexadecimal.

# v0.3 Test Plan — Memory and Stack

## Purpose

This test plan defines the required tests for **v0.3 — Memory and Stack**.

v0.1 proved straight-line instruction execution. v0.2 added control flow with branches, loops, condition flags, and trace mode. v0.3 adds the first real data movement between CPU registers and emulator memory, including stack-style addressing through `sp`.

The goal of v0.3 is to make the emulator capable of running small programs that store values in memory, load them back, and use the stack for simple push/pop-style flows while keeping the implementation intentionally small, inspectable, and testable.

## Current implementation status

The v0.3 runtime implementation is present, including the required memory/stack examples and `dump` CLI command. Automated v0.3 unit/integration and CLI tests have been added under `tests/v0_3/` and are included in `make test`.

Implemented now:

- `LDR` / `STR` unsigned-offset forms for 64-bit `X` and 32-bit `W` registers.
- `LDUR` / `STUR` signed unscaled forms for 64-bit `X` and 32-bit `W` registers.
- `LDR` / `STR` pre-index and post-index forms for 64-bit `X` and 32-bit `W` registers.
- `STP` / `LDP` 64-bit pair forms for offset, pre-index, and post-index addressing.
- `SP` as base register field `31` for memory addressing.
- `XZR` / `WZR` as source/destination register field `31` for load/store data registers.
- Unaligned data memory access is allowed; instruction fetch alignment is unchanged.
- Failed loads/stores avoid partial destination, memory, and write-back updates.
- CLI syntax: `./emulator dump <raw-binary> <address> <length>`.
- Test entry points: `tests/v0_3/test_v0_3.c` and `tests/v0_3/test_cli_memory.sh`.

## Scope

### In scope

- Preserve all v0.1 and v0.2 behavior and tests.
- Decode and execute basic ARM64 load/store instructions.
- Support register-memory data movement using 64-bit `X` registers and 32-bit `W` registers.
- Support basic stack usage through `sp` as a base register.
- Support unsigned-offset, unscaled signed-offset, pre-index, and post-index addressing where listed below.
- Support pair load/store for simple stack-frame style push/pop operations.
- Enforce memory bounds checks for every load/store.
- Preserve little-endian memory behavior.
- Preserve `xzr` / `wzr` behavior where a register field maps to register `31` in contexts where it means zero register.
- Correctly treat register `31` as `sp` in base-register contexts where ARM64 syntax uses `[sp, ...]`.
- Add CLI memory dump support for inspecting emulator memory after a run.
- Add examples for memory store/load, stack push/pop, pair store/load, and invalid access.
- Add automated tests for decode, execution, edge cases, CLI behavior, and examples.

### Out of scope

- Function calls with `BL` / `RET`.
- Full stack-frame conventions using `x29` / `x30` beyond simple `STP` / `LDP` examples.
- ELF loading.
- Mach-O loading.
- Syscalls.
- Debugger REPL.
- Virtual memory and page permissions.
- Memory-mapped devices.
- Floating point, SIMD, NEON, or system instructions.
- Atomic load/store instructions.
- Exclusive load/store instructions.
- Unprivileged load/store instructions.
- Load/store byte and halfword variants unless deliberately added.
- Sign-extending load variants such as `LDRSW`, `LDRSB`, or `LDRSH`.
- Full ARM64 addressing-mode matrix beyond the documented v0.3 set.

## Required instruction support

v0.3 must support the following new instruction families.

### Single-register load/store

Required:

- `LDR Xt, [Xn|SP]`
- `LDR Xt, [Xn|SP, #offset]`
- `LDR Wt, [Xn|SP]`
- `LDR Wt, [Xn|SP, #offset]`
- `STR Xt, [Xn|SP]`
- `STR Xt, [Xn|SP, #offset]`
- `STR Wt, [Xn|SP]`
- `STR Wt, [Xn|SP, #offset]`
- `LDUR Xt, [Xn|SP, #simm]`
- `LDUR Wt, [Xn|SP, #simm]`
- `STUR Xt, [Xn|SP, #simm]`
- `STUR Wt, [Xn|SP, #simm]`

### Pre-index / post-index stack-style addressing

Required for 64-bit `X` registers:

- `STR Xt, [Xn|SP, #simm]!`
- `LDR Xt, [Xn|SP], #simm`

Required for 32-bit `W` registers if the decoder naturally shares the same implementation:

- `STR Wt, [Xn|SP, #simm]!`
- `LDR Wt, [Xn|SP], #simm`

If 32-bit pre/post-index support is deferred, it must be documented and must fail clearly.

### Pair load/store

Required for simple stack examples:

- `STP Xt1, Xt2, [SP, #simm]!`
- `LDP Xt1, Xt2, [SP], #simm`

Recommended if straightforward:

- `STP Xt1, Xt2, [Xn|SP, #simm]`
- `LDP Xt1, Xt2, [Xn|SP, #simm]`

32-bit pair forms may be deferred unless used by examples.

## Required addressing modes

v0.3 must support these addressing forms:

```asm
[base]
[base, #offset]
[base, #offset]!
[base], #offset
```

Where:

- `base` may be an `X` register or `SP`.
- Unsigned-offset `LDR` / `STR` offsets are scaled by access size, matching ARM64 encoding.
- Unscaled `LDUR` / `STUR` offsets are signed byte offsets.
- Pre-index and post-index offsets are signed byte offsets.
- Pair instruction offsets are scaled by pair element size according to ARM64 encoding.

## Assumptions and implementation decisions

- ARM64 instructions remain fixed-width 32-bit little-endian instructions.
- Data memory remains a flat 1 MiB byte array.
- Program code is still loaded at `0x1000`.
- Initial `sp` remains `0x100000`.
- Normal non-branch, non-halt instructions advance `pc` by `4`.
- Successful load/store instructions increment `instructions_executed` by `1`.
- `HLT` still counts as an executed instruction and leaves `pc` at the `HLT` instruction address.
- Memory data is little-endian.
- 64-bit `LDR Xt` reads 8 bytes and writes all 64 bits of `Xt`.
- 32-bit `LDR Wt` reads 4 bytes and zero-extends into `Xt`.
- 64-bit `STR Xt` writes 8 bytes.
- 32-bit `STR Wt` writes only the low 32 bits.
- Loading into `wzr` / `xzr` discards the loaded value, but the memory read still happens and can still fail.
- Storing from `wzr` / `xzr` stores zero.
- In memory-base contexts, register field `31` means `SP`, not `XZR`.
- Write-back addressing must not update the base register if the memory access fails.
- Stores must be atomic from the emulator perspective: a failed store must not partially modify memory.
- Loads must not modify the destination register if the memory access fails.
- Pair operations must not partially modify destination registers or memory on failure.
- v0.3 may allow unaligned data load/store accesses because the existing memory helpers are byte-based and do not enforce alignment. If alignment enforcement is added deliberately, all tests and docs must be updated to match.
- Instruction fetch remains 4-byte aligned; this does not change in v0.3.
- CLI memory dump output must be stable enough for tests to match.

## Suggested artifact layout

```text
examples/v0_3/
├── memory_store_load.s
├── stack_push_pop.s
├── stp_ldp_stack.s
├── w_register_load_store.s
└── invalid_memory_access.s

tests/v0_3/
├── test_v0_3.c
└── test_cli_memory.sh
```

It is acceptable to keep all C tests in a single `tests/v0_3/test_v0_3.c` file at first, as long as test sections are clearly labeled with the test-case IDs in this plan.

## Required test categories

## 1. Regression tests from previous versions

### TC-V03-REG-001 — v0.1 and v0.2 suites still pass

**Purpose:** ensure memory/stack work does not break existing instruction execution or branch behavior.

**Steps:**

1. Run the full v0.1 suite.
2. Run the full v0.2 suite.
3. Run existing CLI examples.

**Expected result:**

- All previous tests pass unchanged.
- Existing example outputs remain stable.

### TC-V03-REG-002 — Existing memory helper behavior remains stable

**Purpose:** protect existing `memory_read*` and `memory_write*` semantics.

**Expected result:**

- Memory starts zero-filled.
- 8/32/64-bit helper reads and writes remain little-endian.
- Out-of-bounds helper calls fail cleanly.

## 2. Decode tests — single-register load/store

### TC-V03-DEC-LS-001 — Decode `LDR Xt, [Xn]`

**Program:**

```asm
ldr x0, [x1]
```

**Expected result:**

- Decodes as single-register load.
- Width is 64-bit.
- Destination is `x0`.
- Base is `x1`.
- Offset is `0`.
- Addressing mode is unsigned-offset or equivalent no-writeback form.

### TC-V03-DEC-LS-002 — Decode `LDR Xt, [Xn, #offset]`

**Program:**

```asm
ldr x2, [x3, #16]
```

**Expected result:**

- Decodes as 64-bit load.
- Destination is `x2`.
- Base is `x3`.
- Effective byte offset is `16`.
- No write-back.

### TC-V03-DEC-LS-003 — Decode `STR Xt, [Xn]`

**Program:**

```asm
str x4, [x5]
```

**Expected result:**

- Decodes as 64-bit store.
- Source is `x4`.
- Base is `x5`.
- Offset is `0`.
- No write-back.

### TC-V03-DEC-LS-004 — Decode `STR Xt, [Xn, #offset]`

**Program:**

```asm
str x6, [x7, #24]
```

**Expected result:**

- Decodes as 64-bit store.
- Source is `x6`.
- Base is `x7`.
- Effective byte offset is `24`.
- No write-back.

### TC-V03-DEC-LS-005 — Decode `LDR Wt` and `STR Wt`

**Program:**

```asm
ldr w8, [x9, #4]
str w10, [x11, #8]
```

**Expected result:**

- `LDR Wt` width is 32-bit and byte offset is `4`.
- `STR Wt` width is 32-bit and byte offset is `8`.
- Decoded register indexes match instruction operands.

### TC-V03-DEC-LS-006 — Decode SP as memory base

**Program:**

```asm
ldr x0, [sp]
str x1, [sp, #8]
```

**Expected result:**

- Base register field `31` is represented as `SP` in memory-base context.
- It is not treated as `XZR`.

## 3. Decode tests — unscaled and write-back addressing

### TC-V03-DEC-U-001 — Decode `LDUR` with positive offset

**Program:**

```asm
ldur x0, [x1, #7]
```

**Expected result:**

- Decodes as 64-bit unscaled load.
- Signed byte offset is `7`.

### TC-V03-DEC-U-002 — Decode `LDUR` with negative offset

**Program:**

```asm
ldur x0, [x1, #-8]
```

**Expected result:**

- Decodes as 64-bit unscaled load.
- Signed byte offset is `-8`.

### TC-V03-DEC-U-003 — Decode `STUR` with positive and negative offsets

**Program:**

```asm
stur x2, [x3, #15]
stur x2, [x3, #-16]
```

**Expected result:**

- Decodes as unscaled store.
- Signed byte offsets are sign-extended correctly.

### TC-V03-DEC-WB-001 — Decode pre-index store

**Program:**

```asm
str x0, [sp, #-8]!
```

**Expected result:**

- Decodes as 64-bit store.
- Addressing mode is pre-index.
- Signed byte offset is `-8`.
- Write-back is enabled.

### TC-V03-DEC-WB-002 — Decode post-index load

**Program:**

```asm
ldr x0, [sp], #8
```

**Expected result:**

- Decodes as 64-bit load.
- Addressing mode is post-index.
- Signed byte offset is `8`.
- Write-back is enabled.

## 4. Decode tests — pair load/store

### TC-V03-DEC-PAIR-001 — Decode `STP` pre-index stack push

**Program:**

```asm
stp x0, x1, [sp, #-16]!
```

**Expected result:**

- Decodes as pair store.
- First source is `x0`.
- Second source is `x1`.
- Base is `sp`.
- Signed byte offset is `-16`.
- Write-back is enabled.

### TC-V03-DEC-PAIR-002 — Decode `LDP` post-index stack pop

**Program:**

```asm
ldp x2, x3, [sp], #16
```

**Expected result:**

- Decodes as pair load.
- First destination is `x2`.
- Second destination is `x3`.
- Base is `sp`.
- Signed byte offset is `16`.
- Write-back is enabled.

### TC-V03-DEC-PAIR-003 — Decode signed pair offsets

**Purpose:** verify sign extension and scaling of pair offsets.

**Expected result:**

- Negative pair offsets decode correctly.
- Positive pair offsets decode correctly.
- Byte offsets equal encoded offset multiplied by element size.

## 5. Execution tests — basic load/store

### TC-V03-EXEC-LS-001 — `STR X` writes 64-bit little-endian data

**Program:**

```asm
movz x0, #0x1122
str  x0, [sp, #-8]!
hlt  #0
```

**Expected result:**

- `sp == 0x000ff8` after pre-index.
- Memory at `sp` contains the 64-bit little-endian value from `x0`.
- Instruction count includes `HLT`.

### TC-V03-EXEC-LS-002 — `LDR X` reads 64-bit data

**Setup:** write known 64-bit value to memory before execution.

**Program:**

```asm
ldr x0, [sp]
hlt #0
```

**Expected result:**

- `x0` equals the known 64-bit value.
- `sp` is unchanged.

### TC-V03-EXEC-LS-003 — `STR W` writes only low 32 bits

**Program:** store a register with high bits set through `STR Wt`.

**Expected result:**

- Only 4 bytes are written.
- Stored value equals low 32 bits of source register.
- Neighboring bytes are unchanged.

### TC-V03-EXEC-LS-004 — `LDR W` zero-extends into `X` register

**Setup:** memory contains `0xffffffff` at target address.

**Program:**

```asm
ldr w0, [sp]
hlt #0
```

**Expected result:**

- `x0 == 0x00000000ffffffff`.
- Upper 32 bits are zero, not preserved.

### TC-V03-EXEC-LS-005 — Offset addressing leaves base unchanged

**Program:**

```asm
str x0, [sp, #16]
ldr x1, [sp, #16]
hlt #0
```

**Expected result:**

- `x1 == x0`.
- `sp` remains the original top-of-memory value.

## 6. Execution tests — stack and write-back

### TC-V03-EXEC-STK-001 — Push/pop one 64-bit value with pre/post index

**Program:**

```asm
movz x0, #42
str  x0, [sp, #-8]!
ldr  x1, [sp], #8
hlt  #0
```

**Expected result:**

- `x1 == 42`.
- Final `sp == 0x100000`.
- Memory at the pushed address contains the stored value.

### TC-V03-EXEC-STK-002 — Multiple push/pop operations are LIFO

**Program:**

```asm
movz x0, #1
movz x1, #2
str  x0, [sp, #-8]!
str  x1, [sp, #-8]!
ldr  x2, [sp], #8
ldr  x3, [sp], #8
hlt  #0
```

**Expected result:**

- `x2 == 2`.
- `x3 == 1`.
- Final `sp == 0x100000`.

### TC-V03-EXEC-STK-003 — Pre-index updates base before access

**Program:**

```asm
movz x0, #7
str  x0, [sp, #-8]!
hlt  #0
```

**Expected result:**

- Store address is `initial_sp - 8`.
- Final `sp == initial_sp - 8`.

### TC-V03-EXEC-STK-004 — Post-index updates base after access

**Setup:** memory at initial `sp - 8` contains a known value; set `sp` to `initial_sp - 8`.

**Program:**

```asm
ldr x0, [sp], #8
hlt #0
```

**Expected result:**

- Load uses old `sp` address.
- Final `sp == initial_sp`.
- `x0` equals the known value.

## 7. Execution tests — pair load/store

### TC-V03-EXEC-PAIR-001 — `STP` stores two 64-bit registers

**Program:**

```asm
movz x0, #11
movz x1, #22
stp  x0, x1, [sp, #-16]!
hlt  #0
```

**Expected result:**

- Final `sp == initial_sp - 16`.
- Memory at `sp` contains `x0`.
- Memory at `sp + 8` contains `x1`.

### TC-V03-EXEC-PAIR-002 — `LDP` loads two 64-bit registers

**Setup:** memory at `sp` contains two known 64-bit values.

**Program:**

```asm
ldp x2, x3, [sp], #16
hlt #0
```

**Expected result:**

- `x2` equals first known value.
- `x3` equals second known value.
- Final `sp == initial_sp + 16`.

### TC-V03-EXEC-PAIR-003 — Pair stack round trip

**Program:**

```asm
movz x0, #100
movz x1, #200
stp  x0, x1, [sp, #-16]!
ldp  x2, x3, [sp], #16
hlt  #0
```

**Expected result:**

- `x2 == 100`.
- `x3 == 200`.
- Final `sp == 0x100000`.

### TC-V03-EXEC-PAIR-004 — Pair offset round trip keeps base unchanged

**Program:** store/load a pair through `[x5, #16]` with no write-back.

**Expected result:**

- Loaded registers match the stored pair values.
- Base register `x5` is unchanged.
- Memory at `x5 + 16` and `x5 + 24` contains the stored 64-bit values.

## 8. Register-31 semantics tests

### TC-V03-SP-001 — Base register `31` means `SP` for memory access

**Program:**

```asm
movz x0, #9
str  x0, [sp, #-8]!
ldr  x1, [sp], #8
hlt  #0
```

**Expected result:**

- Memory access uses `sp`.
- `x1 == 9`.
- Final `sp` is restored.

### TC-V03-SP-002 — Store from `xzr` writes zero

**Setup:** target memory initially contains nonzero bytes.

**Program:**

```asm
str xzr, [sp, #-8]!
hlt #0
```

**Expected result:**

- Stored 64-bit value is zero.
- `sp` is updated because the store succeeds.

### TC-V03-SP-003 — Load into `xzr` discards value but validates memory

**Setup:** target memory contains nonzero value.

**Program:**

```asm
ldr xzr, [sp]
hlt #0
```

**Expected result:**

- General registers are unchanged.
- Memory read succeeds.
- Instruction count advances.

### TC-V03-SP-004 — Load into `xzr` from invalid memory still fails

**Program:** create a load into `xzr` from out-of-bounds address.

**Expected result:**

- Emulator returns error.
- The load is not silently skipped.

## 9. Error and edge-case tests

### TC-V03-ERR-001 — Load from out-of-bounds memory fails

**Program:** attempt to `LDR X` from an address beyond memory.

**Expected result:**

- Emulator stops with `EMU_ERROR`.
- Error message mentions memory access or out of bounds.
- Destination register is unchanged.
- `instructions_executed` does not count the failed instruction unless the project deliberately documents failed-instruction counting.

### TC-V03-ERR-002 — Store to out-of-bounds memory fails without partial write

**Program:** attempt to `STR X` where only part of the 8-byte access would fit.

**Expected result:**

- Emulator stops with `EMU_ERROR`.
- Memory remains unchanged.
- Base register write-back does not occur.

### TC-V03-ERR-003 — Pre-index address underflow fails without write-back

**Program:** set base register to a low address and execute:

```asm
str x0, [x1, #-8]!
```

**Expected result:**

- Emulator returns error.
- `x1` remains unchanged.
- Memory remains unchanged.

### TC-V03-ERR-004 — Post-index write-back overflow fails without partial state change

**Program:** validate the post-index write-back overflow path with the shared memory-access calculation helper.

Normal v0.3 ARM64 encodings use signed 9-bit post-index offsets, so a post-index write-back overflow cannot be produced from a valid in-bounds 1 MiB memory address. The test still covers the overflow logic directly through `cpu_calculate_memory_access()`.

**Expected result:**

- Emulator returns error.
- Destination register remains unchanged.
- Base register remains unchanged.

### TC-V03-ERR-005 — Pair store crossing memory end fails atomically

**Program:** attempt `STP` where the first 8-byte store fits but the second would exceed memory.

**Expected result:**

- Emulator returns error.
- Neither stored value is written.
- Write-back does not occur.

### TC-V03-ERR-006 — Pair load crossing memory end fails atomically

**Program:** attempt `LDP` where the full 16-byte read does not fit.

**Expected result:**

- Emulator returns error.
- Neither destination register changes.
- Write-back does not occur.

### TC-V03-ERR-007 — Unsupported load/store variants fail clearly

**Instructions to test if not implemented:**

```asm
ldrb w0, [sp]
strb w0, [sp]
ldrsw x0, [sp]
```

**Expected result:**

- Unsupported variants decode as unsupported instruction.
- CLI exits nonzero with a clear error.
- Docs list these variants as out of scope.

### TC-V03-ERR-008 — Invalid base address from zero register confusion is avoided

**Purpose:** ensure `[sp]` uses `sp`, not zero.

**Program:**

```asm
ldr x0, [sp]
hlt #0
```

**Expected result:**

- Access is attempted at `sp`, not address `0`.

### TC-V03-ERR-009 — Unaligned data access behavior is documented and tested

**Program:** execute a 64-bit load/store at an odd address.

**Expected result:**

- If v0.3 allows unaligned data access, operation succeeds and little-endian bytes are correct.
- If v0.3 rejects unaligned data access, operation fails clearly.
- The chosen behavior is documented in README and this test plan.

## 10. CLI tests

### TC-V03-CLI-001 — Existing `run` and `trace` commands still work

**Expected result:**

- v0.1 and v0.2 CLI tests still pass.

### TC-V03-CLI-002 — Memory dump command syntax

**Required command:**

```sh
./emulator dump <raw-binary> <address> <length>
```

or, if implemented as an option:

```sh
./emulator run <raw-binary> --dump <address> <length>
```

**Expected result:**

- The chosen command is documented in README.
- Invalid/missing arguments print usage and exit nonzero.
- Address and length accept hex values such as `0xff8` or `0x1000`.
- Address and length accept decimal values if the CLI parser supports them.

### TC-V03-CLI-003 — Dump shows memory after program execution

**Program:** store a known value to stack memory and halt.

**Expected result:**

- Dump output includes the stored bytes.
- Output is stable enough for automated tests.
- Dump reads emulator memory after execution, not before execution.

### TC-V03-CLI-004 — Dump out-of-bounds range fails clearly

**Command:**

```sh
./emulator dump examples/v0_3/stack_push_pop.bin 0xffff8 16
```

**Expected result:**

- CLI exits nonzero.
- Error message mentions out of bounds.

### TC-V03-CLI-005 — Trace still shows loop and memory program addresses

**Command:**

```sh
./emulator trace examples/v0_3/stack_push_pop.bin
```

**Expected result:**

- Trace output includes executed instruction addresses.
- Final register dump remains present.

## 11. Acceptance tests

### TC-V03-ACC-001 — Basic store/load example

**Program:** `examples/v0_3/memory_store_load.s`

```asm
movz x0, #42
str  x0, [sp, #-8]!
ldr  x1, [sp], #8
hlt  #0
```

**Expected result:**

- `x1 == 42`.
- Final `sp == 0x100000`.
- Program halts successfully.

### TC-V03-ACC-002 — Stack push/pop example

**Program:** `examples/v0_3/stack_push_pop.s`

**Expected result:**

- Demonstrates LIFO behavior.
- Final `sp` is restored.
- Program halts successfully.

### TC-V03-ACC-003 — Pair store/load stack example

**Program:** `examples/v0_3/stp_ldp_stack.s`

**Expected result:**

- Two values are stored and loaded in order.
- Final `sp` is restored.
- Program halts successfully.

### TC-V03-ACC-004 — 32-bit `W` register load/store example

**Program:** `examples/v0_3/w_register_load_store.s`

**Expected result:**

- `STR Wt` writes four bytes.
- `LDR Wt` zero-extends into `Xt`.

### TC-V03-ACC-005 — Invalid memory access example

**Program:** `examples/v0_3/invalid_memory_access.s`

**Expected result:**

- CLI exits nonzero.
- Error is clear and stable enough for tests.

## 12. Documentation checks

### TC-V03-DOC-001 — README implementation status is current

**Expected result:**

README documents:

- v0.3 runtime status.
- New supported instructions.
- CLI dump command.
- Example commands for v0.3.
- Any intentionally deferred load/store variants.

### TC-V03-DOC-002 — Education guide exists

**Expected result:**

Add:

```text
education/v0.3-learning-guide.md
```

It should explain:

- how memory access instructions differ from arithmetic instructions
- what an effective address is
- why `[sp, #-8]!` changes `sp` before storing
- why `[sp], #8` changes `sp` after loading
- how `STP` / `LDP` support simple stack-frame-style behavior
- how to read memory dump output

### TC-V03-DOC-003 — Test plan status stays accurate

**Expected result:**

This test plan should be updated after implementation to mark:

- implemented instructions
- deferred instructions
- implemented CLI syntax
- any changed decisions around unaligned memory access or failed-instruction counting

## 13. Release checklist

Before v0.3 is considered complete:

- [x] All v0.1 tests pass.
- [x] All v0.2 tests pass.
- [x] All v0.3 unit tests pass.
- [x] All v0.3 CLI tests pass.
- [x] `make clean && make test` passes.
- [x] `make examples` builds v0.1, v0.2, and v0.3 examples.
- [x] README documents v0.3 usage.
- [x] `education/v0.3-learning-guide.md` exists.
- [x] Unsupported load/store variants are documented.
- [x] At least one CLI command demonstrates memory dump output.
- [x] Git status is clean after `make clean && make test`, excluding ignored generated build/test artifacts.

## 14. Suggested implementation order

1. Extend decoded instruction representation for memory operations and addressing modes.
2. Decode `LDR` / `STR` unsigned-offset forms.
3. Execute 64-bit `LDR X` / `STR X`.
4. Add 32-bit `LDR W` / `STR W` behavior.
5. Decode and execute `LDUR` / `STUR` signed unscaled offsets.
6. Add pre-index and post-index write-back forms.
7. Add `STP` / `LDP` for 64-bit stack pair operations.
8. Add v0.3 examples.
9. Add CLI dump command.
10. Add automated tests following this plan.
11. Update README and education docs.

## 15. Non-goals for v0.3 release

Do not expand v0.3 to include:

- function calls
- call stack tracing
- ELF loading
- syscalls
- debugger commands
- virtual memory
- page permissions
- heap allocation
- C program execution

Those belong to later versions.

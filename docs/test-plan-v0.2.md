# v0.2 Test Plan — Branches and Loops

## Purpose

This test plan defines the required tests for **v0.2 — Branches and Loops**.

v0.1 proved that the emulator can load a raw ARM64/AArch64 binary, execute a small straight-line instruction sequence, halt, and print CPU state. v0.2 adds the first form of real control flow: instructions that can change `pc` based on labels, register values, and condition flags.

The goal of v0.2 is to make the emulator capable of running small loops and simple conditional programs while keeping the architecture intentionally small and testable.

## Scope

### In scope

- Preserve all v0.1 behavior and tests.
- Decode and execute unconditional branches.
- Decode and execute conditional branches.
- Decode and execute compare-style instructions needed by conditional branches.
- Maintain correct `pc` behavior for taken and not-taken branches.
- Maintain correct instruction count behavior for branch instructions.
- Update NZCV flags for compare/subtract-style operations.
- Evaluate common ARM64 condition codes.
- Run simple loops using `CBZ` / `CBNZ`.
- Run simple loops using `CMP` + `B.cond`.
- Add basic trace mode that prints executed instruction addresses.
- Add examples for loops and conditional control flow.
- Add automated tests for decode, execute, edge cases, CLI behavior, and examples.

### Out of scope

- Load/store ARM64 instructions.
- Stack push/pop or stack-frame behavior.
- Function calls with `BL` / `RET`.
- ELF loading.
- Mach-O loading.
- Syscalls.
- Debugger REPL.
- Virtual memory.
- Memory-mapped devices.
- Floating point, SIMD, NEON, or system instructions.
- Cycle-accurate branch timing.
- Branch prediction.
- Exception levels.
- Full ARM64 condition-code matrix beyond the documented v0.2 set, unless implemented deliberately.

## Required instruction support

## Current implementation notes

The v0.2 runtime features and automated tests are implemented. `make test` runs the v0.1 suite and the v0.2 suite.

Implemented now:

- `B`
- `B.cond`
- `CBZ` / `CBNZ`
- `CMP Xn, #imm` / `CMP Wn, #imm`
- `CMP Xn, Xm` / `CMP Wn, Wm`, unshifted only
- NZCV updates for compare/subtract semantics
- condition evaluation for common ARM64 conditions
- `./emulator trace <raw-binary>`
- v0.2 assembly examples under `examples/v0_2/`, including:
  - forward unconditional branch
  - backward unconditional branch inside a terminating loop
  - `CBZ` / `CBNZ` examples
  - `CMP` + `B.cond` examples
  - `CMP` + `B.cond` loop
  - trace-mode loop

Deferred for v0.2 unless deliberately added later:

- `ADD Xd, Xn, Xm` / `ADD Wd, Wn, Wm`
- shifted `CMP register` forms

Implemented and covered by v0.2 tests:

- `CMP` immediate accepts shift `0` and shift `12`, matching ARM64 immediate arithmetic encoding forms supported by the decoder.

The existing v0.1 test suite must continue to pass throughout v0.2 development and release checks.

v0.2 must support the following new instructions.

### Required branch/control-flow instructions

- `B label`
- `B.cond label`
- `CBZ Xt, label`
- `CBNZ Xt, label`

### Required compare / flag-producing instructions

- `CMP Xn, #imm`
- `CMP Xn, Xm`

`CMP` is an alias for `SUBS` with the result discarded. The emulator may implement this internally as `SUBS xzr, Xn, operand`, but the observable result must be:

- source registers are not modified
- arithmetic result is not written to a general-purpose destination register
- NZCV flags are updated

### Required arithmetic extension for demo programs

v0.2 examples may need register-register addition for natural loop demos:

- `ADD Xd, Xn, Xm`

If we keep examples immediate-only, `ADD register` can be deferred. If included, it must be tested. The README currently lists it as "if needed by examples," so this test plan treats it as **conditionally required**: once any v0.2 example or implementation includes `ADD register`, the tests below become mandatory.

## Required condition-code support

v0.2 must support at least:

- `EQ` — equal, `Z == 1`
- `NE` — not equal, `Z == 0`
- `LT` — signed less than, `N != V`
- `LE` — signed less than or equal, `Z == 1 || N != V`
- `GT` — signed greater than, `Z == 0 && N == V`
- `GE` — signed greater than or equal, `N == V`

Optional but useful if decoding table naturally supports them:

- `MI` — negative, `N == 1`
- `PL` — positive or zero, `N == 0`
- `VS` — overflow, `V == 1`
- `VC` — no overflow, `V == 0`
- `CS` / `HS` — carry set / unsigned higher or same, `C == 1`
- `CC` / `LO` — carry clear / unsigned lower, `C == 0`
- `HI` — unsigned higher, `C == 1 && Z == 0`
- `LS` — unsigned lower or same, `C == 0 || Z == 1`
- `AL` — always

If optional conditions are not implemented in v0.2, unsupported optional condition encodings must fail clearly or be documented as unsupported.

## Assumptions and implementation decisions

- ARM64 instructions remain fixed-width 32-bit little-endian instructions.
- `pc` points to the address of the instruction being fetched.
- Normal non-branch instructions advance `pc` by `4`.
- A taken branch sets `pc` to the branch target.
- A not-taken conditional branch advances `pc` by `4`.
- Branch offsets are relative to the address of the branch instruction, matching ARM64 semantics.
- Branch offsets are signed and must be sign-extended correctly.
- Branch targets must be 4-byte aligned.
- Branch targets must remain inside emulator memory.
- Branching to unsupported or non-instruction bytes may succeed as a branch, but the next fetch/decode should fail if bytes are invalid.
- Every successfully executed branch instruction increments `instructions_executed` by `1`.
- `HLT` still counts as an executed instruction.
- The instruction limit still protects against infinite loops.
- Existing v0.1 CLI behavior must remain backward compatible.

## Suggested artifact layout

```text
examples/v0_2/
├── branch_forward.s
├── branch_backward_loop.s
├── cbnz_countdown.s
├── cbz_skip.s
├── cmp_beq.s
├── cmp_bcond_loop.s
├── cmp_bne.s
├── signed_compare_lt_ge.s
└── trace_loop.s

tests/v0_2/
├── test_decode_branch.c
├── test_flags.c
├── test_execute_branch.c
├── test_condition_codes.c
├── test_examples.c
└── test_cli_trace.sh
```

It is also acceptable to keep a single `tests/v0_2/test_v0_2.c` file at first, as long as test sections are clearly labeled with the test-case IDs in this plan.

## Required test categories

## 1. Regression tests from v0.1

### TC-V02-REG-001 — v0.1 suite still passes

**Purpose:** ensure branch work does not break the instruction sandbox.

**Steps:**

1. Run the full v0.1 test suite.
2. Run v0.1 CLI examples.

**Expected result:**

- All v0.1 tests pass unchanged.
- v0.1 example output remains stable.

### TC-V02-REG-002 — Straight-line execution still advances `pc` by 4

**Purpose:** ensure branch support does not alter ordinary instruction flow.

**Program:**

```asm
movz x0, #1
add  x0, x0, #1
hlt  #0
```

**Expected result:**

- `x0 == 2`
- final `pc` is the `HLT` address
- instruction count includes `HLT`

## 2. Decode tests — unconditional branch

### TC-V02-DEC-B-001 — Decode `B` forward

**Purpose:** verify decoding of a positive signed branch offset.

**Program:**

```asm
b target
nop
target:
hlt #0
```

**Expected result:**

- opcode decodes as `B`
- target/offset represents `+8` bytes from branch instruction
- no register fields are incorrectly used

### TC-V02-DEC-B-002 — Decode `B` backward

**Purpose:** verify decoding of a negative signed branch offset.

**Program shape:**

```asm
loop:
nop
b loop
```

**Expected result:**

- opcode decodes as `B`
- branch offset is negative
- sign extension is correct

### TC-V02-DEC-B-003 — Decode maximum positive `B` immediate

**Purpose:** verify upper positive immediate decoding boundary.

**Steps:**

1. Feed a hand-crafted `B` opcode with maximum positive 26-bit signed immediate.
2. Decode it.

**Expected result:**

- decoded offset is positive
- no overflow occurs during sign extension or byte conversion

### TC-V02-DEC-B-004 — Decode most negative `B` immediate

**Purpose:** verify lower negative immediate decoding boundary.

**Steps:**

1. Feed a hand-crafted `B` opcode with the most negative 26-bit signed immediate.
2. Decode it.

**Expected result:**

- decoded offset is negative
- sign extension is correct
- no unsigned wrap is misinterpreted as a positive target

## 3. Decode tests — conditional branch

### TC-V02-DEC-BCOND-001 — Decode `B.EQ`

**Program:**

```asm
b.eq target
nop
target:
hlt #0
```

**Expected result:**

- opcode decodes as conditional branch
- condition is `EQ`
- branch offset is correct

### TC-V02-DEC-BCOND-002 — Decode `B.NE`

**Expected result:**

- opcode decodes as conditional branch
- condition is `NE`

### TC-V02-DEC-BCOND-003 — Decode signed comparison conditions

**Purpose:** verify required signed condition codes are recognized.

**Cases:**

- `B.LT`
- `B.LE`
- `B.GT`
- `B.GE`

**Expected result:**

- each opcode decodes as conditional branch
- each condition value is represented distinctly

### TC-V02-DEC-BCOND-004 — Decode invalid/reserved conditional branch encoding

**Purpose:** verify invalid encodings fail clearly.

**Steps:**

1. Feed a malformed conditional branch opcode that matches the broad branch shape but violates required fixed bits.

**Expected result:**

- decode returns failure
- error message identifies unsupported or invalid instruction

## 4. Decode tests — `CBZ` / `CBNZ`

### TC-V02-DEC-CBZ-001 — Decode `CBZ Xn, label`

**Expected result:**

- opcode decodes as `CBZ`
- register index is correct
- offset is sign-extended correctly
- 64-bit form is recognized

### TC-V02-DEC-CBZ-002 — Decode `CBZ Wn, label`

**Expected result:**

- opcode decodes as `CBZ`
- register index is correct
- 32-bit form is recognized
- comparison uses lower 32 bits only

### TC-V02-DEC-CBNZ-001 — Decode `CBNZ Xn, label`

**Expected result:**

- opcode decodes as `CBNZ`
- register index is correct
- offset is sign-extended correctly

### TC-V02-DEC-CBNZ-002 — Decode `CBNZ Wn, label`

**Expected result:**

- opcode decodes as `CBNZ`
- 32-bit form is recognized

### TC-V02-DEC-CBZ-003 — Decode negative `CBZ` / `CBNZ` offset

**Purpose:** verify loops can branch backward.

**Expected result:**

- negative offset is decoded correctly
- branch target can point to an earlier instruction

## 5. Decode tests — `CMP`

### TC-V02-DEC-CMP-001 — Decode `CMP Xn, #imm`

**Expected result:**

- opcode decodes as compare-immediate or flag-setting subtract alias
- source register is correct
- immediate is correct
- no destination register write is expected

### TC-V02-DEC-CMP-002 — Decode `CMP Wn, #imm`

**Expected result:**

- 32-bit compare form is recognized
- source register is correct
- immediate is correct

### TC-V02-DEC-CMP-003 — Decode `CMP Xn, Xm`

**Expected result:**

- register-register compare is recognized
- both source registers are correct
- no destination register write is expected

### TC-V02-DEC-CMP-004 — Decode `CMP Wn, Wm`

**Expected result:**

- 32-bit register-register compare is recognized

### TC-V02-DEC-CMP-005 — Decode shifted immediate form if supported

**Purpose:** verify ARM64 immediate shift-by-12 behavior if implemented for `CMP` immediate.

**Program:**

```asm
cmp x0, #1, lsl #12
```

**Expected result:**

- immediate value is decoded as `4096`

If shift-by-12 is not implemented in v0.2, this test must be marked deferred and the decoder must reject the form clearly.

## 6. Decode tests — optional `ADD register`

These tests are required only if v0.2 implements `ADD Xd, Xn, Xm`.

### TC-V02-DEC-ADDREG-001 — Decode `ADD Xd, Xn, Xm`

**Expected result:**

- opcode decodes as `ADD register`
- destination, left source, and right source registers are correct
- 64-bit form is recognized

### TC-V02-DEC-ADDREG-002 — Decode `ADD Wd, Wn, Wm`

**Expected result:**

- 32-bit form is recognized
- writes are zero-extended into the destination `x` register

## 7. Condition-code evaluation tests

These tests can directly call a helper such as `condition_passed(flags, condition)` if one exists, or execute short programs that depend on each condition.

### TC-V02-COND-001 — `EQ` uses `Z == 1`

**Cases:**

| N | Z | C | V | Expected `EQ` |
|---|---|---|---|---|
| 0 | 1 | 0 | 0 | true |
| 0 | 0 | 0 | 0 | false |

### TC-V02-COND-002 — `NE` uses `Z == 0`

**Cases:**

| Z | Expected `NE` |
|---|---|
| 0 | true |
| 1 | false |

### TC-V02-COND-003 — `LT` uses `N != V`

**Cases:**

| N | V | Expected `LT` |
|---|---|---|
| 0 | 0 | false |
| 0 | 1 | true |
| 1 | 0 | true |
| 1 | 1 | false |

### TC-V02-COND-004 — `GE` uses `N == V`

**Cases:**

| N | V | Expected `GE` |
|---|---|---|
| 0 | 0 | true |
| 0 | 1 | false |
| 1 | 0 | false |
| 1 | 1 | true |

### TC-V02-COND-005 — `GT` uses `Z == 0 && N == V`

**Cases:**

- `Z=0, N=0, V=0` -> true
- `Z=1, N=0, V=0` -> false
- `Z=0, N=1, V=0` -> false
- `Z=0, N=1, V=1` -> true

### TC-V02-COND-006 — `LE` uses `Z == 1 || N != V`

**Cases:**

- `Z=1, N=0, V=0` -> true
- `Z=0, N=0, V=0` -> false
- `Z=0, N=1, V=0` -> true
- `Z=0, N=1, V=1` -> false

## 8. Flag update tests for `CMP`

### TC-V02-FLAG-001 — Equal comparison sets `Z`

**Program:**

```asm
movz x0, #5
cmp  x0, #5
hlt  #0
```

**Expected result:**

- `Z == 1`
- `N == 0`
- source register `x0 == 5`

### TC-V02-FLAG-002 — Greater-than comparison clears `Z` and `N`

**Program:**

```asm
movz x0, #6
cmp  x0, #5
hlt  #0
```

**Expected result:**

- `Z == 0`
- `N == 0`
- signed `GT` condition would pass

### TC-V02-FLAG-003 — Less-than comparison sets `N` when no overflow

**Program:**

```asm
movz x0, #4
cmp  x0, #5
hlt  #0
```

**Expected result:**

- `Z == 0`
- `N == 1`
- `V == 0`
- signed `LT` condition would pass

### TC-V02-FLAG-004 — Unsigned borrow clears `C`

**Purpose:** verify ARM64 subtraction carry semantics.

**Program:**

```asm
movz x0, #4
cmp  x0, #5
hlt  #0
```

**Expected result:**

- `C == 0` because subtraction borrowed

### TC-V02-FLAG-005 — No unsigned borrow sets `C`

**Program:**

```asm
movz x0, #5
cmp  x0, #4
hlt  #0
```

**Expected result:**

- `C == 1`

### TC-V02-FLAG-006 — 64-bit signed overflow sets `V`

**Purpose:** verify signed overflow detection for subtraction.

**Setup:**

Use a hand-crafted program or direct execution helper to compare:

```text
INT64_MIN - 1
```

**Expected result:**

- result sign/overflow flags match ARM64 subtraction semantics
- `V == 1`

### TC-V02-FLAG-007 — 32-bit compare uses 32-bit result width

**Purpose:** ensure W-register compare does not accidentally use upper 32 bits.

**Setup:**

1. Set `x0 = 0x0000000100000000`.
2. Execute `cmp w0, #0`.

**Expected result:**

- compare sees lower 32 bits as zero
- `Z == 1`

### TC-V02-FLAG-008 — `CMP` does not modify source registers

**Program:**

```asm
movz x0, #9
cmp  x0, #3
hlt  #0
```

**Expected result:**

- `x0 == 9`
- only flags change

## 9. Execution tests — unconditional branch

### TC-V02-EXEC-B-001 — Taken forward branch skips instruction

**Program:**

```asm
movz x0, #1
b target
movz x0, #99
target:
hlt #0
```

**Expected result:**

- `x0 == 1`
- skipped instruction is not executed
- instruction count includes `movz`, `b`, `hlt`

### TC-V02-EXEC-B-002 — Backward branch forms loop until instruction limit

**Program:**

```asm
loop:
b loop
```

**Expected result:**

- emulator stops with instruction-limit error
- instruction count equals configured limit
- process exits non-zero when run through CLI

### TC-V02-EXEC-B-003 — Branch target at first loaded instruction

**Program shape:**

```asm
start:
nop
b start
```

**Expected result:**

- branch target `0x1000` is valid
- loop is stopped by instruction limit, not invalid target

### TC-V02-EXEC-B-004 — Branch to final valid instruction

**Program:**

```asm
b final
nop
final:
hlt #0
```

**Expected result:**

- branch to final `HLT` succeeds
- execution halts successfully

### TC-V02-EXEC-B-005 — Branch outside memory fails clearly

**Purpose:** verify invalid `pc` targets are rejected.

**Steps:**

1. Use a hand-crafted branch opcode or CPU state that branches beyond memory.
2. Execute one step.

**Expected result:**

- execution fails before fetching from invalid memory or on the next fetch
- error mentions invalid branch target or invalid fetch
- no out-of-bounds memory access occurs

## 10. Execution tests — `CBZ` / `CBNZ`

### TC-V02-EXEC-CBZ-001 — `CBZ` taken when register is zero

**Program:**

```asm
movz x0, #0
cbz  x0, target
movz x1, #99
target:
hlt #0
```

**Expected result:**

- `x1 == 0`
- branch is taken

### TC-V02-EXEC-CBZ-002 — `CBZ` not taken when register is non-zero

**Program:**

```asm
movz x0, #1
cbz  x0, target
movz x1, #7
target:
hlt #0
```

**Expected result:**

- `x1 == 7`
- branch is not taken

### TC-V02-EXEC-CBNZ-001 — `CBNZ` taken when register is non-zero

**Program:**

```asm
movz x0, #1
cbnz x0, target
movz x1, #99
target:
hlt #0
```

**Expected result:**

- `x1 == 0`
- branch is taken

### TC-V02-EXEC-CBNZ-002 — `CBNZ` not taken when register is zero

**Program:**

```asm
movz x0, #0
cbnz x0, target
movz x1, #7
target:
hlt #0
```

**Expected result:**

- `x1 == 7`
- branch is not taken

### TC-V02-EXEC-CBNZ-003 — Countdown loop with `CBNZ`

**Program:**

```asm
movz x0, #5
movz x1, #0
loop:
add  x1, x1, #1
sub  x0, x0, #1
cbnz x0, loop
hlt  #0
```

**Expected result:**

- `x0 == 0`
- `x1 == 5`
- loop executes five times
- final halt is successful

### TC-V02-EXEC-CBZ-003 — 32-bit `CBZ Wn` ignores upper 32 bits

**Setup:**

1. Arrange `x0 = 0x0000000100000000`.
2. Execute `cbz w0, target`.

**Expected result:**

- lower 32 bits are zero
- branch is taken

### TC-V02-EXEC-CBNZ-004 — 32-bit `CBNZ Wn` ignores upper 32 bits

**Setup:**

1. Arrange `x0 = 0x0000000100000000`.
2. Execute `cbnz w0, target`.

**Expected result:**

- lower 32 bits are zero
- branch is not taken

## 11. Execution tests — `CMP` + `B.cond`

### TC-V02-EXEC-BCOND-001 — `CMP` + `B.EQ` taken

**Program:**

```asm
movz x0, #5
cmp  x0, #5
b.eq equal
movz x1, #99
equal:
hlt #0
```

**Expected result:**

- `x1 == 0`
- `Z == 1`

### TC-V02-EXEC-BCOND-002 — `CMP` + `B.EQ` not taken

**Program:**

```asm
movz x0, #4
cmp  x0, #5
b.eq equal
movz x1, #7
equal:
hlt #0
```

**Expected result:**

- `x1 == 7`
- `Z == 0`

### TC-V02-EXEC-BCOND-003 — `CMP` + `B.NE` taken

**Program:**

```asm
movz x0, #4
cmp  x0, #5
b.ne notequal
movz x1, #99
notequal:
hlt #0
```

**Expected result:**

- `x1 == 0`
- `Z == 0`

### TC-V02-EXEC-BCOND-004 — `CMP` + `B.NE` not taken

**Program:**

```asm
movz x0, #5
cmp  x0, #5
b.ne notequal
movz x1, #7
notequal:
hlt #0
```

**Expected result:**

- `x1 == 7`
- `Z == 1`

### TC-V02-EXEC-BCOND-005 — `B.LT` taken for signed less-than

**Program:**

```asm
movz x0, #4
cmp  x0, #5
b.lt less
movz x1, #99
less:
hlt #0
```

**Expected result:**

- branch is taken
- `x1 == 0`

### TC-V02-EXEC-BCOND-006 — `B.GE` taken for signed greater-or-equal

**Program:**

```asm
movz x0, #5
cmp  x0, #5
b.ge ge
movz x1, #99
ge:
hlt #0
```

**Expected result:**

- branch is taken
- `x1 == 0`

### TC-V02-EXEC-BCOND-007 — `B.GT` taken for signed greater-than

**Program:**

```asm
movz x0, #6
cmp  x0, #5
b.gt greater
movz x1, #99
greater:
hlt #0
```

**Expected result:**

- branch is taken
- `x1 == 0`

### TC-V02-EXEC-BCOND-008 — `B.LE` taken for signed less-or-equal equality case

**Program:**

```asm
movz x0, #5
cmp  x0, #5
b.le le
movz x1, #99
le:
hlt #0
```

**Expected result:**

- branch is taken
- `x1 == 0`

### TC-V02-EXEC-BCOND-009 — `B.LE` taken for signed less-than case

**Program:**

```asm
movz x0, #4
cmp  x0, #5
b.le le
movz x1, #99
le:
hlt #0
```

**Expected result:**

- branch is taken
- `x1 == 0`

## 12. Execution tests — optional `ADD register`

These tests are required if `ADD register` is implemented.

### TC-V02-EXEC-ADDREG-001 — Add two registers

**Program:**

```asm
movz x0, #2
movz x1, #3
add  x2, x0, x1
hlt  #0
```

**Expected result:**

- `x2 == 5`

### TC-V02-EXEC-ADDREG-002 — 32-bit `ADD Wd, Wn, Wm` zero-extends

**Setup:**

1. Set a register so upper 32 bits would be non-zero if preserved.
2. Execute 32-bit `ADD`.

**Expected result:**

- destination upper 32 bits are zero

### TC-V02-EXEC-ADDREG-003 — `ADD register` writing to `xzr` is ignored

**Expected result:**

- write to register index 31 does not alter CPU state except `pc` and instruction count

## 13. `pc` and instruction count tests

### TC-V02-PC-001 — Taken branch sets exact target `pc`

**Purpose:** verify target calculation.

**Steps:**

1. Execute one taken branch from `0x1000` to `0x100c`.
2. Inspect `pc` immediately after the branch step.

**Expected result:**

- `pc == 0x100c`

### TC-V02-PC-002 — Not-taken branch advances by 4

**Steps:**

1. Execute one not-taken conditional branch at `0x1000`.
2. Inspect `pc`.

**Expected result:**

- `pc == 0x1004`

### TC-V02-PC-003 — Branch instruction increments instruction count

**Expected result:**

- one branch step increments `instructions_executed` by `1`

### TC-V02-PC-004 — Loop instruction count is exact

**Program:**

```asm
movz x0, #3
loop:
sub  x0, x0, #1
cbnz x0, loop
hlt  #0
```

**Expected result:**

- `movz` executes once
- `sub` executes three times
- `cbnz` executes three times
- `hlt` executes once
- total instruction count is `8`

### TC-V02-PC-005 — Branch to self respects instruction limit exactly

**Program:**

```asm
b .
```

**Expected result:**

- emulator stops at configured instruction limit
- count equals configured limit
- no integer overflow in instruction counter

## 14. Trace mode tests

### TC-V02-TRACE-001 — Trace mode prints executed instruction addresses

**Command:**

```sh
./emulator trace examples/v0_2/cbnz_countdown.bin
```

or, if implemented as an option:

```sh
./emulator run --trace examples/v0_2/cbnz_countdown.bin
```

**Expected result:**

- output includes one line per executed instruction before final dump
- each line includes a stable hex `pc` address
- branch loops show repeated addresses

### TC-V02-TRACE-002 — Trace mode still prints final register dump

**Expected result:**

- trace output does not replace final state output
- final dump format remains compatible with v0.1 tests

### TC-V02-TRACE-003 — Trace mode reports errors with last executed address

**Program:**

```asm
b .
```

with a very small instruction limit.

**Expected result:**

- non-zero exit
- error mentions instruction limit
- trace shows repeated branch address

### TC-V02-TRACE-004 — Normal run mode does not print trace lines

**Purpose:** preserve CLI cleanliness and v0.1 output expectations.

**Expected result:**

- `./emulator run ...` does not print per-instruction trace lines unless explicitly requested

## 15. CLI tests

### TC-V02-CLI-001 — `run` supports v0.2 loop example

**Command:**

```sh
./emulator run examples/v0_2/cbnz_countdown.bin
```

**Expected result:**

- exits `0`
- output shows `halted`
- output shows expected final registers

### TC-V02-CLI-002 — Trace command or flag has usage help

**Command examples:**

```sh
./emulator trace
./emulator run --trace
```

**Expected result:**

- invalid trace invocation exits non-zero
- usage text explains correct trace syntax

### TC-V02-CLI-003 — Invalid command still fails

**Purpose:** ensure CLI validation remains strict.

**Command:**

```sh
./emulator branch examples/v0_2/cbnz_countdown.bin
```

**Expected result:**

- exits non-zero
- prints usage or unknown command error

### TC-V02-CLI-004 — Instruction-limit failure exits non-zero

**Command:**

```sh
./emulator run examples/v0_2/infinite_branch.bin
```

or use a test helper to configure a smaller instruction limit.

**Expected result:**

- exits non-zero
- error mentions instruction limit
- no final successful `halted` message

## 16. Error and edge-case tests

### TC-V02-ERR-001 — Branch to unaligned address fails

**Purpose:** ARM64 instruction fetch requires 4-byte alignment.

**Steps:**

1. Hand-craft a CPU state or branch target that results in `pc % 4 != 0`.
2. Execute/fetch next instruction.

**Expected result:**

- emulator fails with misaligned `pc` error

Note: normal ARM64 branch immediates are scaled by 4, so assembled `B`/`B.cond`/`CBZ`/`CBNZ` cannot naturally target unaligned addresses. This test may require direct state manipulation or a helper.

### TC-V02-ERR-002 — Branch to address before memory start fails

**Expected result:**

- target is rejected or next fetch fails cleanly
- no unsigned wrap causes access near the end of memory

### TC-V02-ERR-003 — Branch target arithmetic overflow is handled

**Purpose:** protect `pc + signed_offset` calculation.

**Steps:**

1. Set `pc` near `UINT64_MAX` in a direct unit test.
2. Execute branch target calculation.

**Expected result:**

- overflow is detected or target is rejected
- emulator does not wrap silently to a valid-looking address

### TC-V02-ERR-004 — Conditional branch with unsupported condition fails or is documented

**Expected result:**

- unsupported condition does not execute with incorrect behavior
- decoder or executor returns clear error

### TC-V02-ERR-005 — Infinite loop without `HLT` stops by instruction limit

**Program:**

```asm
b .
```

**Expected result:**

- emulator returns error
- instruction count equals limit
- no hang

### TC-V02-ERR-006 — Zero instruction limit rejects execution

**Steps:**

1. Initialize emulator with `instruction_limit = 0`.
2. Attempt to run any program.

**Expected result:**

- run returns instruction-limit error before executing instructions
- instruction count remains `0`

### TC-V02-ERR-007 — Branch to valid memory containing unsupported opcode fails on decode

**Program shape:**

```asm
b data
.word 0xffffffff
data:
.word 0xffffffff
```

**Expected result:**

- branch itself succeeds if target is aligned and in range
- next decode fails with unsupported opcode

## 17. Acceptance tests

### TC-V02-ACC-001 — Countdown loop demo

**Program:**

```asm
movz x0, #5
movz x1, #0
loop:
add  x1, x1, #1
sub  x0, x0, #1
cbnz x0, loop
hlt  #0
```

**Expected result:**

- `x0 == 0`
- `x1 == 5`
- execution halts successfully

### TC-V02-ACC-002 — Sum loop demo

If `ADD register` is implemented:

```asm
movz x0, #5      // counter
movz x1, #0      // sum
loop:
add  x1, x1, x0
sub  x0, x0, #1
cbnz x0, loop
hlt  #0
```

**Expected result:**

- `x0 == 0`
- `x1 == 15`
- execution halts successfully

If `ADD register` is deferred, use an immediate-counting loop instead and document that full sum-loop acceptance is deferred.

### TC-V02-ACC-003 — `CMP` + condition demo

**Program:**

```asm
movz x0, #7
cmp  x0, #7
b.eq equal
movz x1, #99
equal:
movz x2, #1
hlt  #0
```

**Expected result:**

- `x1 == 0`
- `x2 == 1`
- `Z == 1`

### TC-V02-ACC-004 — Trace demo

**Command:**

```sh
./emulator trace examples/v0_2/cbnz_countdown.bin
```

or documented equivalent.

**Expected result:**

- trace shows repeated loop addresses
- final state is still printed
- program exits `0`

## 18. Documentation requirements

Before v0.2 is considered complete, update:

- `README.md`
  - v0.2 build/run examples
  - trace command or flag syntax
  - list of newly supported instructions
  - updated definition of done
- `docs/test-plan-v0.2.md`
  - mark any deferred optional tests clearly
  - record final implementation decisions
- `education/`
  - add or update a learning guide explaining branches, `pc`, labels, flags, and loops
- `docs/instruction-support.md` if such a file exists by then

## Release checklist

v0.2 can be considered complete only when:

- all v0.1 tests still pass
- all required v0.2 tests pass
- all required examples build with `make examples`
- `make test` runs both v0.1 and v0.2 tests
- countdown loop acceptance demo passes
- at least one `CMP` + `B.cond` demo passes
- instruction-limit infinite-loop test passes
- trace mode is either implemented and tested, or explicitly deferred with README/test-plan updates
- unsupported/invalid branch cases fail clearly
- docs reflect the actual implemented instruction set

## Deferred-test policy

A test from this plan may be deferred only if:

1. the feature is explicitly out of scope or optional for v0.2,
2. the deferral is recorded in this file,
3. the README does not claim the feature is supported, and
4. the implementation rejects the unsupported behavior clearly rather than silently doing the wrong thing.


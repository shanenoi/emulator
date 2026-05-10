# v0.4 Test Plan — Functions

## Purpose

This test plan defines the required tests for **v0.4 — Functions**.

v0.1 proved straight-line instruction execution. v0.2 added branches, loops, and condition flags. v0.3 added memory and stack operations. v0.4 adds function-call control flow: calling a subroutine, saving the return address in the link register, returning to the caller, and observing call/return flow in trace output.

The goal of v0.4 is to make the emulator capable of running small hand-written ARM64 assembly programs that use `BL` and `RET`, including nested calls and simple stack-frame style examples built from already-supported v0.3 `STP` / `LDP` instructions.

## Current implementation status

This is a planning document for the next development pass. At the time this plan is written, v0.4 runtime code and v0.4 automated tests are not implemented yet.

Expected future artifacts:

```text
examples/v0_4/
├── simple_call.s
├── nested_calls.s
├── call_with_stack_frame.s
├── ret_x30.s
├── ret_custom_register.s
└── invalid_return.s

tests/v0_4/
├── test_v0_4.c
└── test_cli_functions.sh
```

## Scope

### In scope

- Preserve all v0.1, v0.2, and v0.3 behavior and tests.
- Decode and execute `BL <label>`.
- Decode and execute `RET`.
- Decode and execute `RET Xn` where straightforward, with `RET` defaulting to `X30`.
- Correctly write the return address to link register `X30` for `BL`.
- Correctly branch to the target function address for `BL`.
- Correctly return to the address stored in the selected return register for `RET`.
- Support simple nested calls.
- Support simple stack-frame examples using existing `STP` / `LDP` behavior.
- Add examples for simple call, nested call, stack-frame call, custom-register return, and invalid return.
- Add trace output that is useful for observing calls and returns.
- Add optional call-trace output if implemented as a separate CLI mode or trace enhancement.
- Add automated tests for decode, execution, nested calls, stack-frame examples, invalid return targets, CLI behavior, and examples.

### Out of scope

- Full AAPCS64 ABI enforcement.
- Full C compiler calling convention support.
- Dynamic stack unwinding.
- Symbolic function names in traces unless a symbol-map feature is deliberately added.
- Full debugger REPL; that starts in v0.5.
- ELF loading.
- Mach-O loading.
- Syscalls.
- Virtual memory and page permissions.
- Memory-mapped devices.
- Function prologue/epilogue validation beyond simple observable behavior.
- `BR`, `BLR`, `ERET`, exception returns, or indirect branching beyond `RET Xn`.
- Pointer authentication instructions.
- Floating point, SIMD, NEON, or system instructions.

## Required instruction support

v0.4 must support the following new instruction family.

### Branch with link

Required:

- `BL <label>`

Expected behavior:

- `X30` receives the return address, normally `old_pc + 4`.
- `PC` receives the branch target.
- The branch offset is a signed immediate encoded relative to the address of the `BL` instruction.
- The target must be 4-byte aligned and inside emulator memory.
- A failed `BL` target validation must not modify `X30` or `PC`.

### Return

Required:

- `RET`

Recommended if straightforward:

- `RET Xn`

Expected behavior:

- `RET` without an explicit register returns to `X30`.
- `RET Xn` returns to the address stored in register `Xn`.
- The return target must be 4-byte aligned and inside emulator memory.
- A failed return target validation must not modify `PC`.
- `RET XZR` is invalid or must fail cleanly because returning to zero will not be a valid loaded program target in this emulator.

### Optional helper instructions

The v0.4 demo in the README uses:

```asm
add x0, x0, x1
```

The current emulator does not yet implement `ADD register`. v0.4 has two acceptable options:

1. Implement `ADD register` and test it because the examples use it.
2. Keep `ADD register` deferred and write v0.4 examples using already-supported instructions such as `ADD immediate`, `SUB immediate`, memory, and branches.

The chosen path must be documented. If `ADD register` remains deferred, tests must ensure v0.4 function behavior does not accidentally depend on it.

## Assumptions and implementation decisions

- ARM64 instructions remain fixed-width 32-bit little-endian instructions.
- Raw binaries are still loaded at `0x1000`.
- Initial `PC` remains `0x1000`.
- Initial `SP` remains `0x100000`.
- `HLT` still counts as an executed instruction and leaves `PC` at the `HLT` address.
- `BL` counts as one executed instruction.
- `RET` counts as one executed instruction.
- `BL` stores the return address in `X30`, also known as the link register or `LR`.
- `RET` does not modify `X30` by itself.
- Branch and return target validation should reuse or mirror v0.2 branch target validation.
- A failed call or return must leave CPU state as unchanged as practical, especially `PC` and `X30` for failed `BL`.
- Stack-frame examples should use v0.3 `STP` / `LDP` behavior; v0.4 should not introduce new memory semantics unless explicitly required.
- Trace output must remain stable enough for tests to match.

## Suggested artifact layout

```text
examples/v0_4/
├── simple_call.s
├── nested_calls.s
├── call_with_stack_frame.s
├── ret_x30.s
├── ret_custom_register.s
└── invalid_return.s

tests/v0_4/
├── test_v0_4.c
└── test_cli_functions.sh
```

It is acceptable to keep all C tests in one `tests/v0_4/test_v0_4.c` file initially, as long as sections are clearly labeled with test-case IDs from this plan.

## Required test categories

## 1. Regression tests from previous versions

### TC-V04-REG-001 — v0.1, v0.2, and v0.3 suites still pass

**Purpose:** ensure function-call support does not break earlier instruction execution, branching, or memory/stack behavior.

**Steps:**

1. Run the full v0.1 suite.
2. Run the full v0.2 suite.
3. Run the full v0.3 suite.
4. Run existing CLI examples.

**Expected result:**

- All previous tests pass unchanged.
- Existing example outputs remain stable.

### TC-V04-REG-002 — Existing trace behavior remains stable

**Purpose:** protect v0.2 trace behavior while adding call/return visibility.

**Expected result:**

- `./emulator trace <raw-binary>` still prints executed `PC` values in order.
- Existing v0.2/v0.3 trace tests still pass.
- Any additional call/return text is either absent in normal trace mode or documented and tested if intentionally added.

## 2. Decode tests — `BL`

### TC-V04-DEC-BL-001 — Decode forward `BL`

**Program:**

```asm
bl target
hlt #0

target:
ret
```

**Expected result:**

- Decodes as `BL`.
- Offset is positive.
- Target is `PC + offset`.
- Link-register write is recognized as part of execution, not as a separate decoded destination register unless the implementation models it that way.

### TC-V04-DEC-BL-002 — Decode backward `BL`

**Program pattern:**

```asm
b start

helper:
ret

start:
bl helper
hlt #0
```

**Expected result:**

- Decodes as `BL`.
- Offset is negative.
- Target points backward to `helper`.

### TC-V04-DEC-BL-003 — Decode maximum positive `BL` offset

**Purpose:** verify sign-extension and offset scaling boundaries.

**Expected result:**

- Maximum positive immediate is decoded as a positive byte offset.
- No sign-extension error occurs.

### TC-V04-DEC-BL-004 — Decode most negative `BL` offset

**Purpose:** verify sign-extension for negative call offsets.

**Expected result:**

- Most negative immediate is decoded as a negative byte offset.
- No unsigned wrap is introduced by decoding.

### TC-V04-DEC-BL-005 — Reject corrupted `BL`-like unsupported encodings

**Purpose:** ensure decoder does not over-match unrelated branch encodings.

**Expected result:**

- Valid `BL` decodes.
- Similar but unsupported encodings fail with a clear decode error.

## 3. Decode tests — `RET`

### TC-V04-DEC-RET-001 — Decode default `RET`

**Program:**

```asm
ret
```

**Expected result:**

- Decodes as `RET`.
- Return register defaults to `X30`.

### TC-V04-DEC-RET-002 — Decode explicit `RET X30`

**Program:**

```asm
ret x30
```

**Expected result:**

- Decodes as `RET`.
- Return register is `30`.

### TC-V04-DEC-RET-003 — Decode explicit `RET X0`

**Program:**

```asm
ret x0
```

**Expected result:**

- Decodes as `RET`.
- Return register is `0`.

### TC-V04-DEC-RET-004 — Reject unsupported `RET` variants

**Purpose:** ensure decoder does not accidentally accept unrelated system/branch instructions.

**Expected result:**

- Corrupted `RET` encodings fail with a clear decode error.

## 4. Execution tests — simple calls

### TC-V04-EXEC-BL-001 — `BL` writes `X30` return address

**Program:**

```asm
bl target
hlt #0

target:
hlt #0
```

**Expected result:**

- After executing `BL`, `X30 == address_of_instruction_after_bl`.
- `PC == address_of_target`.
- Instruction count increments by `1`.

### TC-V04-EXEC-BL-002 — `BL` branches to target

**Program:**

```asm
movz x0, #1
bl target
movz x0, #2
hlt #0

target:
movz x1, #3
hlt #0
```

**Expected result:**

- Execution reaches `target` after `BL`.
- `x1 == 3`.
- The instruction immediately after `BL` is skipped unless target returns.

### TC-V04-EXEC-RET-001 — `RET` returns through `X30`

**Program:**

```asm
movz x0, #1
bl target
movz x0, #2
hlt #0

target:
movz x1, #3
ret
```

**Expected result:**

- Function executes `movz x1, #3`.
- `RET` returns to the instruction after `BL`.
- Final `x0 == 2`.
- Final `x1 == 3`.
- Final `PC` is the `HLT` address.

### TC-V04-EXEC-RET-002 — `RET X0` returns through custom register

**Program pattern:**

```asm
adr-like setup is not available, so construct this test by directly setting x0 in the test harness to a valid address before executing RET X0.
```

**Expected result:**

- `RET X0` sets `PC` to the address stored in `X0`.
- `X30` is unchanged.
- Instruction count increments by `1`.

If explicit `RET Xn` is deferred, this test must be marked deferred and `RET Xn` must fail clearly.

### TC-V04-EXEC-RET-003 — `RET` does not modify `X30`

**Purpose:** ensure return uses but does not clobber the link register.

**Expected result:**

- `X30` remains equal to the return address after `RET`.

### TC-V04-EXEC-CALL-001 — Simple function result

**Program:**

```asm
movz x0, #2
bl add_three
hlt #0

add_three:
add x0, x0, #3
ret
```

**Expected result:**

- Final `x0 == 5`.
- `PC` ends at `HLT`.
- Execution count matches the exact instruction path.

## 5. Execution tests — nested calls

### TC-V04-EXEC-NEST-001 — Nested call returns to correct caller

**Program:**

```asm
movz x0, #0
bl first
hlt #0

first:
add x0, x0, #1
bl second
add x0, x0, #1
ret

second:
add x0, x0, #1
ret
```

**Important note:** this program overwrites `X30` when `first` calls `second`. It is expected to fail or loop unless `first` saves/restores `X30`.

**Expected result:**

- This example must be documented as invalid nested-call code without saving `X30`.
- It should be used as an educational negative case or replaced by the stack-saving version below for successful nested calls.

### TC-V04-EXEC-NEST-002 — Nested call with saved `X30`

**Program:**

```asm
movz x0, #0
bl first
hlt #0

first:
stp x29, x30, [sp, #-16]!
add x0, x0, #1
bl second
add x0, x0, #1
ldp x29, x30, [sp], #16
ret

second:
add x0, x0, #1
ret
```

**Expected result:**

- Final `x0 == 3`.
- `SP` returns to its initial value.
- `X30` is restored by `LDP` before `RET`.
- Execution reaches final `HLT`.

### TC-V04-EXEC-NEST-003 — Multiple sequential calls

**Program:**

```asm
movz x0, #0
bl inc
bl inc
bl inc
hlt #0

inc:
add x0, x0, #1
ret
```

**Expected result:**

- Final `x0 == 3`.
- Each `BL` writes a new `X30` return address.
- Each `RET` returns to the correct next instruction.

## 6. Execution tests — stack-frame style calls

### TC-V04-EXEC-FRAME-001 — Save/restore `X29` and `X30`

**Program:**

```asm
movz x29, #0x1234
bl framed
hlt #0

framed:
stp x29, x30, [sp, #-16]!
movz x29, #0xabcd
add x0, x0, #1
ldp x29, x30, [sp], #16
ret
```

**Expected result:**

- `SP` returns to initial value.
- `X29` returns to original value.
- `X30` returns to the caller return address.
- `x0` is incremented.

### TC-V04-EXEC-FRAME-002 — Failed stack save prevents unsafe call continuation

**Purpose:** test interaction between v0.3 memory failure and v0.4 call flow.

**Program pattern:**

- Set `SP` too low for `stp x29, x30, [sp, #-16]!`.
- Execute framed function prologue.

**Expected result:**

- `STP` fails cleanly.
- `SP`, `X29`, and `X30` are not partially modified by the failed pair store.
- Emulator stops with a memory error.

## 7. Error and edge-case tests

### TC-V04-ERR-001 — `BL` target before memory fails

**Purpose:** validate negative call target bounds checking.

**Expected result:**

- `BL` target calculation fails.
- `PC` and `X30` are not updated.
- Error message is clear.

This may require direct helper testing if no valid assembly encoding can naturally produce the target in the 1 MiB memory range.

### TC-V04-ERR-002 — `BL` target beyond memory fails

**Purpose:** validate positive call target bounds checking.

**Expected result:**

- `BL` target calculation fails.
- `PC` and `X30` are not updated.
- Error message is clear.

### TC-V04-ERR-003 — `BL` target overflow fails

**Purpose:** protect against unsigned arithmetic wraparound.

**Expected result:**

- Overflow is detected.
- `PC` and `X30` remain unchanged.

### TC-V04-ERR-004 — `RET` to unaligned address fails

**Setup:**

- Set return register to `0x1001`.
- Execute `RET`.

**Expected result:**

- Emulator reports invalid return target.
- `PC` remains at the `RET` instruction.

### TC-V04-ERR-005 — `RET` outside memory fails

**Setup:**

- Set return register to `0x100000` or higher.
- Execute `RET`.

**Expected result:**

- Emulator reports invalid return target.
- `PC` remains unchanged.

### TC-V04-ERR-006 — `RET` to unsupported opcode target fails on next step

**Purpose:** distinguish valid control-flow target from valid instruction stream.

**Program pattern:**

- Return target is aligned and inside memory.
- Target memory contains an unsupported opcode.

**Expected result:**

- `RET` itself succeeds.
- The next instruction fetch/decode fails with unsupported opcode.

### TC-V04-ERR-007 — Infinite recursive call hits instruction limit

**Program:**

```asm
recurse:
bl recurse
```

**Expected result:**

- Emulator stops at instruction limit.
- Error clearly indicates instruction limit.
- No host crash or unbounded memory usage occurs.

### TC-V04-ERR-008 — Link-register clobber negative case is documented

**Program pattern:**

```asm
bl first
hlt #0

first:
bl second
ret

second:
ret
```

**Expected result:**

- Without saving `X30`, `first` cannot return to its caller correctly.
- Test should either assert instruction-limit behavior or use this only as an education/example negative case.
- Documentation must explain why nested functions need to preserve `X30`.

## 8. Trace and call-trace tests

### TC-V04-TRACE-001 — Trace shows simple call path

**Command:**

```sh
./emulator trace examples/v0_4/simple_call.bin
```

**Expected result:**

- Trace includes the `BL` address.
- Trace jumps to the function address.
- Trace includes the `RET` address.
- Trace resumes at the instruction after `BL`.
- Final register dump matches normal `run` mode.

### TC-V04-TRACE-002 — Trace shows nested call path

**Command:**

```sh
./emulator trace examples/v0_4/nested_calls.bin
```

**Expected result:**

- Trace order reflects caller -> callee -> nested callee -> caller return flow.
- Final state matches expected nested-call result.

### TC-V04-TRACE-003 — Optional call-trace output

If v0.4 adds call-specific trace lines, test for stable output such as:

```text
call from 0x0000000000001004 to 0x0000000000001010 return=0x0000000000001008
ret  from 0x0000000000001014 to 0x0000000000001008
```

If call-specific trace output is deferred, document that normal `trace pc=...` output is the v0.4 trace mechanism.

## 9. CLI tests

### TC-V04-CLI-001 — Run simple call example

**Command:**

```sh
./emulator run examples/v0_4/simple_call.bin
```

**Expected result:**

- Exit status is `0`.
- Output includes `halted`.
- Output includes expected final register values.

### TC-V04-CLI-002 — Run nested call example

**Command:**

```sh
./emulator run examples/v0_4/nested_calls.bin
```

**Expected result:**

- Exit status is `0`.
- Output includes expected nested-call final result.
- `SP` is restored if the example uses a saved frame.

### TC-V04-CLI-003 — Run stack-frame example

**Command:**

```sh
./emulator run examples/v0_4/call_with_stack_frame.bin
```

**Expected result:**

- Exit status is `0`.
- `SP` returns to `0x100000`.
- Expected result register is updated.

### TC-V04-CLI-004 — Run invalid return example

**Command:**

```sh
./emulator run examples/v0_4/invalid_return.bin
```

**Expected result:**

- Exit status is non-zero.
- Output mentions invalid return target or invalid branch target.
- Final register dump is printed if that remains the established error behavior.

### TC-V04-CLI-005 — Trace simple call example

**Command:**

```sh
./emulator trace examples/v0_4/simple_call.bin
```

**Expected result:**

- Exit status is `0`.
- Output contains trace lines for call target and return target.
- Final result matches normal `run` mode.

## 10. Acceptance tests

### TC-V04-ACC-001 — Simple call acceptance

**Program:**

```asm
movz x0, #2
bl add_three
hlt #0

add_three:
add x0, x0, #3
ret
```

**Expected result:**

- Final `x0 == 5`.

### TC-V04-ACC-002 — Sequential calls acceptance

**Program:**

```asm
movz x0, #0
bl inc
bl inc
bl inc
hlt #0

inc:
add x0, x0, #1
ret
```

**Expected result:**

- Final `x0 == 3`.

### TC-V04-ACC-003 — Nested call with frame acceptance

**Program:**

```asm
movz x0, #0
bl first
hlt #0

first:
stp x29, x30, [sp, #-16]!
add x0, x0, #1
bl second
add x0, x0, #1
ldp x29, x30, [sp], #16
ret

second:
add x0, x0, #1
ret
```

**Expected result:**

- Final `x0 == 3`.
- Final `SP == 0x100000`.
- Program halts successfully.

### TC-V04-ACC-004 — Custom return register acceptance

If `RET Xn` is implemented:

- Test a small harness that sets `X0` to a valid return target and executes `RET X0`.
- Verify `PC` becomes that target.

If `RET Xn` is deferred:

- Document deferral.
- Verify unsupported explicit return registers fail clearly.

### TC-V04-ACC-005 — Trace acceptance

**Command:**

```sh
./emulator trace examples/v0_4/simple_call.bin
```

**Expected result:**

- Trace output clearly demonstrates call and return control flow.

## 11. Documentation requirements

Before v0.4 is complete, update:

- `README.md`
  - Add v0.4 to current implementation status after runtime lands.
  - Add build/run examples for v0.4 function calls.
  - Document any `ADD register` decision.
  - Document whether call-specific trace output exists or normal trace mode is used.
- `docs/test-plan-v0.4.md`
  - Update current implementation status when runtime and tests land.
  - Mark any deferred optional items clearly.
- `education/v0.4-learning-guide.md`
  - Explain `BL`, `RET`, `X30` / `LR`, return addresses, and nested-call `X30` clobbering.
  - Explain why nested calls need stack saving of `X30`.
  - Walk through a simple call step-by-step.
  - Walk through a stack-frame nested call step-by-step.
- `examples/v0_4/`
  - Include clear comments in each `.s` file.

## 12. Release checklist

v0.4 is complete only when:

- `make clean && make test` passes.
- `make examples` builds all v0.1 through v0.4 examples.
- All required v0.1, v0.2, v0.3, and v0.4 tests pass.
- Simple call example works.
- Sequential call example works.
- Nested call with saved `X30` works.
- Stack-frame example restores `SP`.
- Invalid return example fails cleanly.
- Trace output demonstrates call/return flow.
- Documentation explains `BL`, `RET`, `X30`, and nested-call preservation.
- Deferred optional features are explicitly documented.

## 13. Suggested implementation order

Recommended order:

1. Extend decoded-instruction representation for `BL` and `RET`.
2. Decode `BL` signed immediate offset.
3. Decode `RET` default `X30`.
4. Execute `BL`: validate target, write `X30`, update `PC`.
5. Execute `RET`: validate target from return register, update `PC`.
6. Add simple call example.
7. Add sequential call example.
8. Add nested call with saved `X30` example.
9. Add stack-frame example.
10. Add invalid return example.
11. Add trace/CLI coverage.
12. Add v0.4 automated tests.
13. Update README and education guide.

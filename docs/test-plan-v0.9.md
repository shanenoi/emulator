# v0.9 Test Plan — Tiny Freestanding C Programs

## Version Goal

v0.9 lets the emulator run **small freestanding C programs** that are compiled to AArch64 ELF64 and linked without libc.

Before v0.9, v0.8 can load simple static ELF64 files, but the examples are still mostly hand-written assembly. v0.9 adds the next learning step: provide a tiny `_start` assembly stub, call a C `main`, and return the result through the existing fake `exit` syscall.

This version is still **not** a Linux process emulator. It should not try to run normal C programs that depend on libc, dynamic linking, argv/envp/auxv, broad Linux syscalls, or operating-system startup code.

The main promise is:

```text
compile tiny freestanding C -> link simple static AArch64 ELF -> run in emulator
```

## Scope

### In Scope

- v0.1 through v0.8 regression tests must continue to pass.
- Keep raw `.bin` and supported ELF64 `ET_EXEC` behavior unchanged.
- Add enough AArch64 instruction support for deliberately small freestanding C examples.
- Add examples that demonstrate C code running through the v0.8 ELF loader.
- Add a tiny `_start` assembly runtime that:
  - calls `main`,
  - keeps normal `BL`/`RET` behavior visible,
  - moves `main`'s return value into `x0`,
  - calls fake `exit = 93` through `SVC #0`.
- Support C examples compiled with a documented, constrained compiler command, such as:

```sh
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -fno-stack-protector \
  -fno-pic -fno-pie -O0 -c examples/v0_9/fib.c -o examples/v0_9/fib.o
```

- Tests must not require a cross compiler to be installed. They should generate deterministic instruction/ELF fixtures directly, or skip optional real-toolchain smoke tests when the toolchain is unavailable.
- Add instruction decode, execution, and formatting for the selected v0.9 instruction subset.
- Add `run`, `trace`, `regs`, `dump`, and `debug` coverage for C-originated ELF programs.
- Add documentation and a lesson that explain freestanding C, `_start`, fake syscalls, and why normal libc programs are still out of scope.

### Recommended v0.9 Instruction Subset

The exact implementation may be smaller if examples are adjusted. The selected v0.9 profile supports the following common compiler-emitted building blocks and intentionally excludes logical-immediate instructions plus sign-extension aliases unless future examples require them:

- Move/build constants:
  - `MOVK`
- Register arithmetic:
  - `ADD` register
  - `SUB` register
- Logical operations:
  - `AND` register
  - `ORR` register
  - `EOR` register
  - logical-immediate forms are documented as unsupported in the selected v0.9 profile
- Shifts:
  - `LSL` immediate alias / shifted-register form
  - `LSR` immediate alias / shifted-register form
  - `ASR` immediate alias / shifted-register form
- Multiply/divide:
  - `MUL`
  - `UDIV`
  - `SDIV`
- Address generation:
  - `ADR`
  - `ADRP`
  - `ADD` immediate using page offset after `ADRP`
- Byte and halfword memory operations if used by string/memory examples:
  - `LDRB`
  - `STRB`
  - `LDRH`
  - `STRH`
- Sign/zero extending operations:
  - the selected examples avoid `UXTB`/`UXTH`/`SXTW`; those aliases are documented as unsupported in v0.9 unless added later.

### Out of Scope

- Running dynamically linked C programs.
- libc startup, `crt1.o`, `printf`, `malloc`, file I/O, or normal Linux process setup.
- `argc`, `argv`, environment variables, and auxiliary vector.
- Relocations beyond what v0.8 already supports; use fully linked `ET_EXEC` files with resolved addresses.
- PIE/`ET_DYN` support.
- Thread-local storage.
- Floating-point/SIMD instructions.
- C++ runtime support, exceptions, or unwinding.
- Real Linux syscall compatibility beyond the fake `write` and `exit` ABI from v0.7.
- MMU/page permission enforcement.
- Optimizing compiler support as a broad guarantee. v0.9 may document one known-good compiler/flag profile and reject unsupported instructions outside that profile.

## Implementation Assumptions

1. v0.9 programs are still loaded through `emulator_load_program()` from v0.8.
2. The supported binary format is still ELF64 little-endian AArch64 `ET_EXEC`.
3. `_start` is provided by this repository rather than by libc.
4. `_start` calls `main` using normal AArch64 call/return behavior.
5. `main` returns an `int`; `_start` maps the low 8 bits of that return value to fake `exit(status)`.
6. The stack starts at the same v0.8 top-of-memory address unless a later version adds a real process stack layout.
7. Tests should not depend on the host having a cross compiler or AArch64 linker.
8. Build examples may use a cross compiler when available, but `make test` should pass without one.
9. Trace/disassembly formatting must be stable for every new instruction.
10. Unsupported instructions must fail with readable context: `pc`, raw opcode, and clear unsupported/decode text.
11. All new instruction implementations must preserve register-31 behavior: zero register for ALU operands/destinations, stack pointer only for forms where ARM64 defines it.
12. 32-bit `w` destination writes must zero-extend into the corresponding `x` register.
13. Arithmetic/logical operations wrap modulo 32 or 64 bits according to operand width.
14. Division by zero should be deterministic and documented. Recommended: match AArch64 behavior and write zero for `UDIV`/`SDIV` division by zero.
15. `SDIV` signed overflow should be deterministic. Recommended: match AArch64-style wrap result for `INT_MIN / -1`.
16. `ADRP` should compute page-relative addresses using the instruction `pc` page, not `pc + 4`.
17. Byte/halfword loads zero-extend unless a sign-extending variant is explicitly implemented.
18. Failed memory accesses must not partially update destination registers or write-back bases.
19. Examples should run well below the default instruction limit.
20. The lesson should explain the difference between hosted C and freestanding C before showing compiler commands.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v0.9.md
examples/v0_9/start.s
examples/v0_9/return_42.c
examples/v0_9/fib.c
examples/v0_9/sum_array.c
examples/v0_9/string_len.c
examples/v0_9/hello_c.c
examples/v0_9/nested_calls.c
examples/v0_9/stack_locals.c
examples/v0_9/byte_copy.c
examples/v0_9/static_local.c
examples/v0_9/stderr_c.c
examples/v0_9/bad_fd_c.c
examples/v0_9/unknown_syscall_c.c
examples/v0_9/invalid_write_c.c
examples/v0_9/hosted_printf.c
examples/v0_9/linker.ld
tests/v0_9/test_v0_9.c
tests/v0_9/test_cli_c_programs.sh
tests/v0_9/test_optional_c_examples.sh
lessons/v0.9-tiny-c-programs.md
```

## Test Data Strategy

Tests should separate three kinds of evidence:

1. **Instruction fixtures:** raw 32-bit opcodes generated directly in tests to validate decode/execute/formatting for every new instruction.
2. **Tiny ELF fixtures:** minimal ELF files generated directly, reusing the v0.8 strategy, to validate loader + new instructions without a cross compiler dependency.
3. **Optional real compiler smoke tests:** run only when a supported cross compiler is detected. These should be skipped, not failed, when unavailable.
   - The optional smoke script should build and run the actual `examples/v0_9/*.c` programs when `clang` and `ld.lld` are available.
   - If an example recipe prints a skip message, the `.elf` target was not produced and should not be executed.

Recommended CLI fixture programs:

- `return_42.elf`: `_start -> main -> exit(42)`.
- `fib.elf`: recursive Fibonacci returns `55` for `fib(10)`.
- `sum_array.elf`: loads `{1, 2, 3, 4, 5}` from data memory and returns `15`.
- `strlen.elf`: scans `"hello"` until NUL and returns `5`.
- `hello_c.elf`: C code or a tiny C-facing wrapper writes `hello from c\n` through fake `write`.
- `nested_calls.elf`: demonstrates nested C calls and returns `10`.
- `stack_locals.elf`: demonstrates stack local variables and returns `31`.
- `byte_copy.elf`: demonstrates byte loads and stores.
- `stderr_c.elf`: writes to fake fd `2`.
- `bad_fd_c.elf`: checks fake `-EBADF`.
- `unknown_syscall_c.elf`: checks fake `-ENOSYS`.
- `invalid_write_c.elf`: intentionally triggers the fake `write` invalid-memory runtime error.
- `hosted_printf.c`: source-only unsupported hosted/libc counterexample, intentionally not built by `make examples`.

---

# Unit and Integration Test Cases

## Decode and Formatting

### TC-V09-DEC-001 — MOVK decode

Decode `movk x0, #imm, lsl #shift` and `movk w0, #imm, lsl #shift`. Verify destination, immediate, shift, and width.

### TC-V09-DEC-002 — MOVK formatting

Formatter emits stable text, for example:

```text
movk x0, #0x1234, lsl #16
```

### TC-V09-DEC-003 — ADD register decode

Decode `add xD, xN, xM` and `add wD, wN, wM`; include supported shifted-register form if implemented.

### TC-V09-DEC-004 — SUB register decode

Decode 64-bit and 32-bit forms and preserve operand width.

### TC-V09-DEC-005 — Logical register decode

Decode `AND`, `ORR`, and `EOR` register forms with correct destination/source registers and width.

### TC-V09-DEC-006 — Logical immediate policy

Logical-immediate forms are excluded from the selected v0.9 profile. Examples avoid them and tests assert clear unsupported decode.

### TC-V09-DEC-007 — Shift immediate aliases decode

Decode `LSL`, `LSR`, and `ASR` immediate aliases or their underlying bitfield encodings.

### TC-V09-DEC-008 — Shift boundary amounts decode

Validate 32-bit shift range `0..31` and 64-bit shift range `0..63`; invalid encodings fail clearly.

### TC-V09-DEC-009 — Multiply/divide decode

Decode `MUL`, `UDIV`, and `SDIV` for 32-bit and 64-bit forms.

### TC-V09-DEC-010 — ADR decode

Decode destination and signed immediate for positive and negative PC-relative offsets.

### TC-V09-DEC-011 — ADRP decode

Decode destination and signed page immediate.

### TC-V09-DEC-012 — Byte/halfword load-store decode

Decode selected `LDRB`, `STRB`, `LDRH`, and `STRH` addressing modes with access size 1 or 2.

### TC-V09-DEC-013 — Register-31 decode semantics are preserved

Decoded fields contain enough information for execution to distinguish XZR/WZR from SP where required.

### TC-V09-DEC-014 — Unsupported compiler instruction fails clearly

A real but unsupported AArch64 instruction outside v0.9 scope fails with readable unsupported-instruction text and execution context.

## Execution — Constants and ALU

### TC-V09-EXEC-001 — MOVK preserves other bits

`movz x0, #0x1111; movk x0, #0x2222, lsl #16` leaves `x0 = 0x0000000022221111`.

### TC-V09-EXEC-002 — MOVK 32-bit zero-extension

`movk w0, ...` follows `w`-register zero-extension behavior.

### TC-V09-EXEC-003 — ADD register 64-bit

`xD = xN + xM` modulo `2^64`.

### TC-V09-EXEC-004 — ADD register 32-bit zero-extension

`wD = wN + wM` modulo `2^32`, and high bits of `xD` become zero.

### TC-V09-EXEC-005 — SUB register wraparound

Subtraction wraps modulo operand width.

### TC-V09-EXEC-006 — ADD/SUB with XZR/WZR

Reads from zero register are zero; writes to zero register are ignored.

### TC-V09-EXEC-007 — Logical AND/ORR/EOR 64-bit

Bitwise results match unsigned integer behavior.

### TC-V09-EXEC-008 — Logical 32-bit zero-extension

`w` logical results zero-extend into `x` registers.

### TC-V09-EXEC-009 — Logical operations with zero register

Zero register behavior is correct for sources and destinations.

### TC-V09-EXEC-010 — Shift left boundaries

Shifting by `0`, `1`, and max valid amount gives expected results.

### TC-V09-EXEC-011 — Logical shift right boundaries

High bits fill with zero.

### TC-V09-EXEC-012 — Arithmetic shift right signed behavior

Negative values keep sign bits; positive values fill with zero.

### TC-V09-EXEC-013 — MUL normal case

Product is correct modulo operand width.

### TC-V09-EXEC-014 — MUL overflow wraps

Overflow truncates to 32 or 64 bits.

### TC-V09-EXEC-015 — UDIV normal case

Unsigned quotient is correct.

### TC-V09-EXEC-016 — SDIV positive/negative cases

Signed quotient truncates toward zero.

### TC-V09-EXEC-017 — Division by zero

Destination receives the documented deterministic result; recommended value is zero.

### TC-V09-EXEC-018 — Signed division overflow

Minimum signed value divided by `-1` follows the documented AArch64-style result.

## Execution — Address Generation

### TC-V09-ADDR-001 — ADR forward address

Destination receives `pc + imm` for positive immediate.

### TC-V09-ADDR-002 — ADR backward address

Destination receives `pc + imm` for negative immediate.

### TC-V09-ADDR-003 — ADR byte-granular address

ADR may compute byte-granular data addresses even though instruction fetch remains 4-byte aligned.

### TC-V09-ADDR-004 — ADRP current page

Destination receives `pc_page + page_imm`.

### TC-V09-ADDR-005 — ADRP next page

Page-relative address generation works across a page boundary.

### TC-V09-ADDR-006 — ADRP negative page offset

Signed page immediate is handled without unsigned sign-extension bugs.

### TC-V09-ADDR-007 — ADR/ADRP later invalid access

Address calculation may succeed; a later invalid load/store validates memory bounds and fails clearly.

## Execution — Byte/Halfword Memory

### TC-V09-MEM-001 — LDRB zero-extends

Loading byte `0xff` gives `0x000000ff`, not `-1`.

### TC-V09-MEM-002 — STRB writes one byte only

Adjacent bytes are unchanged.

### TC-V09-MEM-003 — LDRH zero-extends

Loading halfword `0xffff` gives `0x0000ffff`.

### TC-V09-MEM-004 — STRH writes two bytes only

Adjacent bytes are unchanged.

### TC-V09-MEM-005 — Byte access at final memory byte succeeds

`LDRB`/`STRB` at `EMU_MEMORY_SIZE - 1` succeeds.

### TC-V09-MEM-006 — Halfword access crossing memory end fails

No partial write; clear error context.

### TC-V09-MEM-007 — Unaligned halfword access policy

Follows the existing byte-addressed memory policy: unaligned data access is allowed unless v0.9 documents otherwise.

### TC-V09-MEM-008 — Byte/halfword access with SP base

Register field 31 as memory base means `SP`.

### TC-V09-MEM-009 — Byte/halfword WZR behavior

Loads into `wzr` are discarded; stores from `wzr` write zero.

### TC-V09-MEM-010 — Indexed byte/halfword policy

If indexed forms are supported, write-back happens only after successful memory access. If not supported, decoder rejects those forms clearly.

## C Runtime and Calling

### TC-V09-CRT-001 — `_start` calls `main`

`main` returns `42`; guest exits with status `42`.

### TC-V09-CRT-002 — `_start` preserves call/return semantics

`BL main` and `RET` work from ELF using normal `x30` link behavior.

### TC-V09-CRT-003 — Main return low 8 bits map to host exit code

`main` returns `300`; host CLI exits with status `44`.

### TC-V09-CRT-004 — C stack frame works

C function with local variables stored on stack returns the correct result and does not corrupt `sp` unexpectedly.

### TC-V09-CRT-005 — Nested C calls work

`main -> add3 -> add2` returns the expected value without link-register corruption.

### TC-V09-CRT-006 — Recursive C calls work within instruction limit

`fib(10)` exits with status `55`.

### TC-V09-CRT-007 — Recursion instruction limit failure remains clear

Too-deep recursion or missing base case hits the instruction limit with `pc`/opcode context and no host hang.

### TC-V09-CRT-008 — Global initialized data works

C reads `int values[] = {1, 2, 3}` and returns sum `6`.

### TC-V09-CRT-009 — Global zero-initialized data works

C reads a zero-initialized global; value is zero through v0.8 `.bss` zero-fill.

### TC-V09-CRT-010 — Static local storage works if emitted as data/bss

Static local state loads or zero-fills correctly within a single run.

### TC-V09-CRT-011 — Simple array traversal loop works

Sum an array with the selected v0.9 compiler profile; result is correct and trace shows loop branch behavior. The real C smoke example may use a pointer-increment loop when index-based code would require register-offset addressing outside the selected profile.

### TC-V09-CRT-012 — Simple pointer loop works

Sum an array using pointer increment; result is correct.

### TC-V09-CRT-013 — `strlen`-style loop works

Scan a NUL-terminated string with byte loads; exit status equals string length.

### TC-V09-CRT-014 — `memcpy`-style byte loop works

Copy bytes from source to destination, verify result, and dump memory after run.

### TC-V09-CRT-015 — Unsupported hosted/libc behavior is documented

A hosted or libc-dependent program is rejected by loader/runtime or documented as out of scope; it must not silently appear supported.

---

# CLI Test Cases

### TC-V09-CLI-001 — `run` executes return-42 C ELF

`./emulator run examples/v0_9/return_42.elf` exits with host status `42` and no unexpected register dump on guest syscall exit.

### TC-V09-CLI-002 — `regs` shows final C state

`./emulator regs examples/v0_9/return_42.elf` prints stable final registers including documented `x0` state.

### TC-V09-CLI-003 — `trace` includes C runtime path

Trace begins at `_start`, includes decoded new instructions, includes `bl` into `main`, and includes `svc #0` exit.

### TC-V09-CLI-004 — `dump` after C program shows data memory

C copies or writes bytes to a known buffer; `dump` shows expected bytes after execution.

### TC-V09-CLI-005 — `debug` break at `_start`

Break at entry, run, step, regs, continue; workflow completes normally.

### TC-V09-CLI-006 — `debug` break at `main`

Break at known `main` address from fixture/linker map; breakpoint hits before `main` executes.

### TC-V09-CLI-007 — `debug trace on` with C program

Debugger trace uses the same readable formatting for new instructions.

### TC-V09-CLI-008 — C program writes stdout through wrapper

Stdout exactly matches expected bytes and exit status is zero.

### TC-V09-CLI-009 — C program writes stderr through wrapper

Stderr exactly matches expected bytes; stdout remains clean.

### TC-V09-CLI-010 — Unsupported instruction in C-originated ELF reports context

CLI exits non-zero and error includes `pc`, raw opcode, and unsupported instruction wording.

---

# Build and Toolchain Tests

### TC-V09-BUILD-001 — `make examples` handles missing cross compiler gracefully

Either generated v0.9 fixtures do not need a cross compiler, or cross-compiler examples are skipped with a clear message while `make test` still passes.

### TC-V09-BUILD-002 — Optional cross-compiler smoke test

When a supported AArch64 compiler/linker is present, build `return_42.c` through the documented command and run it successfully.

### TC-V09-BUILD-003 — Documented compiler command is accurate

README/examples docs contain the exact flags required for freestanding, non-PIE, no-libc examples.

### TC-V09-BUILD-004 — Incorrect hosted C build fails clearly

A dynamically linked or libc-dependent C ELF is rejected with unsupported dynamic linking, `ET_DYN`, or `PT_INTERP` wording.

---

# Edge Cases and Error Handling

## Arithmetic Edge Cases

### TC-V09-ERR-001 — 32-bit arithmetic wrap

`0xffffffff + 1` in `w` form produces `0` and zero-extends.

### TC-V09-ERR-002 — 64-bit arithmetic wrap

`UINT64_MAX + 1` produces `0`.

### TC-V09-ERR-003 — Shift amount zero

Source value is unchanged.

### TC-V09-ERR-004 — Maximum valid shift amount

Shift by 31/63 boundary produces correct result.

### TC-V09-ERR-005 — Division by zero

Documented deterministic result, recommended zero.

### TC-V09-ERR-006 — Signed division negative rounding

Quotient truncates toward zero.

## Memory Edge Cases

### TC-V09-ERR-007 — Byte write at end of memory

One byte at final address succeeds.

### TC-V09-ERR-008 — Halfword write crossing end of memory

Fails without partial write.

### TC-V09-ERR-009 — Word/dword access still follows v0.3 behavior

Existing v0.3 load/store tests continue to pass.

### TC-V09-ERR-010 — Stack underflow through C recursion

Invalid memory access error is clear if recursion pushes below address zero.

## Addressing Edge Cases

### TC-V09-ERR-011 — ADR computes address near zero

Address arithmetic result is correct; later access determines memory validity.

### TC-V09-ERR-012 — ADRP computes page near memory end

Address calculation is correct; later access validates memory bounds.

### TC-V09-ERR-013 — ADRP signed immediate negative

No unsigned sign-extension bug.

## Runtime Edge Cases

### TC-V09-ERR-014 — `main` never returns

Infinite loop in C hits instruction-limit error; no host hang.

### TC-V09-ERR-015 — C calls fake exit directly

C wrapper invokes fake `exit(7)`; host exit status is `7`.

### TC-V09-ERR-016 — C fake write invalid pointer

Fake `write(1, bad_ptr, len)` produces emulator runtime error with invalid guest memory context.

### TC-V09-ERR-017 — C fake write bad fd

Wrapper receives `-EBADF` and can check/return it.

### TC-V09-ERR-018 — Unknown fake syscall from C wrapper

Wrapper receives `-ENOSYS` and can check/return it.

### TC-V09-ERR-019 — Guest exit status from negative C return

`main` returns `-1`; host exit code is `255`.

### TC-V09-ERR-020 — Empty C program body policy

If `int main(void) {}` is accepted by the compiler, documented return behavior is tested. Recommended examples explicitly return a value.

---

# Regression Tests

### TC-V09-REG-001 — v0.1 raw arithmetic examples still pass

### TC-V09-REG-002 — v0.2 branch/loop examples still pass

### TC-V09-REG-003 — v0.3 memory/stack examples still pass

### TC-V09-REG-004 — v0.4 function examples still pass

### TC-V09-REG-005 — v0.5 debugger scripts still pass

### TC-V09-REG-006 — v0.6 readable trace expectations still pass

### TC-V09-REG-007 — v0.7 raw syscall examples still pass

### TC-V09-REG-008 — v0.8 ELF loader examples still pass

### TC-V09-REG-009 — Raw vs ELF loader auto-detection remains unchanged

### TC-V09-REG-010 — Error message context remains stable

Unsupported instruction, memory error, and instruction-limit paths include useful `pc`/opcode context as applicable.

---

# Documentation and Lesson Tests

### TC-V09-DOC-001 — README links v0.9 test plan

README Test Plans includes `docs/test-plan-v0.9.md`.

### TC-V09-DOC-002 — README describes v0.9 support accurately

README says v0.9 supports tiny freestanding C programs, not normal Linux C programs.

### TC-V09-DOC-003 — README documents required compiler flags

Flags include no-libc/freestanding/static/non-PIE intent.

### TC-V09-DOC-004 — examples README explains C example workflow

Explains `_start`, `main`, fake exit, and optional cross compiler behavior.

### TC-V09-DOC-005 — v0.9 lesson follows lesson-chain structure

Lesson includes previous version -> new ability, mental model, tiny example, slow walkthrough, implementation notes, common mistakes, exercises, and one-sentence summary.

### TC-V09-DOC-006 — v0.9 lesson explains freestanding vs hosted C

New users understand why `printf` and libc are unavailable.

### TC-V09-DOC-007 — supported instruction list is updated

README/current status lists every v0.9 instruction that was added.

### TC-V09-DOC-008 — limitations are explicit

Docs say no dynamic linking, no libc, no argv/envp, no floating point/SIMD, and no broad compiler compatibility guarantee.

---

# Acceptance Tests

## TC-V09-ACC-001 — Return 42 from C

```c
int main(void) {
    return 42;
}
```

`./emulator run return_42.elf` exits with host status `42`.

## TC-V09-ACC-002 — Fibonacci from C

```c
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    return fib(10);
}
```

Host exit status is `55`.

## TC-V09-ACC-003 — Sum array from C

Sum `{1, 2, 3, 4, 5}`. Host exit status is `15`.

## TC-V09-ACC-004 — String length from C

Count bytes in a stable NUL-terminated string such as `"hello"`. Host exit status is `5`.

## TC-V09-ACC-005 — C hello through fake write

Tiny wrapper writes `hello from c\n` to stdout through fake syscall. Stdout exactly:

```text
hello from c
```

and host exit status is `0`.

## TC-V09-ACC-006 — Trace C program

`./emulator trace fib.elf` produces readable trace with `_start`, branch/call behavior, and new instruction mnemonics.

## TC-V09-ACC-007 — Debug C program

Break at entry, run, step, regs, continue. Debugger workflow works with C ELF exactly as with v0.8 assembly ELF.

## TC-V09-ACC-008 — Hosted/dynamic C program rejected clearly

Ordinary dynamically linked C hello, if available, is rejected with unsupported dynamic linking, `ET_DYN`, or `PT_INTERP` wording.

## TC-V09-ACC-009 — Existing v0.8 ELF examples still pass

`hello_elf`, `exit_status_elf`, and `bss_elf` behavior unchanged.

## TC-V09-ACC-010 — Existing raw examples still pass

Representative v0.1-v0.7 raw examples behavior unchanged.

---

# Suggested Implementation Checklist

1. Pick the exact compiler profile and example programs for v0.9.
2. Compile/disassemble candidate examples outside the emulator to identify the smallest needed instruction subset.
3. Add instruction enum entries and decoded fields for selected new instructions.
4. Add decode tests first for each new opcode family.
5. Implement execution one family at a time: `MOVK`, register add/sub, logical operations, shifts, multiply/divide, address generation, byte/halfword memory if needed.
6. Add disassembly formatting for every new instruction before relying on trace tests.
7. Add raw instruction fixtures for edge cases.
8. Add or generate minimal ELF fixtures that use the new instructions.
9. Add `_start` and tiny C examples.
10. Wire `Makefile` example targets with clear optional-cross-compiler behavior.
11. Add CLI tests for `run`, `regs`, `trace`, `dump`, and `debug`.
12. Update README supported-instruction list and current status.
13. Update `examples/README.md` with freestanding C workflow.
14. Add `lessons/v0.9-tiny-c-programs.md` using the existing lesson structure.
15. Run `make clean && make test`.

# Final Definition of Done

v0.9 is complete when:

- `make test` passes v0.1 through v0.9.
- Existing raw and ELF examples still behave exactly as before.
- At least one tiny freestanding C ELF can return a value through fake `exit`.
- At least one C example exercises function calls and stack frames.
- At least one C example exercises data memory or `.bss`.
- At least one C example exercises byte-oriented memory, such as a `strlen`-style loop, if byte loads/stores are in scope.
- The selected v0.9 instruction subset has decode, execute, disassembly, and edge-case tests.
- New unsupported instructions fail clearly rather than executing incorrectly.
- `run`, `trace`, `regs`, `dump`, and `debug` work with v0.9 C ELF examples.
- Docs clearly explain freestanding C, `_start`, compiler flags, fake syscalls, and limitations.
- The v0.9 lesson fits the v0.1-v0.8 learning chain and teaches tiny C programs at the same beginner-friendly level.

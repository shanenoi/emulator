# v0.6 Test Plan — Assembler-Friendly Runtime

## Version Goal

v0.6 makes the emulator easier to use as a learning and development tool.

Previous versions can already execute, trace, dump, and debug raw ARM64 binaries, but the output is still mostly machine-oriented. v0.6 should make examples easier to build, run, inspect, and explain by improving CLI ergonomics, example tooling, trace readability, and error messages.

This version is still **not** an ELF/Mach-O loader, source-level debugger, or full disassembler. It is a polish layer around the existing raw-binary workflow.

## Scope

### In Scope

- v0.1 through v0.5 regression tests must continue to pass.
- Example-building workflow must remain one-command friendly.
- Examples must stay organized by version.
- CLI help/usage must clearly document available commands.
- A final-state view command may be added, for example:

```sh
./emulator regs <raw-binary>
```

  or an equivalent documented command.

- Existing commands must continue to work:

```sh
./emulator run <raw-binary>
./emulator trace <raw-binary>
./emulator dump <raw-binary> <address> <length>
./emulator debug <raw-binary>
```

- Trace output should become more readable and useful for lessons/docs.
- Trace output should include at least:
  - instruction address,
  - raw opcode,
  - decoded instruction text when supported,
  - stable formatting suitable for tests.
- Instruction errors should include:
  - `pc`,
  - raw opcode when available,
  - clear reason.
- Optional symbol-map support may be added if kept simple and documented.
- Optional generated disassembly listing commands may be added if they only cover supported instructions.

### Out of Scope

- ELF loading.
- Mach-O loading.
- DWARF debug info.
- Source-line debugging.
- Real assembler implementation.
- Full ARM64 disassembler.
- Symbolic execution.
- Watchpoints.
- Conditional breakpoints.
- Syscalls.
- Dynamic linking.
- OS/runtime ABI support.
- New CPU instructions unless they are strictly needed for better examples and explicitly documented.

## Implementation Assumptions

Current v0.6 implementation status: **implemented and tested**.

These assumptions should become implementation decisions before tests are finalized:

1. `make examples` should continue to build all versioned examples.
2. `make test` should build examples before running CLI tests that depend on generated `.bin` files.
3. Trace/disassembly text should be stable enough for exact or substring tests.
4. Unknown instructions should still fail; v0.6 should improve the message, not silently skip them.
5. A decode-to-text helper should cover only instructions currently implemented by the emulator.
6. If a symbol map feature is implemented, missing symbol maps must not break normal `run`, `trace`, `dump`, or `debug` commands.

Recommended v0.6 decisions:

```text
- Keep raw binaries as the only executable input format.
- Add a reusable instruction-formatting helper:
  cpu_format_instruction(opcode, address, buffer, buffer_size)
- Make trace output include address, opcode, and decoded text.
- Keep debugger trace and CLI trace using the same formatting.
- Add `regs <raw-binary>` as a convenience alias for running to halt and printing final registers.
```

## Required Public/Private Interfaces

The implementation may choose different internal names, but tests should have access to equivalent behavior.

Recommended helper:

```c
bool cpu_format_instruction(uint32_t opcode, uint64_t address, char *out, size_t out_size);
```

Recommended behavior:

```text
Input opcode:  movz x0, #2
Output text:   0x0000000000001000: 0xd2800040  movz x0, #0x2
```

Acceptable alternative:

```text
0x0000000000001000  d2800040  movz x0, #2
```

The exact format must be documented and tested.

## Suggested Runtime Artifacts

Potential new or updated files:

```text
src/disasm.c              instruction-formatting helper
src/main.c                CLI command routing and usage updates
src/emulator.c            trace formatting integration
src/debugger.c            debugger trace formatting integration
include/emulator.h        helper declarations
examples/README.md        example-building/running guide
lessons/v0.6-assembler-friendly-runtime.md
```

Potential new test files:

```text
tests/v0_6/test_v0_6.c
tests/v0_6/test_cli_runtime.sh
```

## Test Categories

- `TC-V06-REGRESS` — previous-version regressions.
- `TC-V06-CLI` — command-line usage and command compatibility.
- `TC-V06-EXAMPLES` — example build/run workflow.
- `TC-V06-TRACE` — readable trace output.
- `TC-V06-FMT` — instruction formatting helper.
- `TC-V06-ERR` — improved error messages.
- `TC-V06-REGS` — final-state register view command.
- `TC-V06-DOCS` — docs and lessons.
- `TC-V06-OPTIONAL-SYM` — optional symbol-map support.
- `TC-V06-ACC` — acceptance workflows.

---

# TC-V06-REGRESS — Regression Tests

## TC-V06-REGRESS-001 — Full existing suite still passes

**Command:**

```sh
make clean && make test
```

**Expected:**

- v0.1 tests pass.
- v0.2 tests pass.
- v0.3 tests pass.
- v0.4 tests pass.
- v0.5 tests pass.
- v0.6 tests pass once implemented.

## TC-V06-REGRESS-002 — Existing CLI commands remain compatible

Run representative commands:

```sh
./emulator run examples/v0_1/add.bin
./emulator trace examples/v0_2/cbnz_countdown.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
printf 'run\nquit\n' | ./emulator debug examples/v0_4/simple_call.bin
```

**Expected:**

- Commands still work.
- Exit codes remain compatible.
- New formatting does not remove required old information such as `halted`, register dump, or memory dump headers.

## TC-V06-REGRESS-003 — `make examples` continues to build all existing examples

**Command:**

```sh
make clean && make examples
```

**Expected:**

- All v0.1 through v0.5 examples build.
- Generated `.bin` files exist for assembly examples.
- Debugger script examples remain text files and are not incorrectly assembled.

---

# TC-V06-CLI — CLI Usage and Compatibility

## TC-V06-CLI-001 — Usage includes all commands

**Command:**

```sh
./emulator
```

**Expected:**

- Non-zero exit code.
- Usage mentions:
  - `run`,
  - `trace`,
  - `dump`,
  - `debug`,
  - `regs` or the chosen final-state command.

## TC-V06-CLI-002 — Unknown command remains rejected

**Command:**

```sh
./emulator nope examples/v0_1/add.bin
```

**Expected:**

- Non-zero exit code.
- Error includes `unknown command`.
- Usage is printed.

## TC-V06-CLI-003 — Extra arguments are rejected consistently

Commands:

```sh
./emulator run examples/v0_1/add.bin extra
./emulator trace examples/v0_1/add.bin extra
./emulator regs examples/v0_1/add.bin extra
```

**Expected:**

- Non-zero exit code.
- Usage or command-specific argument error.
- No partial execution.

## TC-V06-CLI-004 — Missing raw binary path is rejected

Commands:

```sh
./emulator run
./emulator trace
./emulator regs
```

**Expected:**

- Non-zero exit code.
- Usage is printed.

## TC-V06-CLI-005 — Missing input file reports loader error

**Command:**

```sh
./emulator run tests/v0_6/tmp/missing.bin
```

**Expected:**

- Non-zero exit code.
- Error includes the missing path.
- Error does not look like an instruction decode error.

---

# TC-V06-EXAMPLES — Example Workflow Tests

## TC-V06-EXAMPLES-001 — Versioned example layout remains intact

Expected directories:

```text
examples/v0_1/
examples/v0_2/
examples/v0_3/
examples/v0_4/
examples/v0_5/
```

**Expected:**

- Directories exist.
- Assembly examples are under versioned directories.
- v0.5 scripts remain under `examples/v0_5/`.

## TC-V06-EXAMPLES-002 — Example README exists if added

If `examples/README.md` is added, verify it documents:

- how to run `make examples`,
- how to run an example,
- how to trace an example,
- how to debug an example,
- why `.bin` files are generated artifacts.

If not added, this test should be marked deferred.

## TC-V06-EXAMPLES-003 — Representative examples run after build

After:

```sh
make clean && make examples
```

Run:

```sh
./emulator run examples/v0_1/add.bin
./emulator run examples/v0_2/cbnz_countdown.bin
./emulator run examples/v0_3/stack_push_pop.bin
./emulator run examples/v0_4/simple_call.bin
```

**Expected:**

- All halt successfully.
- Final register values match previous-version expectations.

## TC-V06-EXAMPLES-004 — Debugger script examples still work

Commands:

```sh
./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt
./emulator debug examples/v0_4/simple_call.bin < examples/v0_5/debug_function_script.txt
./emulator debug examples/v0_3/memory_store_load.bin < examples/v0_5/debug_memory_script.txt
```

**Expected:**

- Zero exit code.
- Expected breakpoint/register/memory text appears.

---

# TC-V06-FMT — Instruction Formatting Helper Tests

These tests apply if v0.6 adds a helper such as `cpu_format_instruction()`.

## TC-V06-FMT-001 — Format NOP

Input:

```text
address = 0x1000
opcode = 0xd503201f
```

**Expected:**

- Output includes address.
- Output includes raw opcode.
- Output includes `nop`.

## TC-V06-FMT-002 — Format HLT

Input:

```text
opcode = 0xd4400000
```

**Expected:**

- Output includes `hlt`.
- Immediate is shown if the chosen format includes immediates.

## TC-V06-FMT-003 — Format MOVZ X and W forms

Inputs:

```text
movz x0, #2
movz w0, #1
```

**Expected:**

- X form displays `x0`.
- W form displays `w0`.
- Immediate is visible.

## TC-V06-FMT-004 — Format ADD/SUB immediate

Inputs:

```text
add x2, x0, #3
sub x0, x0, #1
```

**Expected:**

- Mnemonic appears.
- Destination/source registers appear.
- Immediate appears.

## TC-V06-FMT-005 — Format branch instructions

Inputs:

```text
b <target>
bl <target>
b.eq <target>
cbnz x0, <target>
```

**Expected:**

- Output includes mnemonic.
- Output includes absolute target address or signed offset.
- Chosen target style is documented.

## TC-V06-FMT-006 — Format CMP instructions

Inputs:

```text
cmp x0, #5
cmp x0, x1
```

**Expected:**

- Output includes `cmp`.
- Register/immediate operands are readable.

## TC-V06-FMT-007 — Format memory instructions

Inputs:

```text
str x0, [sp, #-8]!
ldr x1, [sp], #8
stp x29, x30, [sp, #-16]!
ldp x29, x30, [sp], #16
```

**Expected:**

- Output includes brackets.
- Pre-index `!` is shown for pre-index forms.
- Post-index offset is shown after bracket for post-index forms.

## TC-V06-FMT-008 — Unsupported opcode formatting

Input:

```text
opcode = 0xffffffff
```

**Expected:**

- Helper returns failure or formats as `<unsupported>`.
- No crash.
- Raw opcode remains visible to caller.

## TC-V06-FMT-009 — Small output buffer handling

Call formatting helper with a tiny output buffer.

**Expected:**

- No buffer overflow.
- Function returns false or truncates with documented behavior.
- Tests assert the chosen behavior.

## TC-V06-FMT-010 — Null argument handling

Call formatting helper with null output pointer or zero buffer size.

**Expected:**

- Safe failure.
- No crash.

---

# TC-V06-TRACE — Readable Trace Output Tests

## TC-V06-TRACE-001 — Trace includes address, opcode, and decoded text

**Command:**

```sh
./emulator trace examples/v0_1/add.bin
```

**Expected:**

- Output includes each instruction address.
- Output includes raw opcode, for example `0xd2800040`.
- Output includes decoded text, for example `movz`, `add`, `hlt`.

## TC-V06-TRACE-002 — Trace for loop shows repeated instruction addresses

**Command:**

```sh
./emulator trace examples/v0_2/cbnz_countdown.bin
```

**Expected:**

- Loop body addresses appear more than once.
- Branch instruction is visible as `cbnz`.
- Final output still includes `halted` and register dump.

## TC-V06-TRACE-003 — Trace for memory program shows memory instructions

**Command:**

```sh
./emulator trace examples/v0_3/stack_push_pop.bin
```

**Expected:**

- Output includes `str`.
- Output includes `ldr`.
- Output includes stack-style operands involving `sp`.

## TC-V06-TRACE-004 — Trace for function call shows BL and RET

**Command:**

```sh
./emulator trace examples/v0_4/simple_call.bin
```

**Expected:**

- Output includes `bl`.
- Output includes `ret`.
- Reordered `pc` flow is visible through trace addresses.

## TC-V06-TRACE-005 — Debugger trace uses same format

**Command:**

```sh
printf 'trace on\nstep\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

**Expected:**

- Debugger trace line uses the same address/opcode/decoded-text style as CLI trace, or a documented compatible variant.

## TC-V06-TRACE-006 — Trace handles runtime error cleanly

Create a program with an unsupported opcode after one valid instruction.

**Expected:**

- Trace includes the valid instruction before the error.
- Error includes unsupported opcode and `pc`.
- No misleading decoded text for the bad opcode unless marked unsupported.

---

# TC-V06-ERR — Improved Error Message Tests

## TC-V06-ERR-001 — Unsupported instruction includes pc and opcode

Program:

```text
0xffffffff
```

**Expected error includes:**

```text
unsupported instruction
pc=0x0000000000001000
opcode=0xffffffff
```

If the exact word is `instruction` rather than `opcode`, document and test that exact wording.

## TC-V06-ERR-002 — Misaligned pc error remains clear

Force a misaligned `pc` through C tests.

**Expected:**

- Error includes `misaligned pc`.
- Error includes current `pc`.
- No raw opcode is required because fetch cannot happen.

## TC-V06-ERR-003 — Branch target error includes source pc and target if possible

Create or directly test a branch target validation failure.

**Expected:**

- Error includes branch source `pc` or target address.
- Error reason is clear:
  - before memory,
  - outside memory,
  - misaligned,
  - overflow.

## TC-V06-ERR-004 — Return target error includes register and target

Program:

```asm
movz x30, #1
ret
```

**Expected:**

- Error includes return register, such as `x30`.
- Error includes target value.
- Error says misaligned or invalid return target.

## TC-V06-ERR-005 — Load/store error includes instruction address

Program with invalid memory access.

**Expected:**

- Error includes instruction `pc`.
- Error includes memory address or effective address.
- Error describes load/store failure.

## TC-V06-ERR-006 — Loader errors remain unchanged enough for previous tests

Missing/empty/too-large files should still report loader-specific errors, not instruction-formatting errors.

---

# TC-V06-REGS — Final-State Register View Tests

These tests apply if v0.6 adds `regs <raw-binary>` or equivalent.

## TC-V06-REGS-001 — `regs` command exists

**Command:**

```sh
./emulator regs examples/v0_1/add.bin
```

**Expected:**

- Zero exit code.
- Program runs until halt.
- Output includes register dump.
- Output may omit the word `halted` if documented, but final CPU state must be visible.

## TC-V06-REGS-002 — `regs` result matches `run` final state

Compare key outputs from:

```sh
./emulator run examples/v0_1/add.bin
./emulator regs examples/v0_1/add.bin
```

**Expected:**

- Same `x0`, `x1`, `x2`, `pc`, and instruction count.

## TC-V06-REGS-003 — `regs` reports runtime errors

Run `regs` on invalid program.

**Expected:**

- Non-zero exit code.
- Error includes reason.
- Partial CPU dump behavior is documented and tested.

## TC-V06-REGS-004 — `regs` rejects extra args

**Command:**

```sh
./emulator regs examples/v0_1/add.bin extra
```

**Expected:**

- Non-zero exit code.
- Usage/error message.

---

# TC-V06-OPTIONAL-SYM — Optional Symbol Map Tests

These tests apply only if symbol-map support is implemented.

## TC-V06-SYM-001 — Symbol map file can annotate trace

Given a small symbol map such as:

```text
0x1000 _start
0x1008 loop
```

Run trace with the selected symbol option.

**Expected:**

- Trace includes symbol names next to matching addresses.
- Raw addresses remain visible.

## TC-V06-SYM-002 — Missing symbol map path is rejected clearly

**Expected:**

- Non-zero exit code.
- Error mentions missing symbol map.
- Program is not partially executed unless documented.

## TC-V06-SYM-003 — Malformed symbol map is rejected clearly

Examples:

```text
not-an-address label
0x1000
```

**Expected:**

- Clear parse error.
- No crash.

## TC-V06-SYM-004 — Duplicate symbols or addresses have documented behavior

**Expected:**

- Either rejected, or last-one-wins/first-one-wins behavior is documented and tested.

## TC-V06-SYM-005 — Normal trace works without symbol map

**Expected:**

- Symbol-map feature does not become required.

---

# TC-V06-DOCS — Documentation Tests

## TC-V06-DOCS-001 — README documents v0.6 commands

README should document:

- `run`,
- `trace`,
- `dump`,
- `debug`,
- `regs` or equivalent if implemented,
- `make examples`,
- readable trace format.

## TC-V06-DOCS-002 — v0.6 lesson exists

Expected path:

```text
lessons/v0.6-assembler-friendly-runtime.md
```

The lesson should explain:

- why readable traces help,
- address/opcode/decoded-text columns,
- how to compare `run`, `trace`, `debug`, and `regs`,
- how examples are built and used.

If the lesson is deferred, this must be noted in README/test-plan status before release.

## TC-V06-DOCS-003 — Test plan status is current

After implementation, update this file to say whether v0.6 is:

```text
runtime pending
runtime implemented, tests pending
implemented and tested
```

## TC-V06-DOCS-004 — No stale `education/` references

Search docs for:

```text
education/
```

**Expected:**

- No stale references.
- Lessons should use `lessons/` paths.

---

# TC-V06-ACC — Acceptance Workflows

## TC-V06-ACC-001 — Beginner trace workflow

Commands:

```sh
make examples
./emulator trace examples/v0_1/add.bin
```

**Expected:**

- User can see each instruction address.
- User can see raw opcode.
- User can see decoded text.
- Final register values are visible.

## TC-V06-ACC-002 — Loop trace workflow

Command:

```sh
./emulator trace examples/v0_2/cbnz_countdown.bin
```

**Expected:**

- Repeated addresses make loop behavior visible.
- `cbnz` line is readable.
- Final `x0 = 0`, `x1 = 5` expectation remains visible through final dump.

## TC-V06-ACC-003 — Memory example workflow

Commands:

```sh
./emulator trace examples/v0_3/stack_push_pop.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
```

**Expected:**

- Trace makes load/store instructions recognizable.
- Dump output remains stable and readable.

## TC-V06-ACC-004 — Function example workflow

Command:

```sh
./emulator trace examples/v0_4/simple_call.bin
```

**Expected:**

- `bl` and `ret` are visible.
- `pc` movement makes call/return behavior easier to see.

## TC-V06-ACC-005 — Debugger plus readable trace workflow

Command:

```sh
printf 'trace on\nbreak 0x1008\nrun\nstep\nregs\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

**Expected:**

- Debugger stops at breakpoint.
- Trace output uses readable instruction formatting.
- `regs` shows the effect of stepping.

---

# Edge Cases Checklist

Before v0.6 is considered complete, verify these explicitly:

- [ ] Existing commands still reject wrong argument counts.
- [ ] Trace formatting handles every currently supported instruction family.
- [ ] Unsupported opcodes do not crash formatting.
- [ ] Formatting helper handles tiny buffers safely.
- [ ] Error messages include enough context without breaking previous tests unnecessarily.
- [ ] Debugger trace and CLI trace are consistent or intentionally documented as different.
- [ ] `make clean` removes v0.6 generated test artifacts.
- [ ] `.gitignore` ignores v0.6 generated test artifacts.
- [ ] README links this test plan.
- [ ] No stale `education/` references remain.
- [ ] Optional symbol-map tests are either implemented or explicitly marked deferred.

# Suggested Implementation Order

1. Add instruction-formatting helper for supported opcodes.
2. Add C tests for the formatter.
3. Integrate formatter into CLI `trace`.
4. Integrate formatter into debugger `trace on`.
5. Improve unsupported-instruction error messages if needed.
6. Add `regs` command or document an equivalent final-state view.
7. Add/update example documentation.
8. Add CLI tests for trace/readability workflows.
9. Update README and lessons.
10. Run full regression suite.

# Release Gate

v0.6 is complete only when:

```sh
make clean && make test
make examples
```

passes on macOS Clang and the project has a clean Git status after generated files are ignored.

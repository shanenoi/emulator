# v0.5 Test Plan — Debugger REPL

## Version Goal

v0.5 introduces an interactive debugger mode for the emulator.

The debugger should let a user load a raw ARM64 binary, inspect the CPU state, step through instructions, set and remove breakpoints, continue execution, inspect memory, toggle tracing, and exit without crashing the emulator process on invalid input.

The debugger is a learning tool. It is not a full symbolic debugger, disassembler, or source-level debugger.

## Scope

### In Scope

- New CLI command:

```sh
./emulator debug <raw-binary>
```

- Interactive REPL prompt.
- Basic debugger commands:

```text
help
run
step
continue
regs
mem <address> <length>
break <address>
delete <breakpoint-id-or-address>
breakpoints
trace on
trace off
quit
```

- Common aliases, if implemented:

```text
r       -> run
s       -> step
c       -> continue
q       -> quit
b       -> break
x       -> mem
```

- Breakpoint handling by instruction address.
- Stable register output using the existing `cpu_dump()` format or an explicitly documented debugger-specific format.
- Memory dump output compatible with the existing CLI `dump` style.
- Error reporting for invalid commands and invalid arguments.
- Non-interactive/scriptable debugger input through stdin, for automated tests:

```sh
printf 'break 0x1008\nrun\nregs\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

- v0.1 through v0.4 regression tests must continue to pass.

### Out of Scope

- Source-level debugging.
- Symbolic labels.
- DWARF support.
- ELF/Mach-O loading.
- Disassembly output beyond optional raw opcode/pc display.
- Watchpoints.
- Reverse execution.
- Conditional breakpoints.
- Persistent debugger history.
- Terminal cursor UI.
- Multithreaded debugging.
- Debugging multiple loaded programs in one session.

## Implementation Assumptions

Current v0.5 runtime implementation status: **implemented and tested**. Debugger script examples are present in `examples/v0_5/`, and the automated test pass is implemented in `tests/v0_5/`.

These assumptions are now implementation decisions and should be tested during the v0.5 test pass.

1. `debug` loads the program once at startup.
2. `run` starts from the initial reset state unless the program has already run.
3. If `run` is entered after execution has already started, the debugger should either:
   - reset CPU and memory to initial loaded state, or
   - return a clear error telling the user to use `continue` instead.

Recommended v0.5 decision:

```text
run resets the emulator to the initial loaded program state and starts execution.
continue resumes from the current state.
```

Current implementation: uses this recommended behavior. Breakpoints are preserved across `run` resets.

4. Breakpoints trigger **before** executing the instruction at the breakpoint address.
5. When execution stops at a breakpoint, `pc` should equal the breakpoint address.
6. `step` executes exactly one instruction from the current `pc`, even if the current `pc` has a breakpoint.
7. `continue` stops at the next breakpoint, halt, error, or instruction limit.
8. A breakpoint at the current `pc` should not cause `continue` to immediately stop forever without executing anything. The debugger needs a documented rule.

Recommended v0.5 decision:

```text
continue skips breakpoint checking for the current pc only once when execution is already stopped at that breakpoint.
```

Current implementation: uses this recommended behavior. After a breakpoint hit, `continue` executes the instruction at the current `pc` once before checking future breakpoint stops.

Alternative acceptable decision:

```text
The user must use step to move past a breakpoint at the current pc before continue.
```

If the alternative is chosen, tests must assert the exact error/help message.

## Required Public/Private Interfaces

The implementation may choose different internal names, but tests should have access to equivalent behavior.

Recommended structures:

```c
typedef struct {
    uint64_t address;
    bool enabled;
} DebugBreakpoint;

typedef struct {
    Emulator emu;
    DebugBreakpoint breakpoints[MAX_BREAKPOINTS];
    size_t breakpoint_count;
    bool loaded;
    bool running;
} Debugger;
```

Recommended core helpers:

```c
bool debugger_init(Debugger *debugger, const char *path, char *error, size_t error_size);
void debugger_free(Debugger *debugger);
bool debugger_add_breakpoint(Debugger *debugger, uint64_t address, char *error, size_t error_size);
bool debugger_delete_breakpoint(Debugger *debugger, uint64_t address_or_id, char *error, size_t error_size);
bool debugger_has_breakpoint(const Debugger *debugger, uint64_t address);
EmuStatus debugger_step(Debugger *debugger, char *error, size_t error_size);
EmuStatus debugger_continue(Debugger *debugger, char *error, size_t error_size);
```

Direct helper exposure is optional, but if helpers are not exposed, the CLI/script tests must cover equivalent behavior.

Current implementation exposes the recommended `Debugger` type and helper functions through `include/emulator.h`, and the CLI uses those helpers.

## Current Runtime Artifacts

- `src/debugger.c`
- `./emulator debug <raw-binary>`
- `debugger_init()` / `debugger_free()` / `debugger_reset()`
- `debugger_add_breakpoint()` / `debugger_delete_breakpoint()` / `debugger_has_breakpoint()`
- `debugger_step()` / `debugger_continue()` / `debugger_repl()`
- stdin-scriptable REPL commands:

```text
help
run / r
step / s
continue / c
regs
mem / x <address> <length>
break / b <address>
delete <breakpoint-id-or-address>
breakpoints
trace on
trace off
quit / q
```

Current parser behavior:

- commands reject unexpected extra arguments, for example `regs extra` and `trace on extra` return usage errors.
- input lines longer than the REPL buffer are consumed and reported as overlong instead of being split into multiple commands.

Current script examples:

```text
examples/v0_5/debug_add_script.txt
examples/v0_5/debug_function_script.txt
examples/v0_5/debug_memory_script.txt
```

## Test Categories

- `TC-V05-CLI` — debugger command-line entry and scriptability.
- `TC-V05-REPL` — command parsing and prompt behavior.
- `TC-V05-RUN` — run/reset behavior.
- `TC-V05-STEP` — single-step execution.
- `TC-V05-CONT` — continue execution behavior.
- `TC-V05-BP` — breakpoint handling.
- `TC-V05-REGS` — register inspection.
- `TC-V05-MEM` — memory inspection.
- `TC-V05-TRACE` — debugger trace toggling.
- `TC-V05-ERR` — invalid input and runtime errors.
- `TC-V05-ACC` — acceptance workflows.
- `TC-V05-REGRESS` — previous-version regressions.

---

# TC-V05-CLI — CLI Entry Tests

## TC-V05-CLI-001 — `debug` command exists

**Command:**

```sh
./emulator debug examples/v0_1/add.bin
```

**Input:**

```text
quit
```

**Expected:**

- exits with status `0`.
- prints a debugger banner or prompt.
- does not execute the program automatically before `run` or `step`, unless explicitly documented otherwise.

## TC-V05-CLI-002 — missing file fails clearly

**Command:**

```sh
./emulator debug missing.bin
```

**Expected:**

- exits non-zero.
- message contains `loader error` or `failed to open`.
- no REPL prompt is entered.

## TC-V05-CLI-003 — missing debug argument prints usage

**Command:**

```sh
./emulator debug
```

**Expected:**

- exits with usage status, recommended `2`.
- usage includes:

```text
emulator debug <raw-binary>
```

## TC-V05-CLI-004 — too many debug arguments prints usage

**Command:**

```sh
./emulator debug examples/v0_1/add.bin extra
```

**Expected:**

- exits with usage status, recommended `2`.
- usage includes the `debug` form.

## TC-V05-CLI-005 — stdin script mode works

**Command:**

```sh
printf 'run\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

**Expected:**

- exits `0`.
- output includes `halted` or equivalent debugger stop reason.
- output includes final/inspectable state if documented.

---

# TC-V05-REPL — Command Parsing Tests

## TC-V05-REPL-001 — `help` lists commands

**Input:**

```text
help
quit
```

**Expected:**

Output mentions at least:

```text
run
step
continue
regs
mem
break
delete
breakpoints
trace
quit
```

## TC-V05-REPL-002 — empty line is ignored

**Input:**

```text


quit
```

**Expected:**

- no error for empty lines.
- exits normally.

## TC-V05-REPL-003 — whitespace around commands is accepted

**Input:**

```text
   regs   
   quit   
```

**Expected:**

- `regs` executes successfully.
- exits normally.

## TC-V05-REPL-004 — unknown command is rejected without exiting

**Input:**

```text
wat
regs
quit
```

**Expected:**

- output contains `unknown command`.
- REPL continues and accepts `regs` afterward.
- exits `0` after `quit`.

## TC-V05-REPL-005 — command arguments are validated

**Inputs:**

```text
mem
break
trace maybe
delete
quit
```

**Expected:**

- each invalid command prints a clear usage/error message.
- no crash.
- debugger remains usable until `quit`.

## TC-V05-REPL-006 — aliases work if documented

If aliases are implemented, test:

```text
b 0x1008
r
c
q
```

Expected behavior should match full command names.

If aliases are not implemented in v0.5, this test must be omitted and docs must not advertise aliases.

---

# TC-V05-RUN — Run and Reset Behavior

## TC-V05-RUN-001 — `run` executes until HLT without breakpoints

**Program:** `examples/v0_1/add.bin`

**Input:**

```text
run
regs
quit
```

**Expected:**

- execution stops with `halted`.
- `x0 = 2`, `x1 = 3`, `x2 = 5`.
- `pc` equals the HLT address according to existing emulator behavior.

## TC-V05-RUN-002 — `run` resets after previous mutation

**Input:**

```text
step
step
run
regs
quit
```

**Expected if recommended reset behavior is used:**

- `run` restarts from the original loaded program state.
- final state matches a clean full run.
- instruction count reflects the fresh run, not accumulated previous steps.

**Alternative accepted behavior:**

- If `run` does not reset, it must print a clear message and tests must assert that behavior.

## TC-V05-RUN-003 — repeated `run` is deterministic

**Input:**

```text
run
regs
run
regs
quit
```

**Expected:**

- both register dumps after `run` show the same state.
- no duplicated memory mutations beyond what the program naturally performs.

## TC-V05-RUN-004 — `run` handles program runtime error

**Program:** unsupported-opcode raw binary.

**Input:**

```text
run
quit
```

**Expected:**

- output reports `unsupported instruction`.
- REPL remains alive until `quit`.
- exit status remains `0` unless the design treats runtime errors as debugger-process failure. Recommended: REPL process stays successful if the user quits normally.

---

# TC-V05-STEP — Step Execution Tests

## TC-V05-STEP-001 — `step` executes exactly one instruction

**Program:** `examples/v0_1/add.bin`

**Input:**

```text
step
regs
quit
```

**Expected:**

- `x0 = 2`.
- `x1 = 0`.
- `x2 = 0`.
- `pc = 0x1004`.
- instruction count is `1`.

## TC-V05-STEP-002 — multiple steps progress one instruction each

**Input:**

```text
step
step
regs
quit
```

**Expected:**

- `x0 = 2`.
- `x1 = 3`.
- `x2 = 0`.
- `pc = 0x1008`.
- instruction count is `2`.

## TC-V05-STEP-003 — stepping HLT stops execution

**Input:**

```text
step
step
step
step
regs
quit
```

**Expected:**

- fourth step executes `HLT`.
- debugger reports `halted`.
- instruction count includes the HLT instruction.

## TC-V05-STEP-004 — step after HLT is rejected cleanly

**Input:**

```text
run
step
quit
```

**Expected:**

- after `run`, program is halted.
- `step` prints a message such as `program already halted`.
- no crash and no state mutation.

## TC-V05-STEP-005 — step ignores breakpoint at current PC

**Input:**

```text
break 0x1000
step
regs
quit
```

**Expected:**

- `step` executes instruction at `0x1000` despite breakpoint.
- `pc = 0x1004`.
- this prevents a user from getting stuck at a breakpoint.

---

# TC-V05-CONT — Continue Execution Tests

## TC-V05-CONT-001 — `continue` without breakpoint runs until halt

**Input:**

```text
continue
regs
quit
```

**Expected:**

- program halts.
- final registers match full run.

## TC-V05-CONT-002 — `continue` stops at breakpoint before executing it

**Program:** `examples/v0_1/add.bin`

**Input:**

```text
break 0x1008
continue
regs
quit
```

**Expected:**

- output reports breakpoint hit at `0x1008`.
- `x0 = 2`.
- `x1 = 3`.
- `x2 = 0` because the `ADD` at `0x1008` has not executed yet.
- `pc = 0x1008`.

## TC-V05-CONT-003 — continue after breakpoint can make progress

**Input:**

```text
break 0x1008
continue
step
continue
regs
quit
```

**Expected:**

- first continue stops at `0x1008`.
- `step` executes the `ADD`.
- second continue runs until `HLT`.
- final `x2 = 5`.

## TC-V05-CONT-004 — continue handles loops and breakpoints

**Program:** `examples/v0_2/cbnz_countdown.bin`

**Input:**

```text
break 0x1008
continue
regs
step
continue
regs
quit
```

**Expected:**

- breakpoint at loop body is hit.
- registers show loop state before executing the breakpoint instruction.
- after step/continue, execution can proceed without infinite immediate breakpoint re-hit unless loop returns naturally.

## TC-V05-CONT-005 — continue respects instruction limit

**Program:** `examples/v0_2/infinite_branch.bin`

**Input:**

```text
continue
quit
```

**Expected:**

- stops with `instruction limit reached`.
- REPL remains alive until `quit`.

---

# TC-V05-BP — Breakpoint Tests

## TC-V05-BP-001 — add valid breakpoint

**Input:**

```text
break 0x1008
breakpoints
quit
```

**Expected:**

- breakpoint is accepted.
- list includes `0x1008`.

## TC-V05-BP-002 — reject misaligned breakpoint

**Input:**

```text
break 0x1002
quit
```

**Expected:**

- error mentions alignment.
- breakpoint is not added.

## TC-V05-BP-003 — reject breakpoint outside memory

**Input:**

```text
break 0x100000
quit
```

**Expected:**

- error mentions out of bounds or outside memory.
- breakpoint is not added.

## TC-V05-BP-004 — reject breakpoint below load address if policy requires executable program region only

Recommended policy:

```text
Breakpoints must be 4-byte aligned and within memory. They do not need to be within the loaded file.
```

If this policy is used, `break 0x0` is valid as a memory address but may lead to unsupported instruction if reached.

Alternative policy:

```text
Breakpoints must be inside the loaded raw binary range.
```

The chosen policy must be documented and tested.

## TC-V05-BP-005 — duplicate breakpoint is rejected or deduplicated

**Input:**

```text
break 0x1008
break 0x1008
breakpoints
quit
```

**Expected:**

Either:

- second command reports `already exists`, and only one breakpoint is listed; or
- command succeeds but list still contains only one effective breakpoint.

## TC-V05-BP-006 — delete breakpoint by address

**Input:**

```text
break 0x1008
breakpoints
delete 0x1008
breakpoints
quit
```

**Expected:**

- first list contains `0x1008`.
- second list does not contain `0x1008`.

## TC-V05-BP-007 — delete nonexistent breakpoint is clean error

**Input:**

```text
delete 0x1008
quit
```

**Expected:**

- error says no such breakpoint.
- no crash.

## TC-V05-BP-008 — breakpoint survives after being hit

**Input:**

```text
break 0x1008
run
step
run
quit
```

**Expected if `run` resets and breakpoints persist:**

- both runs stop at `0x1008` before executing that instruction.
- breakpoint remains installed until deleted.

## TC-V05-BP-009 — breakpoint list empty message

**Input:**

```text
breakpoints
quit
```

**Expected:**

- output clearly says no breakpoints, or prints an empty list header.

## TC-V05-BP-010 — breakpoint capacity edge case

If a fixed breakpoint array is used, test adding more than capacity.

**Expected:**

- capacity overflow is rejected with a clear error.
- existing breakpoints remain intact.

---

# TC-V05-REGS — Register Inspection Tests

## TC-V05-REGS-001 — `regs` before execution shows initial state

**Input:**

```text
regs
quit
```

**Expected:**

- all `x0` through `x30` are zero.
- `pc = 0x1000`.
- `sp = 0x100000`.
- instruction count is `0`.

## TC-V05-REGS-002 — `regs` after step shows partial state

**Input:**

```text
step
regs
quit
```

**Expected:**

- state reflects one executed instruction.

## TC-V05-REGS-003 — `regs` after halt shows final state

**Input:**

```text
run
regs
quit
```

**Expected:**

- final state matches normal `run` CLI output.

## TC-V05-REGS-004 — register output is stable enough for tests

**Expected:**

Output includes stable tokens for:

```text
x0
x1
...
x30
sp
pc
nzcv
instructions
```

---

# TC-V05-MEM — Memory Inspection Tests

## TC-V05-MEM-001 — dump memory before execution

**Input:**

```text
mem 0x1000 16
quit
```

**Expected:**

- prints 16 bytes from the loaded program.
- output includes address `0x0000000000001000`.

## TC-V05-MEM-002 — dump memory after store execution

**Program:** `examples/v0_3/memory_store_load.bin`

**Input:**

```text
run
mem 0xffff8 8
quit
```

**Expected:**

- output includes little-endian representation of stored value `42`:

```text
2a 00 00 00 00 00 00 00
```

## TC-V05-MEM-003 — decimal memory arguments are accepted

**Input:**

```text
mem 4096 16
quit
```

**Expected:**

- same memory range as `mem 0x1000 16`.

## TC-V05-MEM-004 — zero-length dump behavior is documented

Choose and test one behavior:

Recommended:

```text
mem <addr> 0
```

prints a header and no data rows.

Alternative:

```text
reject zero length as invalid.
```

## TC-V05-MEM-005 — out-of-bounds memory dump fails cleanly

**Input:**

```text
mem 0xfffff 8
quit
```

**Expected:**

- error mentions out of bounds.
- REPL continues.

## TC-V05-MEM-006 — invalid address syntax fails cleanly

**Input:**

```text
mem banana 8
mem 0x1000 nope
quit
```

**Expected:**

- errors mention invalid address/length.
- no crash.

---

# TC-V05-TRACE — Debugger Trace Tests

## TC-V05-TRACE-001 — trace is off by default

**Input:**

```text
step
quit
```

**Expected:**

- no `trace pc=...` line unless debugger design always prints current pc for step.
- if step prints pc by design, docs must distinguish step status from trace mode.

## TC-V05-TRACE-002 — `trace on` enables trace during run

**Input:**

```text
trace on
run
quit
```

**Expected:**

- output includes `trace pc=0x...` lines.

## TC-V05-TRACE-003 — `trace off` disables trace

**Input:**

```text
trace on
step
trace off
step
quit
```

**Expected:**

- first step/run after trace on emits trace output.
- second step/run after trace off does not emit trace output.

## TC-V05-TRACE-004 — invalid trace argument is rejected

**Input:**

```text
trace maybe
quit
```

**Expected:**

- error mentions usage: `trace on|off`.

---

# TC-V05-ERR — Runtime and Input Error Tests

## TC-V05-ERR-001 — unsupported instruction during step

**Program:** raw binary beginning with `0xffffffff`.

**Input:**

```text
step
regs
quit
```

**Expected:**

- reports unsupported instruction.
- state remains inspectable.
- REPL continues.

## TC-V05-ERR-002 — unsupported instruction during continue

**Input:**

```text
continue
regs
quit
```

**Expected:**

- reports unsupported instruction.
- state remains inspectable.

## TC-V05-ERR-003 — invalid return target in debug mode

**Program:** `examples/v0_4/invalid_return.bin`

**Input:**

```text
continue
regs
quit
```

**Expected:**

- reports invalid return target.
- REPL continues.

## TC-V05-ERR-004 — loader error does not enter REPL

Covered by `TC-V05-CLI-002`.

## TC-V05-ERR-005 — overly long input line is handled safely

**Input:**

A line longer than the debugger command buffer.

**Expected:**

- command is rejected/truncated safely with an error.
- no buffer overflow.
- REPL remains usable.

## TC-V05-ERR-006 — EOF exits cleanly

**Command:**

```sh
./emulator debug examples/v0_1/add.bin < /dev/null
```

**Expected:**

- exits `0`.
- no infinite loop.

## TC-V05-ERR-007 — Ctrl-D / stdin EOF after partial session exits cleanly

**Input:**

```text
regs
```

followed by EOF.

**Expected:**

- exits `0`.
- no crash.

---

# TC-V05-ACC — Acceptance Workflows

## TC-V05-ACC-001 — breakpoint and inspect workflow

**Program:** `examples/v0_1/add.bin`

**Input:**

```text
break 0x1008
run
regs
step
regs
continue
regs
quit
```

**Expected:**

- stops at `0x1008` before ADD.
- before step: `x2 = 0`.
- after step: `x2 = 5`.
- after continue: halted.

## TC-V05-ACC-002 — function-call stepping workflow

**Program:** `examples/v0_4/simple_call.bin`

**Input:**

```text
step
step
regs
step
regs
continue
regs
quit
```

**Expected:**

- stepping through `BL` shows `x30` set to return address.
- `pc` enters function body.
- final `x0 = 5`.

## TC-V05-ACC-003 — memory inspection workflow

**Program:** `examples/v0_3/stack_push_pop.bin`

**Input:**

```text
step
step
mem 0xffff8 8
continue
regs
quit
```

**Expected:**

- memory after store shows pushed value.
- final registers show load/pop result.
- `sp` returns to `0x100000`.

## TC-V05-ACC-004 — trace loop workflow

**Program:** `examples/v0_2/trace_loop.bin`

**Input:**

```text
trace on
continue
regs
quit
```

**Expected:**

- trace output includes repeated loop addresses.
- program halts.
- loop result is correct.

## TC-V05-ACC-005 — error recovery workflow

**Program:** unsupported-opcode binary.

**Input:**

```text
step
regs
run
quit
```

**Expected:**

- first step reports error.
- `regs` still works.
- `run` reset behavior is applied or a documented error is printed.
- REPL exits normally with `quit`.

---

# TC-V05-REGRESS — Regression Requirements

Before v0.5 is accepted:

```sh
make clean
make test
make examples
```

must pass all existing suites:

```text
v0.1 unit/integration tests
v0.1 CLI tests
v0.2 unit/integration tests
v0.2 CLI/trace tests
v0.3 unit/integration tests
v0.3 CLI/memory tests
v0.4 unit/integration tests
v0.4 CLI/function tests
v0.5 debugger tests
```

Manual smoke tests should still pass:

```sh
./emulator run examples/v0_1/add.bin
./emulator trace examples/v0_2/trace_loop.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
./emulator run examples/v0_4/simple_call.bin
printf 'break 0x1008\nrun\nregs\nquit\n' | ./emulator debug examples/v0_1/add.bin
```

---

# Suggested Test Artifacts

Recommended files:

```text
tests/v0_5/test_v0_5.c
tests/v0_5/test_cli_debugger.sh
```

Optional helper fixtures:

```text
tests/v0_5/tmp/unsupported.bin
tests/v0_5/tmp/long-command.txt
```

Recommended examples:

```text
examples/v0_5/debug_add_script.txt
examples/v0_5/debug_function_script.txt
examples/v0_5/debug_memory_script.txt
```

These examples are now present and should be kept in sync with debugger behavior.

---

# Documentation Requirements

Before v0.5 is accepted, update:

- `README.md`
  - list `./emulator debug <raw-binary>` in current CLI commands.
  - document debugger commands.
  - document breakpoint behavior.
  - document run/reset behavior.
  - document stdin script usage.
- `education/v0.5-learning-guide.md`
  - explain why a debugger is useful for emulator learning.
  - explain `step`, `continue`, breakpoints, and register/memory inspection.
  - include at least one complete debugging session walkthrough.
- `docs/test-plan-v0.5.md`
  - update status once runtime/tests are implemented.

---

# Release Checklist

v0.5 is complete when:

- `debug` command exists and is documented.
- REPL supports required commands.
- scripted stdin usage works.
- breakpoints stop before instruction execution.
- `step` executes exactly one instruction.
- `continue` handles breakpoints, halts, runtime errors, and instruction limits.
- `regs` output is stable and includes all CPU state expected by earlier versions.
- `mem` handles valid, invalid, decimal, and hex ranges.
- `trace on/off` works in debug mode.
- invalid commands do not crash or exit unexpectedly.
- EOF exits cleanly.
- all v0.1 through v0.5 tests pass.
- README and education docs are current.

---

# Suggested Implementation Order

1. Add `debug` CLI command and a minimal REPL with `quit` and `help`.
2. Add debugger initialization/loading and stdin script support.
3. Add `regs` and `mem` commands.
4. Add `step` command.
5. Add `continue` command without breakpoints.
6. Add breakpoint storage/listing/deletion.
7. Make `continue` stop at breakpoints before executing them.
8. Add `run` reset behavior.
9. Add `trace on/off` in debug mode.
10. Add robust invalid-input handling.
11. Add automated v0.5 tests. **Done:** `tests/v0_5/test_v0_5.c` and `tests/v0_5/test_cli_debugger.sh`.
12. Update docs and education guide.

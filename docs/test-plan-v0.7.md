# v0.7 Test Plan — Toy Syscalls and Standalone Programs

## Version Goal

v0.7 lets small raw ARM64 programs interact with a tiny host-provided runtime instead of only mutating registers and memory silently.

The intended feature is **fake syscall support**: the emulator recognizes an ARM64 `SVC` instruction, reads syscall arguments from registers, performs a small documented host action, and returns a stable result to the emulated program. This is the next stepping stone toward running useful standalone examples before adding an ELF loader.

The release should make it possible to write raw assembly examples that:

- write bytes to stdout or stderr,
- exit with a chosen status code,
- report syscall errors consistently,
- continue to work under `run`, `trace`, `regs`, `dump`, and `debug`.

This version is still **not** a Linux-compatible runtime, ELF loader, dynamic linker, or operating system emulator.

## Scope

### In Scope

- v0.1 through v0.6 regression tests must continue to pass.
- Decode and execute the ARM64 `SVC #imm16` instruction.
- Add a tiny fake-syscall dispatcher.
- Add at least these syscalls:
  - `exit(status)` or equivalent process termination syscall.
  - `write(fd, buffer, length)` or equivalent byte output syscall.
- Define syscall ABI clearly. Recommended ABI:
  - syscall number in `x8`, matching common AArch64 Linux convention where practical,
  - arguments in `x0` through `x5`,
  - return value in `x0`,
  - `SVC #0` as the accepted trap encoding.
- Keep syscall behavior deterministic and testable.
- Make syscall output injectable in tests so unit tests do not need to write directly to the real terminal.
- Make CLI behavior stable for stdout, stderr, and exit status.
- Make trace and instruction formatting display `svc #imm` clearly.
- Make debugger stepping and continuing handle syscalls correctly.
- Add v0.7 examples and documentation.
- Add unit/integration/CLI tests for normal behavior, invalid inputs, edge cases, and regressions.

### Out of Scope

- ELF loading.
- Mach-O loading.
- Dynamic linking.
- Linux syscall compatibility beyond the tiny documented subset.
- Real kernel state.
- File creation, opening, reading, seeking, stat, mmap, brk, clone, signals, pipes, sockets, terminals, environment variables, argv, or filesystem emulation.
- User/kernel privilege levels.
- MMU, page tables, permissions, or virtual memory.
- Asynchronous I/O.
- Source-level debugging.
- DWARF debug information.
- Full ARM64 instruction coverage.
- Automatically translating host `errno` values unless explicitly documented.

## Implementation Assumptions

These assumptions should be finalized before implementation starts.

1. The input format remains a raw little-endian AArch64 instruction stream loaded at `EMU_LOAD_ADDRESS`.
2. Syscalls are fake emulator services, not pass-through host syscalls.
3. `SVC #0` is supported. Other immediates should either behave identically or produce a documented unsupported-trap error. The recommended decision is: only `SVC #0` is accepted in v0.7.
4. The fake syscall number is read from `x8`.
5. `write` reads:
   - `x0 = fd`,
   - `x1 = guest memory address`,
   - `x2 = byte length`.
6. `write` returns number of bytes written in `x0` on success.
7. `write` returns a documented negative error value in `x0` on recoverable syscall-level failure.
8. Invalid guest memory access during `write` is treated as an emulator runtime error, not silently truncated.
9. `exit` reads `x0 = status` and stops execution without requiring `HLT`.
10. CLI process exit code should map to the low 8 bits of the guest `exit` status, unless the team intentionally keeps CLI success/failure separate. This must be documented and tested.
11. Existing `HLT` behavior remains valid and backward compatible.
12. `make test` builds v0.7 examples before v0.7 CLI tests run.
13. The debugger must not lose syscall output when stepping or continuing.

Recommended syscall numbers:

```text
write = 64
exit  = 93
```

These match common AArch64 Linux numbers and make example programs easier to compare with real assembly tutorials, while keeping the implementation fake and intentionally tiny.

## Required Public/Private Interfaces

The implementation may use different names, but tests should be able to exercise equivalent behavior.

Recommended decode kind:

```c
EMU_INST_SVC
```

Recommended syscall status model:

```c
typedef enum {
    EMU_SYSCALL_OK = 0,
    EMU_SYSCALL_EXITED,
    EMU_SYSCALL_ERROR,
} EmuSyscallStatus;
```

Recommended emulator fields:

```c
FILE *stdout_stream;
FILE *stderr_stream;
uint8_t guest_exit_code;
bool guest_exited;
```

Recommended dispatcher shape:

```c
EmuStatus emulator_handle_syscall(Emulator *emu, char *error, size_t error_size);
```

Recommended instruction formatting behavior:

```text
0x0000000000001000: 0xd4000001  svc #0x0
```

The exact string can differ, but it must be stable and covered by tests.

## Suggested Runtime Artifacts

Potential new or updated files:

```text
src/cpu.c                    SVC decode and step dispatch handoff
src/emulator.c               fake syscall dispatcher and host stream handling
src/disasm.c                 svc formatting
src/main.c                   guest exit-code handling and usage text if needed
src/debugger.c               step/continue behavior around guest exit
include/emulator.h           new instruction/syscall declarations
examples/v0_7/hello.s        write stdout then exit 0
examples/v0_7/stderr.s       write stderr then exit 0
examples/v0_7/exit_status.s  exit with non-zero status
examples/v0_7/bad_fd.s       recoverable write error example
lessons/v0.7-toy-syscalls.md
docs/test-plan-v0.7.md
tests/v0_7/test_v0_7.c
tests/v0_7/test_cli_syscalls.sh
```

## Test Categories

- `TC-V07-REGRESS` — previous-version regressions.
- `TC-V07-DECODE` — `SVC` instruction decoding and formatting.
- `TC-V07-ABI` — syscall ABI and register behavior.
- `TC-V07-EXIT` — guest exit syscall behavior.
- `TC-V07-WRITE` — stdout/stderr byte output.
- `TC-V07-ERR` — invalid syscall and invalid memory handling.
- `TC-V07-CLI` — command-line behavior and exit codes.
- `TC-V07-TRACE` — trace output and disassembly text.
- `TC-V07-DEBUG` — debugger behavior around syscalls.
- `TC-V07-DUMP` — memory dump compatibility after syscalls.
- `TC-V07-EXAMPLES` — example build/run workflow.
- `TC-V07-DOCS` — documentation and lessons.
- `TC-V07-ACC` — acceptance workflows.

---

# TC-V07-REGRESS — Regression Tests

## TC-V07-REGRESS-001 — Full existing suite still passes

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
- v0.6 tests pass.
- v0.7 tests pass once implemented.
- Existing raw binary programs that end with `HLT` still behave exactly as before.

## TC-V07-REGRESS-002 — Existing CLI commands remain compatible

Run representative commands:

```sh
./emulator run examples/v0_1/add.bin
./emulator trace examples/v0_2/cbnz_countdown.bin
./emulator regs examples/v0_1/add.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
printf 'run\nquit\n' | ./emulator debug examples/v0_4/simple_call.bin
```

**Expected:**

- Commands still work.
- Existing output shape remains compatible with v0.6 tests.
- Syscall support does not change register, memory, branch, stack, function, trace, or debugger behavior for older examples.

## TC-V07-REGRESS-003 — Instruction limit still protects non-terminating programs

**Program:** an existing infinite-branch raw binary with no `HLT` and no `exit` syscall.

**Expected:**

- Execution stops at the configured instruction limit.
- Error message remains clear.
- No fake syscall state is accidentally set.
- CLI exits non-zero.

---

# TC-V07-DECODE — SVC Decode and Formatting

## TC-V07-DECODE-001 — Decode `SVC #0`

**Input opcode:** ARM64 encoding for `svc #0`.

**Expected:**

- `cpu_decode` succeeds.
- Instruction kind is `EMU_INST_SVC` or equivalent.
- Immediate value is `0`.
- No unrelated fields are used to compute branch or memory behavior.

## TC-V07-DECODE-002 — Decode non-zero SVC immediate

**Input opcode:** ARM64 encoding for `svc #1` and a high immediate such as `svc #0xffff`.

**Expected, if only `SVC #0` is supported:**

- Decode may succeed and execution rejects unsupported immediate, or decode may reject unsupported immediate.
- The chosen behavior is documented.
- Error includes the unsupported immediate.

**Expected, if all immediates trap identically:**

- Decode succeeds.
- Formatting includes the immediate.
- Execution dispatches to the same fake-syscall handler.

## TC-V07-DECODE-003 — Reject near-SVC invalid encodings

**Input:** opcodes that differ from `SVC` in fixed opcode bits.

**Expected:**

- Invalid opcodes are rejected.
- Error message reports unsupported or unknown instruction.
- No syscall is executed.

## TC-V07-DECODE-004 — Format `svc` in instruction text

**Call:**

```c
cpu_format_instruction(svc_opcode, 0x1000, buffer, sizeof(buffer));
```

**Expected:**

- Returns true.
- Output includes address.
- Output includes raw opcode.
- Output includes `svc`.
- Output includes the immediate in a stable format.

## TC-V07-DECODE-005 — Formatting with small output buffer

**Input:** a valid `svc` opcode and a tiny buffer.

**Expected:**

- Function does not overflow.
- Function returns documented success/failure behavior.
- Output is null-terminated when buffer size is greater than zero.

---

# TC-V07-ABI — Syscall ABI and Register Behavior

## TC-V07-ABI-001 — Syscall number read from `x8`

**Program:**

```asm
movz x8, #93
movz x0, #7
svc #0
```

**Expected:**

- Dispatcher treats the syscall as `exit`.
- Guest exits with status `7`.
- Values in unrelated registers remain unchanged unless documented.

## TC-V07-ABI-002 — Write arguments read from `x0`, `x1`, and `x2`

**Setup:** guest memory contains `ABC` at a known address.

**Program:**

```asm
movz x0, #1       // fd stdout
movz x1, #addr    // guest buffer address, using available construction pattern
movz x2, #3       // length
movz x8, #64      // write
svc #0
```

**Expected:**

- Exactly `ABC` is written to emulator stdout stream.
- `x0` becomes `3` after syscall.
- `x1`, `x2`, and `x8` remain unchanged unless explicitly documented otherwise.

## TC-V07-ABI-003 — Return value is placed in `x0`

**Program:** perform successful `write`, then copy or use `x0` in a following instruction.

**Expected:**

- Follow-up instruction observes `x0 == bytes_written`.
- Return value can participate in normal arithmetic and comparisons.

## TC-V07-ABI-004 — Zero register is not a valid syscall argument register target

**Program:** attempts to use `xzr` semantics through register 31 in setup instructions where applicable.

**Expected:**

- Existing register-31 behavior remains consistent.
- Syscall dispatcher reads actual architectural register values from `x0` to `x8`, not an accidental pseudo-register slot.

## TC-V07-ABI-005 — 32-bit writes to argument registers zero-extend

**Program:** set syscall arguments using `w0`, `w1`, `w2`, or instructions that write 32-bit register views.

**Expected:**

- Existing 32-bit write semantics zero-extend.
- Syscall sees the zero-extended 64-bit values.

---

# TC-V07-EXIT — Guest Exit Syscall

## TC-V07-EXIT-001 — Exit 0 terminates execution successfully

**Program:**

```asm
movz x0, #0
movz x8, #93
svc #0
movz x0, #99      // must not execute
hlt #0
```

**Expected:**

- Execution stops at `svc`.
- Instruction after `svc` is not executed.
- Guest exit flag is set.
- Guest exit code is `0`.
- CLI exits with status `0` if guest exit status is propagated.

## TC-V07-EXIT-002 — Non-zero exit status is preserved

**Program:** exit with `42`.

**Expected:**

- Guest exit code is `42`.
- CLI exits with status `42` if guest exit status is propagated.
- If CLI does not propagate guest status, output still reports `guest exited status=42` or equivalent and tests lock that behavior.

## TC-V07-EXIT-003 — Exit status is truncated to 8 bits for CLI

**Program:** exit with `0x1234`.

**Expected:**

- Guest status is recorded consistently.
- CLI exit code is `0x34` if low-8-bit propagation is chosen.
- Documentation explains the truncation.

## TC-V07-EXIT-004 — Exit without final HLT

**Program:** ends by syscall exit and contains no `HLT`.

**Expected:**

- Program terminates cleanly.
- No instruction-limit error occurs.
- Final register dump remains available for `run` and `regs` according to command behavior.

## TC-V07-EXIT-005 — HLT still works without syscall exit

**Program:** existing `HLT`-terminated binary.

**Expected:**

- Execution halts normally.
- No guest-exit status is required.
- CLI remains backward compatible.

## TC-V07-EXIT-006 — Exit reached after branches and calls

**Program:** branch or `BL` into a function that performs `exit`.

**Expected:**

- Execution stops immediately on exit.
- `RET` after the syscall does not run.
- Link-register and stack state do not matter after guest exit.

---

# TC-V07-WRITE — Write Syscall

## TC-V07-WRITE-001 — Write to stdout

**Program:** writes `hello\n` to fd `1`, then exits `0`.

**Expected:**

- stdout contains exactly `hello\n` plus any documented emulator register output for commands that intentionally print registers.
- stderr is empty.
- `x0` equals `6` immediately after the write.

## TC-V07-WRITE-002 — Write to stderr

**Program:** writes `error\n` to fd `2`, then exits `0`.

**Expected:**

- stderr contains exactly the guest bytes, separated from emulator diagnostic errors.
- stdout does not contain the guest stderr bytes.
- `x0` equals the number of bytes written.

## TC-V07-WRITE-003 — Write zero bytes

**Program:** fd `1`, valid buffer address, length `0`.

**Expected:**

- No bytes are written.
- `x0` returns `0`.
- Program continues normally.

## TC-V07-WRITE-004 — Write binary data including NUL bytes

**Guest memory:** bytes `41 00 42 0a`.

**Expected:**

- Exactly four bytes are written.
- Output is not treated as a C string.
- NUL byte does not truncate output.

## TC-V07-WRITE-005 — Multiple sequential writes preserve order

**Program:** writes `hello`, then ` `, then `world\n`.

**Expected:**

- Output is exactly `hello world\n`.
- Each syscall return value equals its individual byte count.
- No extra bytes are inserted between writes.

## TC-V07-WRITE-006 — Write from load address region

**Setup:** message bytes are embedded after code in the raw binary.

**Expected:**

- Program can write bytes located in loaded program memory.
- Execution does not accidentally treat data bytes as instructions when control flow exits before data.

## TC-V07-WRITE-007 — Write from stack memory

**Program:** stores bytes or words on the stack, writes them, then exits.

**Expected:**

- Syscall reads current memory contents from stack addresses.
- Stack pre-index/post-index behavior from v0.3 remains correct.

## TC-V07-WRITE-008 — Write after modifying buffer

**Program:** writes bytes into memory with `STR`/`STUR`, then calls write.

**Expected:**

- Output reflects modified memory, not original loaded bytes.

## TC-V07-WRITE-009 — Maximum valid write ending at memory boundary

**Setup:** buffer starts near the end of memory and length reaches exactly `memory.size`.

**Expected:**

- Write succeeds if the whole range is valid.
- Return value equals length.
- No out-of-bounds read occurs.

## TC-V07-WRITE-010 — Large but valid write

**Setup:** buffer and length are valid and larger than a typical line.

**Expected:**

- All bytes are written.
- No truncation occurs unless a documented test-stream limit is intentionally used.
- Return value equals length.

---

# TC-V07-ERR — Error and Edge Cases

## TC-V07-ERR-001 — Unsupported syscall number

**Program:**

```asm
movz x8, #999
svc #0
```

**Expected:**

- Behavior is documented as either recoverable syscall error or emulator runtime error.
- Recommended behavior: runtime error with message `unsupported syscall: 999`.
- CLI exits non-zero.
- Error includes `pc` and raw opcode context, matching v0.6 error-quality goals.

## TC-V07-ERR-002 — Unsupported file descriptor

**Program:** `write(fd=3, valid_buffer, len=1)`.

**Expected:**

- Recommended behavior: recoverable syscall failure with negative return value in `x0`.
- Program continues after syscall.
- No bytes are written to stdout or stderr.
- Error return value is documented and stable.

## TC-V07-ERR-003 — Negative-looking file descriptor

**Program:** `x0 = UINT64_MAX`, syscall write.

**Expected:**

- Treated as an invalid fd.
- Does not index an array or crash.
- Returns documented error or reports documented runtime error.

## TC-V07-ERR-004 — Buffer address below memory range

**Program:** write from an address that is not valid guest memory.

**Expected:**

- Runtime error.
- Error message includes invalid address, length, syscall name, `pc`, and raw opcode where possible.
- No partial host output is written.

## TC-V07-ERR-005 — Buffer range crosses memory end

**Program:** address is valid but `address + length` exceeds memory size.

**Expected:**

- Runtime error before writing bytes.
- No partial output.
- Overflow-safe range check is used.

## TC-V07-ERR-006 — Buffer range overflows uint64

**Program:** address near `UINT64_MAX`, length large enough to wrap.

**Expected:**

- Runtime error.
- No host memory access outside guest memory.
- Error message distinguishes invalid guest range.

## TC-V07-ERR-007 — Null-like address with non-zero length

**Program:** `write(1, 0, 1)`.

**Expected:**

- If address `0` is within allocated memory, behavior follows normal memory rules.
- If project treats address `0` as valid guest memory, output is byte at address `0`.
- If project reserves address `0` as invalid later, error is documented.
- Test locks the chosen behavior to prevent ambiguity.

## TC-V07-ERR-008 — Host stream write failure

**Setup:** inject a failing output stream or mock writer.

**Expected:**

- Syscall returns documented error or emulator reports runtime error.
- Emulator does not crash.
- Partial write behavior is documented.

## TC-V07-ERR-009 — SVC after guest already exited cannot execute

**Setup:** call emulator step/run again after guest exit.

**Expected:**

- Subsequent run returns halted/exited status consistently.
- It does not execute more guest instructions.
- It does not repeat syscall side effects such as duplicate output.

## TC-V07-ERR-010 — Syscall does not bypass instruction limit before execution

**Program:** loop forever, eventually never reaching syscall.

**Expected:**

- Instruction limit error still fires.
- No syscall side effects occur.

---

# TC-V07-CLI — Command-Line Behavior

## TC-V07-CLI-001 — Usage still lists all commands

**Command:**

```sh
./emulator
```

**Expected:**

- Non-zero exit code.
- Usage mentions `run`, `trace`, `regs`, `dump`, and `debug`.
- If v0.7 adds no new top-level command, usage remains simple.

## TC-V07-CLI-002 — Run hello-world syscall example

**Command:**

```sh
./emulator run examples/v0_7/hello.bin
```

**Expected:**

- Guest output appears on stdout.
- Emulator final-state output remains documented.
- Exit code is `0`.
- stderr is empty.

## TC-V07-CLI-003 — Regs command with syscall output

**Command:**

```sh
./emulator regs examples/v0_7/hello.bin
```

**Expected:**

- Guest stdout bytes are preserved.
- Final register dump is printed according to `regs` behavior.
- Output ordering is deterministic: guest writes occur at the syscall point; register dump appears after termination.

## TC-V07-CLI-004 — Trace command with syscall output

**Command:**

```sh
./emulator trace examples/v0_7/hello.bin
```

**Expected:**

- Trace lines appear before each instruction, including `svc`.
- Guest output appears at the syscall point.
- Output ordering is stable enough for substring tests.

## TC-V07-CLI-005 — Dump command after syscall writes

**Command:**

```sh
./emulator dump examples/v0_7/hello.bin <message-address> <message-length>
```

**Expected:**

- Guest output appears when syscall executes.
- Final memory dump still works.
- Dump content matches guest memory.

## TC-V07-CLI-006 — Exit-status example returns non-zero status

**Command:**

```sh
./emulator run examples/v0_7/exit_status.bin
```

**Expected:**

- CLI status matches documented guest status propagation.
- Test captures status without aborting the shell script unexpectedly.
- Any final output is stable.

## TC-V07-CLI-007 — Unsupported syscall reports clear error

**Command:** run a binary that calls unknown syscall.

**Expected:**

- CLI exits non-zero.
- stderr includes unsupported syscall number.
- stderr includes runtime context.
- stdout does not contain misleading success output.

---

# TC-V07-TRACE — Trace and Disassembly

## TC-V07-TRACE-001 — CLI trace includes `svc`

**Command:**

```sh
./emulator trace examples/v0_7/hello.bin
```

**Expected:**

- At least one trace line includes decoded `svc #0x0` or equivalent.
- Trace line includes `pc` and raw opcode.
- Format matches v0.6 readable trace style.

## TC-V07-TRACE-002 — Trace shows instruction before syscall side effect

**Program:** write one visible marker byte.

**Expected:**

- The trace line for `svc` appears before the marker byte if trace is defined as pre-execution.
- The chosen order is stable and documented.

## TC-V07-TRACE-003 — Debugger trace and CLI trace use same formatting

**Commands:**

```sh
./emulator trace examples/v0_7/hello.bin
printf 'trace on\nrun\nquit\n' | ./emulator debug examples/v0_7/hello.bin
```

**Expected:**

- `svc` instruction text appears in the same style in both modes.
- Minor debugger prompt differences are acceptable.

## TC-V07-TRACE-004 — Trace does not hide syscall errors

**Program:** invalid syscall.

**Expected:**

- Last trace line identifies the failing `svc` instruction.
- Runtime error follows clearly.
- CLI exits non-zero.

---

# TC-V07-DEBUG — Debugger Behavior

## TC-V07-DEBUG-001 — Step over write syscall

**Script:**

```text
step
step
regs
quit
```

Run against a program where the second step is `svc write`.

**Expected:**

- Guest output appears exactly once.
- `x0` contains bytes written after the step.
- Debugger remains interactive after successful write.

## TC-V07-DEBUG-002 — Continue through write then stop at breakpoint

**Script:** set breakpoint after a write syscall, continue, inspect regs.

**Expected:**

- Guest output occurs before breakpoint is reported.
- Breakpoint stops at expected address.
- Register state reflects syscall return value and any following instructions already executed according to breakpoint semantics.

## TC-V07-DEBUG-003 — Continue through exit syscall terminates debug session run

**Script:**

```text
run
regs
quit
```

**Expected:**

- Debugger reports guest exit or halted status clearly.
- Program does not continue past exit.
- `regs` remains available after termination.

## TC-V07-DEBUG-004 — Breakpoint on `svc` address

**Script:** set breakpoint exactly at the `svc` instruction, continue, then step.

**Expected:**

- Continue stops before executing `svc`.
- No syscall side effect has occurred yet.
- One step executes `svc` and performs the side effect exactly once.

## TC-V07-DEBUG-005 — Reset clears guest-exit state

**Script:** run a program to guest exit, reset/run again if debugger has a reset command or use `run` semantics that reset program.

**Expected:**

- Second run executes from the beginning.
- Guest output appears once per run, not zero times and not duplicated within a run.
- Guest-exit flag/status are reset correctly.

## TC-V07-DEBUG-006 — Debugger handles syscall runtime error

**Program:** invalid guest memory range passed to write.

**Expected:**

- Debugger reports error clearly.
- Error includes syscall name and invalid range.
- Debugger exits or remains interactive according to documented existing error behavior.

---

# TC-V07-DUMP — Memory Dump Compatibility

## TC-V07-DUMP-001 — Dump embedded message bytes

**Program:** message bytes are embedded in the binary after code.

**Command:** run `dump` over message address and length.

**Expected:**

- Dump shows the exact message bytes.
- Guest write output does not corrupt memory dump formatting.

## TC-V07-DUMP-002 — Dump stack-built message after write

**Program:** builds message on stack, writes it, then exits or halts.

**Expected:**

- Dump at stack address shows the message bytes if the stack pointer/address is known.
- Memory state after syscall is unchanged by the syscall itself.

## TC-V07-DUMP-003 — Existing dump bounds checks remain intact

**Command:** use out-of-bounds dump ranges after running a syscall program.

**Expected:**

- Same bounds-checking behavior as v0.3/v0.6.
- Syscall support does not mask dump errors.

---

# TC-V07-EXAMPLES — Example Build and Run Workflow

## TC-V07-EXAMPLES-001 — `make examples` builds v0.7 examples

**Command:**

```sh
make clean && make examples
```

**Expected:**

- Existing v0.1 through v0.4 examples still build.
- v0.7 assembly examples build into `.bin` files.
- Debugger script examples remain untouched.

## TC-V07-EXAMPLES-002 — `make test` includes v0.7 tests

**Command:**

```sh
make clean && make test
```

**Expected:**

- `tests/v0_7/test_v0_7` is built and run.
- `tests/v0_7/test_cli_syscalls.sh` is run.
- Build dependencies ensure needed examples exist before CLI tests execute.

## TC-V07-EXAMPLES-003 — Hello example is minimal and teachable

**File:** `examples/v0_7/hello.s`.

**Expected:**

- Uses only instructions supported by the emulator.
- Contains comments explaining `x0`, `x1`, `x2`, and `x8`.
- Writes a short message and exits.

## TC-V07-EXAMPLES-004 — Non-zero exit example is deterministic

**File:** `examples/v0_7/exit_status.s`.

**Expected:**

- Exits with a documented status.
- CLI test can assert status reliably.
- Does not depend on host shell-specific behavior beyond POSIX exit-code capture.

---

# TC-V07-DOCS — Documentation and Lessons

## TC-V07-DOCS-001 — README links v0.7 test plan

**Expected:**

- README test-plan list includes v0.7.
- Current implementation status remains accurate if v0.7 is not implemented yet.

## TC-V07-DOCS-002 — Lesson explains fake syscall ABI

**Expected lesson content:**

- What `SVC` means in this emulator.
- Which syscall numbers are supported.
- Which registers carry syscall number, arguments, and return value.
- Difference between fake emulator syscalls and real Linux syscalls.
- How `write` and `exit` examples work.

## TC-V07-DOCS-003 — Out-of-scope behavior is explicit

**Expected:**

- Docs clearly state that v0.7 does not load ELF files.
- Docs clearly state that unsupported syscalls are not Linux-compatible.
- Docs do not imply real OS isolation or security.

## TC-V07-DOCS-004 — Error behavior is documented

**Expected:**

- Unsupported syscall behavior documented.
- Invalid guest memory behavior documented.
- Invalid fd behavior documented.
- Guest exit status behavior documented.

---

# TC-V07-ACC — Acceptance Workflows

## TC-V07-ACC-001 — Build, run, and observe hello world

**Commands:**

```sh
make clean
make
make examples
./emulator run examples/v0_7/hello.bin
```

**Expected:**

- Build succeeds.
- Example build succeeds.
- Program writes expected hello message.
- Program exits successfully.

## TC-V07-ACC-002 — Trace a syscall program

**Command:**

```sh
./emulator trace examples/v0_7/hello.bin
```

**Expected:**

- Trace explains setup instructions.
- Trace includes decoded `svc`.
- Guest output is visible.
- Final CPU state is stable.

## TC-V07-ACC-003 — Debug a syscall program

**Command:**

```sh
printf 'break <svc-address>\nrun\nregs\nstep\nregs\nquit\n' | ./emulator debug examples/v0_7/hello.bin
```

**Expected:**

- Breakpoint stops before syscall.
- First register dump shows syscall arguments.
- Step executes syscall.
- Second register dump shows return value in `x0`.
- Guest output appears exactly once.

## TC-V07-ACC-004 — Error workflow is understandable

**Command:** run invalid syscall example.

**Expected:**

- Error message names unsupported syscall or invalid memory range.
- Error includes instruction context.
- CLI exits non-zero.
- User can identify what to fix in assembly.

## TC-V07-ACC-005 — Regression confidence workflow

**Command:**

```sh
make clean && make test
```

**Expected:**

- All tests pass from v0.1 through v0.7.
- No existing examples or lessons are broken by syscall support.

---

# Edge-Case Checklist

Use this checklist when implementing or reviewing v0.7 tests.

- [ ] `SVC #0` decode succeeds.
- [ ] Non-zero `SVC` immediate behavior is specified and tested.
- [ ] Invalid SVC-like opcode is rejected.
- [ ] `svc` formatting is stable.
- [ ] Unsupported syscall number is tested.
- [ ] `exit(0)` is tested.
- [ ] Non-zero `exit` is tested.
- [ ] Large exit status truncation or preservation is documented and tested.
- [ ] `write(1, valid, len)` is tested.
- [ ] `write(2, valid, len)` is tested.
- [ ] `write` with length `0` is tested.
- [ ] `write` with NUL bytes is tested.
- [ ] Multiple writes preserve order.
- [ ] Invalid fd is tested.
- [ ] Very large fd is tested.
- [ ] Buffer address at start of memory is tested or explicitly rejected.
- [ ] Buffer address at end of memory with zero length is tested.
- [ ] Buffer ending exactly at memory boundary is tested.
- [ ] Buffer crossing memory boundary is tested.
- [ ] Address-plus-length overflow is tested.
- [ ] No partial output occurs for invalid guest ranges.
- [ ] Syscall side effects happen exactly once per executed `SVC`.
- [ ] Breakpoint before `SVC` does not execute syscall.
- [ ] Stepping over `SVC` executes syscall once.
- [ ] Trace shows `SVC` before side effect if trace is pre-execution.
- [ ] `run`, `trace`, `regs`, `dump`, and `debug` all work with syscall programs.
- [ ] Existing `HLT`-terminated examples are unchanged.
- [ ] Instruction-limit behavior is unchanged.
- [ ] `make examples` includes v0.7 examples.
- [ ] `make test` includes v0.7 unit and CLI tests.

## Release Exit Criteria

v0.7 is ready when:

1. All v0.1 through v0.7 automated tests pass with `make clean && make test`.
2. At least one stdout write example and one exit-status example are checked in.
3. Syscall ABI is documented in README or the v0.7 lesson.
4. Unsupported syscall and invalid memory errors are covered by tests.
5. Debugger behavior around `SVC` is covered by tests.
6. Trace output for `SVC` is readable and stable.
7. The version remains honest about being a fake runtime, not a Linux emulator.
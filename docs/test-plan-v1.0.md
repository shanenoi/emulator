# v1.0 Test Plan — Stable Learning Emulator

## Version Goal

v1.0 is the first **stable learning release** of the emulator.

Before v1.0, the project grows feature-by-feature:

- v0.1 starts a tiny instruction sandbox.
- v0.2 adds branches, loops, and flags.
- v0.3 adds memory and stack operations.
- v0.4 adds function calls and returns.
- v0.5 adds the debugger REPL.
- v0.6 makes execution readable with disassembly and traces.
- v0.7 adds fake `write` and `exit` syscalls.
- v0.8 adds a small ELF64 loader.
- v0.9 runs tiny freestanding C programs.

v1.0 should not be a large new architecture milestone. It should be a **polish, stability, documentation, and release-quality milestone**. A new learner should be able to clone the repository, build the emulator, run examples, use the debugger, read the lessons, and understand the current limitations without guessing.

The v1.0 promise is:

```text
clone -> build -> run tests -> run examples -> debug examples -> understand supported behavior
```

## Scope

### In Scope

- v0.1 through v0.9 behavior must continue to pass unchanged.
- Keep the supported feature set intentionally small and explicit.
- Make the command-line interface consistent and helpful across all modes:

```sh
./emulator run <program>
./emulator trace <program>
./emulator regs <program>
./emulator dump <program> <address> <length>
./emulator debug <program>
```

- Keep raw `.bin` loading and supported ELF64 `ET_EXEC` loading working.
- Keep fake syscall behavior stable:
  - `write = 64`,
  - `exit = 93`,
  - fd `1` is stdout,
  - fd `2` is stderr,
  - bad fd returns fake `-EBADF`,
  - unknown syscall returns fake `-ENOSYS`,
  - host stream write/flush failure returns fake `-EIO`,
  - invalid guest memory is a runtime error.
- Keep freestanding C examples working when the optional toolchain is available.
- Make `make test` a reliable local release gate.
- Make `make examples` either build examples or skip optional v0.9 C examples clearly when the toolchain is unavailable.
- Ensure generated artifacts are cleaned by `make clean`.
- Improve help text, error messages, docs, and examples so they match the implemented behavior.
- Add release-level tests for documentation consistency, CLI stability, example coverage, and packaging hygiene.
- Add or update lessons/docs only when they clarify supported behavior or current limitations.

### Out of Scope

- New major CPU subsystems.
- Dynamic linking.
- PIE / `ET_DYN` support.
- Relocations.
- Normal hosted C runtime support.
- `argv`, `envp`, or auxiliary vector setup.
- `printf`, `malloc`, real file I/O, or a libc layer.
- More Linux syscalls beyond the documented fake `write` and `exit` ABI.
- MMU, page tables, memory protection enforcement, signals, threads, or interrupts.
- Floating-point/SIMD/NEON.
- Cycle accuracy or performance modeling.
- Mach-O support; that remains a future v1.1-style milestone.
- Replacing the teaching-oriented code structure with a production emulator architecture.

## Release Principles

1. v1.0 favors **clarity over feature growth**.
2. Every supported behavior should be documented or discoverable through examples.
3. Every public CLI mode should have a stable success path and a stable error path.
4. Error messages should include enough context for a learner to know what failed.
5. Optional tooling should skip clearly rather than failing mysteriously.
6. Tests should be deterministic and should not require an installed cross compiler.
7. Optional real-toolchain smoke tests may run when tools are available.
8. The README should be accurate for a first-time user.
9. Lessons should form a coherent v0.1-to-v1.0 learning ladder.
10. Known limitations should be explicit, not hidden.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.0.md
tests/v1_0/test_cli_release.sh
tests/v1_0/test_docs_release.sh
tests/v1_0/test_optional_release_examples.sh
```

Optional supporting files:

```text
lessons/v1.0-stable-learning-emulator.md
examples/v1_0/README.md
examples/v1_0/smoke_manifest.txt
scripts/release_docs_check.sh
scripts/release_hygiene_check.sh
scripts/release_archive_check.sh
```

The implementation may choose different filenames, but it should keep release tests separate from feature tests so v1.0 remains clearly a stability milestone. Before the dedicated `tests/v1_0/` release tests are added, release helper scripts may live under `scripts/` and be invoked by `make release-check`; the later v1.0 test phase should either reuse those helpers or wrap them with stricter release tests.

## Test Data Strategy

v1.0 tests should combine four kinds of evidence:

1. **Existing regression suites:** run all v0.1 through v0.9 tests unchanged.
2. **CLI release checks:** run a representative set of raw, ELF assembly, generated ELF, and optional real C examples through all public CLI modes.
3. **Documentation consistency checks:** verify README, lessons, test-plan links, and examples docs mention the current supported behavior and do not contain stale “not implemented yet” claims for implemented features.
4. **Packaging hygiene checks:** verify build/clean behavior, ignored generated files, and optional toolchain skip messages.

Tests should avoid relying on a specific host shell beyond POSIX `sh` where practical. If a test requires Python for deterministic ELF generation, it should use the same interpreter assumptions already used by v0.8/v0.9 tests.

---

# Release Acceptance Test Cases

## Build and Clean

### TC-V10-BUILD-001 — Fresh build succeeds

From a clean checkout, run:

```sh
make clean
make
```

Expected:

- build exits with status `0`,
- emulator binary exists,
- no compiler warnings are emitted under the configured warning flags.

### TC-V10-BUILD-002 — Full test suite succeeds from clean tree

Run:

```sh
make clean && make test
```

Expected:

- all v0.1 through v1.0 tests pass,
- optional real-toolchain tests either pass or skip clearly,
- command exits with status `0`.

### TC-V10-BUILD-003 — Repeated test run is idempotent

Run:

```sh
make test
make test
```

Expected:

- both runs pass,
- generated test fixtures do not corrupt future runs,
- no stale state from debugger scripts or generated ELF fixtures changes results.

### TC-V10-BUILD-004 — `make clean` removes generated artifacts

Run:

```sh
make examples
make test
make clean
```

Expected generated artifacts are removed, including at least:

- emulator binary,
- object files,
- generated raw example binaries,
- generated ELF example binaries,
- generated test fixture files,
- generated v0.9 optional C objects and ELFs.

Source files, docs, lessons, and test scripts remain.

### TC-V10-BUILD-005 — Optional v0.9 C toolchain skip is clear

In an environment without usable `clang --target=aarch64-none-elf` or `ld.lld`, run an optional v0.9 C example target.

Expected:

- command exits successfully only if the project policy is to skip optional examples,
- output clearly says the example was skipped,
- output clearly says the `.elf` was not produced,
- later docs explain this behavior.

### TC-V10-BUILD-006 — Optional v0.9 C toolchain smoke passes when available

When the supported tools are available, run the optional real C smoke test.

Expected:

- actual C examples build,
- actual C examples run,
- expected exit statuses and stdout/stderr outputs match docs,
- hosted/libc counterexample remains unsupported.

### TC-V10-BUILD-007 — Missing assembler/linker errors are understandable

If required assembly tools for non-optional examples are missing, the build should fail with a clear command/error rather than a misleading success message.

Expected:

- failure is non-zero,
- output identifies the missing tool or failed command.

## CLI Interface and Help

### TC-V10-CLI-001 — No arguments prints usage and fails

Run:

```sh
./emulator
```

Expected:

- exits non-zero,
- prints usage,
- lists public commands,
- says `<program>` may be a raw `.bin` or supported ELF64 executable.

### TC-V10-CLI-002 — Unknown command prints usage and fails

Run:

```sh
./emulator nope
```

Expected:

- exits non-zero,
- mentions unknown command or usage,
- does not crash.

### TC-V10-CLI-003 — Wrong argument count fails per command

Check each public command with too few and too many arguments where applicable:

```sh
./emulator run
./emulator trace
./emulator regs
./emulator dump
./emulator dump <program>
./emulator dump <program> 0x1000
./emulator debug
```

Expected:

- exits non-zero,
- prints useful command-specific usage,
- does not create files or run partial execution.

### TC-V10-CLI-004 — Address parsing accepts documented formats

For `dump`, verify documented address/length formats:

```sh
./emulator dump <program> 0x1000 16
./emulator dump <program> 4096 16
```

Expected:

- both succeed if documented,
- output addresses match the requested range.

If only hex is intended, decimal must fail clearly and docs must say so.

### TC-V10-CLI-005 — Invalid address parsing fails clearly

Run `dump` with invalid values:

```sh
./emulator dump <program> xyz 16
./emulator dump <program> 0x1000 nope
./emulator dump <program> -1 16
./emulator dump <program> 0x1000 -1
```

Expected:

- exits non-zero,
- no wraparound into large unsigned values,
- error message identifies invalid address or length.

### TC-V10-CLI-006 — Paths with spaces work

Copy a valid program to a path containing spaces, then run all applicable commands.

Expected:

- `run`, `trace`, `regs`, `dump`, and `debug` work when the path is passed as one shell-quoted argument,
- error messages quote or otherwise display the path understandably.

### TC-V10-CLI-007 — Missing program file fails clearly

Run:

```sh
./emulator run does-not-exist.bin
```

Expected:

- exits non-zero,
- prints loader/open error,
- does not print misleading CPU state.

### TC-V10-CLI-008 — Empty program file fails clearly

Run `run`, `trace`, `regs`, `dump`, and `debug` against an empty file.

Expected:

- raw loader behavior is documented,
- execution modes fail with invalid fetch or equivalent clear error,
- dump behavior is consistent with the loader policy.

### TC-V10-CLI-009 — Directory path fails clearly

Pass a directory instead of a program file.

Expected:

- exits non-zero,
- identifies read/open failure,
- does not crash.

### TC-V10-CLI-010 — Broken stdout does not corrupt emulator state

Where practical, run a stdout-writing program with stdout closed or piped to a command that exits early.

Expected:

- host write failure maps to fake `-EIO` if detected through v0.7 fake write path,
- emulator does not crash,
- behavior is documented as host/platform dependent if exact broken-pipe behavior varies.

## Raw Binary Regression Release Checks

### TC-V10-RAW-001 — v0.1 raw program still runs

Run a v0.1 raw example such as add/sub/nop-hlt.

Expected:

- successful halt behavior preserved,
- final register state matches earlier tests.

### TC-V10-RAW-002 — v0.2 branch/loop raw example still runs

Run a branch or countdown example.

Expected:

- loop terminates correctly,
- instruction limit is not hit unexpectedly,
- trace output shows repeated addresses when looping.

### TC-V10-RAW-003 — v0.3 memory/stack raw example still runs

Run a memory or stack example.

Expected:

- memory values round-trip correctly,
- stack pointer behavior remains unchanged.

### TC-V10-RAW-004 — v0.4 function-call raw example still runs

Run a function-call example.

Expected:

- `BL`, `RET`, and `x30` behavior remains unchanged.

### TC-V10-RAW-005 — v0.7 raw syscall example still runs

Run raw `hello`, `stderr`, `bad_fd`, and `exit_status` examples.

Expected:

- stdout/stderr separation works,
- bad fd returns fake `-EBADF`,
- exit status maps to host CLI status.

## ELF and Freestanding C Release Checks

### TC-V10-ELF-001 — v0.8 assembly ELF hello runs

Build or generate the v0.8 ELF hello example and run it.

Expected:

- prints expected message,
- starts at ELF entry point,
- exits through fake syscall or valid halt behavior.

### TC-V10-ELF-002 — v0.8 `.bss` ELF behavior remains stable

Run the v0.8 `.bss` example or generated fixture.

Expected:

- zero-filled memory is visible to the program,
- exit status/output confirms `.bss` initialization.

### TC-V10-ELF-003 — v0.9 generated C-style ELF fixtures run without toolchain

Run deterministic v0.9 ELF fixtures from tests.

Expected:

- `_start -> main -> exit` path works,
- no installed cross compiler is required.

### TC-V10-ELF-004 — Optional actual v0.9 C examples run when tools exist

When optional tools are available, build and run actual v0.9 C examples.

Expected exit statuses:

```text
return_42 = 42
fib = 55
sum_array = 15
string_len = 5
hello_c = 0
nested_calls = 10
stack_locals = 31
byte_copy = 187
static_local = documented expected value
stderr_c = 0
bad_fd_c = 0
unknown_syscall_c = 0
invalid_write_c = runtime error as expected
```

### TC-V10-ELF-005 — Hosted/libc counterexample remains unsupported

Attempt to build or run the hosted/libc counterexample only through the documented unsupported workflow.

Expected:

- it is not built by `make examples`,
- docs explain why it is unsupported,
- if a hosted dynamic ELF is supplied, v0.8/v0.9 loader rejection remains clear.

### TC-V10-ELF-006 — Malformed ELF rejection remains clear

Reuse representative malformed ELF cases from v0.8.

Expected:

- wrong magic, class, endian, machine, type, truncated headers, bad program-header ranges, `PT_INTERP`, `ET_DYN`, overlapping segments, and misaligned entry all fail clearly.

## Debugger Release Checks

### TC-V10-DBG-001 — Debugger help lists supported commands

Start debugger and enter help command if available, or invalid command if help does not exist.

Expected:

- supported commands are listed or invalid command is explained,
- docs match the actual command names.

### TC-V10-DBG-002 — Breakpoint before raw instruction works

Set a breakpoint in a raw program, continue, then inspect registers.

Expected:

- execution stops before the breakpoint instruction,
- register state reflects instructions before the breakpoint only.

### TC-V10-DBG-003 — Step executes exactly one instruction

In debug mode, step through a small program.

Expected:

- `pc` advances or changes exactly as the executed instruction dictates,
- output makes it clear what instruction was executed.

### TC-V10-DBG-004 — Continue reaches halt or guest exit

Run a program under debugger and continue to completion.

Expected:

- halting program reports halt,
- guest syscall exit reports exit status,
- host CLI behavior remains documented.

### TC-V10-DBG-005 — Breakpoint before `SVC #0` works

Set breakpoint at a syscall instruction.

Expected:

- debugger stops before syscall side effect,
- one step performs the syscall exactly once,
- stdout/stderr side effects happen at step time.

### TC-V10-DBG-006 — Debugger handles invalid memory syscall error

Run a program that triggers invalid fake `write` memory from debugger.

Expected:

- runtime error is reported clearly,
- debugger exits or stays in a documented state,
- no infinite prompt loop.

### TC-V10-DBG-007 — Debugger invalid commands do not crash

Enter unknown commands, malformed breakpoint addresses, malformed dump commands, and empty lines.

Expected:

- each is handled gracefully,
- debugger remains usable unless quit was requested.

### TC-V10-DBG-008 — Debugger EOF exits cleanly

Pipe an empty input stream into debugger.

Expected:

- debugger exits cleanly,
- no hang waiting forever.

## Trace, Regs, and Dump Stability

### TC-V10-OBS-001 — `trace` output is stable enough for lessons

Run trace on representative raw and ELF programs.

Expected:

- each line includes `pc`, raw opcode, and readable disassembly,
- trace is printed before the instruction side effect when documented,
- unsupported instruction errors include `pc` and opcode context.

### TC-V10-OBS-002 — `regs` output is stable enough for tests and lessons

Run regs on raw and ELF programs.

Expected:

- prints key registers and flags in documented format,
- includes changed registers needed by lessons,
- does not include syscall program stdout mixed into register data unexpectedly unless documented.

### TC-V10-OBS-003 — `dump` reads raw loaded memory

Dump bytes from a raw program.

Expected:

- address labels are correct,
- byte order matches memory contents,
- out-of-range dump fails clearly.

### TC-V10-OBS-004 — `dump` reads ELF segment memory

Dump bytes from loaded ELF `.text`, `.rodata`, `.data`, and `.bss` where applicable.

Expected:

- file-backed bytes are present,
- `.bss` bytes are zero,
- unmapped-but-in-memory ranges follow the documented flat-memory policy.

### TC-V10-OBS-005 — `trace` and syscall output ordering is documented

Trace a program that writes to stdout/stderr.

Expected:

- instruction trace ordering relative to syscall output is deterministic or clearly documented,
- stdout and stderr separation is preserved.

## Error Quality and Edge Cases

### TC-V10-ERR-001 — Unsupported instruction error includes context

Run a program containing an unsupported instruction.

Expected:

- exits non-zero,
- error includes `pc`, raw opcode, and unsupported/decode text.

### TC-V10-ERR-002 — Instruction limit error includes context

Run a deliberate infinite loop.

Expected:

- exits non-zero,
- message mentions instruction limit,
- message includes `pc` and current opcode context.

### TC-V10-ERR-003 — Invalid fetch error includes address

Create a program that branches outside valid memory.

Expected:

- exits non-zero,
- message identifies invalid fetch address.

### TC-V10-ERR-004 — Invalid memory load/store error includes address and width

Create programs that load/store outside guest memory.

Expected:

- exits non-zero,
- message identifies memory operation, address, and size/width if supported.

### TC-V10-ERR-005 — Raw file too large fails clearly

Attempt to load a raw file larger than available memory after the raw load address.

Expected:

- loader rejects it,
- no partial execution occurs,
- error includes size/bounds context.

### TC-V10-ERR-006 — ELF segment too large fails clearly

Attempt to load an ELF with a segment outside guest memory.

Expected:

- loader rejects it,
- error includes segment/range context.

### TC-V10-ERR-007 — Arithmetic edge behavior remains deterministic

Representative arithmetic edge cases from prior versions still behave as documented:

- unsigned wraparound,
- 32-bit write zero-extension,
- signed divide edge behavior,
- division by zero result.

### TC-V10-ERR-008 — Register 31 behavior remains documented

Representative instructions preserve the v0.9 policy:

- zero register for ordinary ALU operands/destinations,
- stack pointer only for instruction forms where the emulator intentionally supports it,
- no accidental writes to `xzr`.

### TC-V10-ERR-009 — Host stream write failure remains deterministic where testable

Use a failing output stream such as `/dev/full` where available.

Expected:

- fake `write` returns `-EIO`,
- test skips on platforms without a suitable failing stream.

### TC-V10-ERR-010 — Large output remains bounded by guest memory checks

Run fake `write` with large valid and invalid lengths.

Expected:

- valid range writes exactly requested bytes,
- invalid range is rejected before host output occurs,
- no unsigned overflow bypass.

## Documentation Consistency

### TC-V10-DOC-001 — README links all test plans and lessons

README includes links for v0.1 through v1.0 test plans and v0.1 through v0.9 lessons. If a v1.0 lesson is added, README links it too.

Expected:

- all links point to existing files,
- no broken relative paths.

### TC-V10-DOC-002 — README implementation status matches code

README accurately states the current stable feature set:

- raw loader,
- ELF64 loader,
- debugger,
- readable traces,
- fake syscalls,
- tiny freestanding C examples,
- v0.1 through v1.0 tests.

Expected:

- no stale “v0.8/v0.9 tests are missing” text,
- no stale “no ELF loader yet” text,
- no unsupported claim that normal hosted C works.

### TC-V10-DOC-003 — Known limitations are explicit

Docs clearly state that the emulator does not support:

- dynamic linking,
- PIE,
- libc startup,
- `printf`,
- `malloc`,
- real Linux syscalls beyond the fake subset,
- argv/envp/auxv,
- MMU/page permissions,
- floating point/SIMD.

### TC-V10-DOC-004 — Lessons maintain the learning chain

Lessons v0.1 through v0.9 should read as a coherent progression.

Expected:

- each lesson says what the previous version could do,
- each lesson introduces one main new ability,
- examples and commands still match actual files,
- v0.6 through v0.9 maintain the same structure/quality as v0.1 through v0.5.

### TC-V10-DOC-005 — Example README documents raw, ELF, and C example flows

`examples/README.md` explains:

- how raw assembly examples are built/run,
- how v0.8 ELF examples are built/run,
- how v0.9 optional C examples are built/run or skipped,
- what to do when optional `.elf` files are not produced.

### TC-V10-DOC-006 — Test plans describe current acceptance status

Docs/test-plan files do not need to be rewritten historically, but README or a release note should make it clear which versions are currently implemented and tested.

Expected:

- no contradiction between roadmap, current implementation status, and test suite claims.

### TC-V10-DOC-007 — Command snippets are copy-pasteable

Representative commands in README and lessons should work from the repository root.

Expected:

- paths exist,
- commands use current binary name,
- commands mention optional-tool skip behavior where relevant.

## Release Packaging and Repository Hygiene

### TC-V10-REL-001 — Source tree has no unexpected generated files after clean

Run:

```sh
make clean
git status --short
```

Expected:

- no unexpected tracked-file modifications,
- no unexpected untracked generated files except documented ignored artifacts if the test environment leaves them.

### TC-V10-REL-002 — `.gitignore` covers generated artifacts

Build examples and tests, then inspect untracked files.

Expected:

- generated `.o`, `.bin`, `.elf`, test fixtures, and emulator binary are ignored or cleaned,
- source files remain trackable.

### TC-V10-REL-003 — Release archive can be built from `HEAD`

Use the project’s packaging convention:

```sh
git archive --format=zip HEAD -o emulator_<timestamp>.zip
zip -r emulator_<timestamp>.zip .git
```

Expected:

- archive is produced,
- archive includes source files and `.git`,
- archive does not require generated binaries to be present.

### TC-V10-REL-004 — Fresh archive can build and test

Extract the release archive into a new directory and run:

```sh
make clean && make test
```

Expected:

- test suite passes,
- no hidden dependency on the original working directory.

### TC-V10-REL-005 — File permissions are suitable

Shell scripts under `tests/` and any helper scripts are executable if invoked directly by `make`, or are invoked explicitly via `sh`.

Expected:

- no permission-denied failures on a fresh clone/archive.

### TC-V10-REL-006 — No accidental large binaries are tracked

Repository should not track generated object files, generated ELF files, or generated raw binaries unless there is a deliberate fixture reason documented in the test plan.

Expected:

- `git ls-files` contains source/docs/tests, not ordinary build outputs.

## Optional Quality Gates

These tests are recommended for v1.0, but may be skipped on hosts that do not support them. Skips must be clear.

### TC-V10-OPT-001 — AddressSanitizer build

Build and run tests with AddressSanitizer if the host compiler supports it.

Expected:

- no leaks or memory errors in normal test suite,
- skips clearly if unsupported.

### TC-V10-OPT-002 — UndefinedBehaviorSanitizer build

Build and run tests with UndefinedBehaviorSanitizer if available.

Expected:

- no undefined-behavior reports in supported paths,
- intentional wraparound uses unsigned types or documented behavior.

### TC-V10-OPT-003 — Multiple host compilers

Run the test suite with available host compilers, for example `cc`, `clang`, and `gcc`.

Expected:

- all supported compiler runs pass,
- compiler-specific skips or warnings are documented.

### TC-V10-OPT-004 — Strict shell mode for test scripts

Test scripts use strict shell practices where practical:

```sh
set -eu
```

Expected:

- failures stop the script,
- temporary files are cleaned up,
- optional skips are explicit and not mistaken for passes.

---

# Definition of Done

v1.0 is complete when:

1. All v0.1 through v0.9 feature tests still pass.
2. The new v1.0 release tests pass.
3. `make clean && make test` is the documented local release gate.
4. A fresh user can follow README commands to build, run, trace, dump, and debug programs.
5. Raw, ELF assembly, and tiny freestanding C examples are documented and representative.
6. Optional v0.9 real C builds either pass or skip clearly.
7. Error messages are clear for common learner mistakes.
8. README, examples docs, lessons, and test plans no longer contain stale implementation-status claims.
9. Generated files are cleaned or ignored.
10. The release archive can be produced from `HEAD` and tested after extraction.

## Non-Goals Reminder

A passing v1.0 release does **not** mean the emulator is a complete ARM64 or Linux emulator. It means the current teaching emulator is stable, well-tested, documented, and honest about its limits.

# v1.1 Test Plan — Mach-O Loader

## Version Goal

v1.1 adds the first Apple-focused executable-format milestone: a small,
teaching-oriented **Mach-O arm64 loader and inspector**.

Before v1.1, the emulator can run raw `.bin` programs and deliberately simple
little-endian AArch64 ELF64 `ET_EXEC` programs. v1.1 should extend the same
public CLI workflow to deliberately simple Mach-O arm64 executables while making
unsupported macOS/iOS runtime features obvious to learners.

The v1.1 promise is:

```text
inspect Mach-O -> validate load commands -> map simple segments -> run a tiny arm64 Mach-O or reject it clearly
```

This version is still **not** a macOS or iOS process emulator. It should not try
to run normal dynamically linked Apple-platform applications that require
`dyld`, Objective-C runtime support, platform syscalls, code signing, entitlements,
shared cache libraries, `argv`/`envp`, thread-local storage, or real kernel
services.

## Scope

### In Scope

- v0.1 through v1.0 regression behavior must continue to pass unchanged.
- Keep raw `.bin` and ELF64 `ET_EXEC` loading behavior unchanged.
- Auto-detect supported Mach-O files by magic number.
- Parse 64-bit Mach-O headers for arm64.
- Parse enough load commands to support simple loader behavior:
  - `LC_SEGMENT_64`,
  - `LC_MAIN` when present,
  - optional symbol-table inspection if practical.
- Map supported `LC_SEGMENT_64` segments into emulator memory.
- Support at least `__TEXT` and `__DATA` segment mapping for simple examples.
- Zero-fill segment memory when `vmsize > filesize`.
- Initialize `pc` from `LC_MAIN` when present, using the selected Mach-O entry
  policy documented by the implementation.
- Reject unsupported dynamic-linking/runtime features with clear errors, including
  at least:
  - `LC_LOAD_DYLINKER`,
  - `LC_LOAD_DYLIB`,
  - `LC_DYLD_INFO` / `LC_DYLD_INFO_ONLY`,
  - unsupported relocations or rebasing/binding metadata if encountered.
- Add unit, integration, CLI, documentation, and malformed-file tests.
- Keep `run`, `trace`, `regs`, `dump`, and `debug` behavior consistent across raw,
  ELF, and supported Mach-O files.
- Document the supported Mach-O profile and known limitations.

### Out of Scope

- Running normal dynamically linked macOS or iOS applications.
- Implementing `dyld`.
- Shared libraries, framework loading, or Apple shared-cache behavior.
- Code signing, entitlements, notarization, sandboxing, or platform policy checks.
- Objective-C, Swift, C++ runtime startup, exceptions, or unwinding.
- Real Darwin syscall compatibility.
- `argc`, `argv`, `envp`, or Apple process stack setup.
- Fat/universal binary execution unless explicitly implemented as inspection-only.
- Relocation processing beyond a tiny, documented static/freestanding profile.
- PIE/ASLR unless a fixed, documented load-bias policy is added.
- MMU/page permission enforcement; segment protections may be recorded for future
  versions but do not need to be enforced in v1.1.
- New CPU instructions unless strictly required by the selected tiny Mach-O examples.

## Implementation Assumptions

These assumptions should become explicit implementation decisions before tests are
finalized.

1. The public loader continues to select raw, ELF, or Mach-O mode automatically.
2. Non-ELF and non-Mach-O files continue through the raw-binary path.
3. Supported Mach-O input is 64-bit little-endian arm64 first.
4. Big-endian, 32-bit, and non-arm64 Mach-O files are rejected clearly.
5. Fat/universal files are either rejected clearly or treated as inspection-only
   unless an explicit architecture-slice selection policy is implemented.
6. `LC_SEGMENT_64` is the source of mapped memory; section tables are not required
   for loading unless the implementation uses them for inspection.
7. Segment `vmaddr` is treated as the guest memory address in the existing flat
   memory model.
8. Segment ranges must fit inside `EMU_MEMORY_SIZE`.
9. Segment file ranges must fit inside the input file.
10. Segment arithmetic must be checked for unsigned overflow.
11. Overlapping mapped segment ranges are rejected unless a documented override
    policy exists.
12. `filesize <= vmsize` is required for mapped segments.
13. Bytes from `fileoff..fileoff+filesize` are copied to `vmaddr`.
14. Bytes from `vmaddr+filesize..vmaddr+vmsize` are zero-filled.
15. Empty zero-sized segments are ignored or recorded according to a documented
    policy, but they must not create invalid memory writes.
16. Segment permissions are recorded if the program representation already stores
    permissions; v1.1 does not have to enforce them.
17. `LC_MAIN` is the preferred entry source when present.
18. The `LC_MAIN` `entryoff` must resolve to a mapped, 4-byte-aligned instruction
    address.
19. If `LC_MAIN` is absent, the implementation either rejects the file clearly or
    documents a fallback entry policy.
20. Tests should not require Xcode, `clang`, or Apple linker tools to be installed.
    Deterministic Mach-O fixtures should be generated from byte arrays or small
    fixture writers.
21. Optional real-toolchain smoke tests may run on systems that can produce arm64
    Mach-O files and should skip clearly elsewhere.
22. Existing fake `SVC #0` syscall behavior remains the only supported program exit
    and output path for executable Mach-O examples unless v1.1 documents otherwise.
23. Unsupported load commands that are harmless metadata may be ignored with traceable
    behavior; unsupported load commands that imply runtime work must be rejected.
24. All runtime errors must include enough context for learners to distinguish loader
    validation failures from CPU execution failures.

## Recommended Constants

```c
#define EMU_MACHO_MAGIC_64        0xfeedfacf
#define EMU_MACHO_CIGAM_64        0xcffaedfe
#define EMU_MACHO_CPU_TYPE_ARM64  0x0100000c
#define EMU_MACHO_LC_SEGMENT_64   0x19
#define EMU_MACHO_LC_MAIN         0x80000028
#define EMU_MACHO_LC_LOAD_DYLINKER 0x0e
#define EMU_MACHO_LC_LOAD_DYLIB   0x0c
#define EMU_MACHO_LC_DYLD_INFO    0x22
#define EMU_MACHO_LC_DYLD_INFO_ONLY 0x80000022
```

The exact names may differ, but tests should pin the same binary-format values so
fixtures are deterministic.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.1.md
lessons/v1.1-mach-o-loader.md
examples/v1_1/README.md
tests/v1_1/test_v1_1.c
tests/v1_1/test_cli_macho.sh
tests/v1_1/test_optional_macho_examples.sh
```

Optional supporting files:

```text
tests/fixtures/macho_fixture_writer.py
examples/v1_1/minimal_exit_macho_fixture.py
examples/v1_1/hello_macho_fixture.py
scripts/optional_macho_toolchain_check.sh
```

The implementation may choose different filenames, but v1.1 tests should remain
separate from v0.8 ELF tests so Mach-O behavior is easy to audit.

## Test Data Strategy

v1.1 tests should combine five kinds of evidence:

1. **Existing regression suites:** run all v0.1 through v1.0 tests unchanged.
2. **Deterministic byte fixtures:** generate tiny valid and invalid Mach-O files
   without requiring Apple developer tools.
3. **Loader unit tests:** validate header parsing, load-command parsing, segment
   mapping, zero-fill behavior, entry calculation, and rejection paths.
4. **CLI tests:** run supported fixtures through `run`, `trace`, `regs`, `dump`, and
   `debug`.
5. **Optional real-toolchain smoke tests:** when tools are available, build one or
   two tiny freestanding Mach-O examples and run or inspect them; otherwise skip
   with a clear message.

Generated fixtures should cover both executable examples and malformed edge cases.
Malformed files should be as small as possible so each test points to one failure
reason.

---

# Acceptance Test Cases

## Regression and Build

### TC-V11-BUILD-001 — Full existing suite still passes

Run:

```sh
make clean && make test
```

Expected:

- all v0.1 through v1.1 deterministic tests pass,
- optional toolchain tests either pass or skip clearly,
- command exits with status `0`.

### TC-V11-BUILD-002 — v1.1 tests are included in `make test`

Run the project test target and inspect the output or test manifest.

Expected:

- v1.1 unit/integration tests are executed,
- v1.1 CLI tests are executed,
- failures in any v1.1 test fail the top-level test target.

### TC-V11-BUILD-003 — `make clean` removes generated Mach-O fixtures

Run:

```sh
make examples
make test
make clean
```

Expected:

- generated Mach-O fixture files are removed,
- generated optional Mach-O objects/binaries are removed,
- source fixture writers, docs, tests, and lessons remain.

### TC-V11-BUILD-004 — Repeated fixture generation is deterministic

Generate deterministic Mach-O fixtures twice and compare bytes.

Expected:

- generated files are byte-for-byte identical,
- no timestamps, host paths, random IDs, or tool versions leak into deterministic
  fixtures used by `make test`.

## Format Detection

### TC-V11-DETECT-001 — Raw binary behavior is unchanged

Use an existing raw `.bin` example whose first four bytes are not Mach-O or ELF
magic.

Expected:

- loader selects raw mode,
- load address remains `0x1000`,
- initial `pc` remains `0x1000`,
- program output and final registers match v1.0 behavior.

### TC-V11-DETECT-002 — ELF behavior is unchanged

Run an existing supported ELF64 `ET_EXEC` fixture.

Expected:

- loader selects ELF mode,
- ELF entry and segment behavior match v1.0,
- no Mach-O parser attempts to parse the file.

### TC-V11-DETECT-003 — Mach-O magic selects Mach-O path

Create a minimal file starting with `0xfeedfacf` in little-endian byte order.

Expected:

- loader selects Mach-O mode,
- malformed Mach-O errors come from Mach-O validation rather than raw decode.

### TC-V11-DETECT-004 — Big-endian Mach-O magic is rejected clearly

Create a file with `0xcffaedfe` magic.

Expected:

- loader recognizes it as an unsupported Mach-O variant,
- error mentions unsupported endian or unsupported byte order,
- command exits non-zero.

### TC-V11-DETECT-005 — Truncated magic remains raw or gives documented error

Create files with 0, 1, 2, and 3 bytes.

Expected:

- behavior matches the documented loader policy,
- no out-of-bounds read occurs,
- error text is understandable.

## Mach-O Header Validation

### TC-V11-HDR-001 — Valid arm64 64-bit header is accepted

Use a deterministic fixture with a complete `mach_header_64` and valid load command
table.

Expected:

- parser accepts the header,
- CPU type is arm64,
- load-command count and size are stored correctly.

### TC-V11-HDR-002 — Wrong magic is not treated as Mach-O

Create a file with non-Mach-O and non-ELF magic.

Expected:

- loader uses raw path,
- raw path behavior matches v1.0.

### TC-V11-HDR-003 — 32-bit Mach-O is rejected

Create a file with 32-bit Mach-O magic.

Expected:

- command exits non-zero,
- error mentions unsupported 32-bit Mach-O.

### TC-V11-HDR-004 — Non-arm64 CPU type is rejected

Create valid-size headers with x86_64, armv7, and zero CPU types.

Expected:

- each file is rejected,
- error mentions unsupported CPU type or architecture.

### TC-V11-HDR-005 — Truncated header is rejected

Create files that end at every byte offset from 1 through `sizeof(mach_header_64)-1`.

Expected:

- every file is rejected cleanly,
- no parser reads past the file buffer,
- errors identify a truncated Mach-O header.

### TC-V11-HDR-006 — Load command count/size mismatch is rejected

Set `ncmds` and `sizeofcmds` to inconsistent values.

Expected:

- parser rejects the file,
- error identifies invalid load command table metadata.

### TC-V11-HDR-007 — Load command table range overflow is rejected

Use values that make `header_size + sizeofcmds` overflow an unsigned integer.

Expected:

- parser rejects before indexing the file buffer,
- error identifies invalid load command table range.

### TC-V11-HDR-008 — Load command table extends past file end

Set `sizeofcmds` larger than the remaining file length.

Expected:

- parser rejects the file,
- no partial load commands are processed as valid.

## Load Command Validation

### TC-V11-LC-001 — `LC_SEGMENT_64` command is parsed

Create a fixture with one valid `LC_SEGMENT_64` command.

Expected:

- segment name, `vmaddr`, `vmsize`, `fileoff`, `filesize`, protections, and section
  count are parsed correctly,
- command size is validated.

### TC-V11-LC-002 — `LC_MAIN` command is parsed

Create a fixture with `LC_MAIN` after a valid `__TEXT` segment.

Expected:

- `entryoff` and stack-size field are parsed,
- entry address resolves according to documented v1.1 policy.

### TC-V11-LC-003 — Unknown harmless load command is ignored or recorded

Create a fixture with an unknown load command that has a valid command size and no
runtime implication.

Expected:

- behavior matches the documented policy,
- parser stays synchronized with later load commands.

### TC-V11-LC-004 — Load command with `cmdsize < 8` is rejected

Create a load command whose size is smaller than the command header.

Expected:

- parser rejects the file,
- no infinite loop occurs.

### TC-V11-LC-005 — Load command size not aligned is rejected or documented

Create load commands with odd or non-8-byte-aligned sizes.

Expected:

- parser follows the documented alignment policy,
- malformed command does not desynchronize parsing silently.

### TC-V11-LC-006 — Load command extends past declared table

Set a command size that crosses the `sizeofcmds` boundary.

Expected:

- parser rejects the file,
- error identifies invalid command range.

### TC-V11-LC-007 — Load command extends past file end

Set `sizeofcmds` to include a command whose bytes are missing from the file.

Expected:

- parser rejects the file,
- error identifies a truncated load command.

### TC-V11-LC-008 — Duplicate `LC_MAIN` is rejected or deterministic

Create a fixture with two `LC_MAIN` commands.

Expected:

- implementation either rejects duplicate entry commands or documents exactly which
  one wins,
- tests pin that behavior.

## Segment Mapping

### TC-V11-SEG-001 — Single `__TEXT` segment maps executable bytes

Create a minimal Mach-O fixture whose `__TEXT` segment contains `MOVZ`, `SVC`, or
`HLT` instructions.

Expected:

- bytes are copied to guest memory at `vmaddr`,
- `pc` points to the entry instruction,
- program executes expected instructions.

### TC-V11-SEG-002 — `__DATA` segment maps initialized data

Create a fixture with code reading bytes from a `__DATA` segment.

Expected:

- data bytes appear at the expected guest address,
- program output or final registers prove the read succeeded.

### TC-V11-SEG-003 — Segment zero-fill supports BSS-like storage

Create a segment where `vmsize > filesize` and code reads the zero-filled range.

Expected:

- copied bytes retain file contents,
- trailing bytes are zero,
- code can read zero-filled storage.

### TC-V11-SEG-004 — `filesize > vmsize` is rejected

Create a segment with more file bytes than virtual memory size.

Expected:

- loader rejects before copying,
- error mentions invalid segment size.

### TC-V11-SEG-005 — Segment file range past EOF is rejected

Set `fileoff + filesize` beyond the file length.

Expected:

- loader rejects,
- no partial copy occurs.

### TC-V11-SEG-006 — Segment file range arithmetic overflow is rejected

Use `fileoff` and `filesize` values that overflow when added.

Expected:

- loader rejects,
- no wraparound into the beginning of the file occurs.

### TC-V11-SEG-007 — Segment memory range past emulator memory is rejected

Set `vmaddr + vmsize` beyond `EMU_MEMORY_SIZE`.

Expected:

- loader rejects,
- no memory write occurs outside the emulator buffer.

### TC-V11-SEG-008 — Segment memory range arithmetic overflow is rejected

Use `vmaddr` and `vmsize` values that wrap around unsigned arithmetic.

Expected:

- loader rejects,
- error mentions invalid segment memory range.

### TC-V11-SEG-009 — Overlapping mapped segments are rejected

Create two `LC_SEGMENT_64` commands whose guest memory ranges overlap by one byte
and by a full page.

Expected:

- loader rejects both,
- error identifies overlapping segments.

### TC-V11-SEG-010 — Adjacent segments are accepted

Create two mapped segments where the first ends exactly where the second begins.

Expected:

- loader accepts,
- both segments are mapped correctly,
- no false overlap is reported.

### TC-V11-SEG-011 — Zero-sized segment is safe

Create a segment with `vmsize = 0` and `filesize = 0`.

Expected:

- behavior follows documented policy,
- loader does not attempt an invalid copy,
- later valid segments still load correctly.

### TC-V11-SEG-012 — Section count cannot make command parsing overflow

Create an `LC_SEGMENT_64` with `nsects` too large for `cmdsize`.

Expected:

- parser rejects the segment command,
- no section parser reads beyond the command.

## Entry Point

### TC-V11-ENTRY-001 — `LC_MAIN` entry in `__TEXT` starts execution

Create a fixture with `LC_MAIN.entryoff` pointing to the first instruction in
`__TEXT`.

Expected:

- initial `pc` is the resolved entry address,
- first executed instruction is the entry instruction.

### TC-V11-ENTRY-002 — Non-zero `entryoff` starts inside `__TEXT`

Place padding before the entry instruction and set a non-zero entry offset.

Expected:

- initial `pc` skips padding,
- execution starts at the intended instruction.

### TC-V11-ENTRY-003 — Entry outside mapped segments is rejected

Set `LC_MAIN.entryoff` so the resolved address is not inside any mapped segment.

Expected:

- loader rejects,
- error mentions unmapped entry point.

### TC-V11-ENTRY-004 — Misaligned entry is rejected

Set `LC_MAIN.entryoff` to produce an address not divisible by 4.

Expected:

- loader rejects,
- error mentions instruction alignment or entry alignment.

### TC-V11-ENTRY-005 — Missing `LC_MAIN` follows documented policy

Create a valid segment-only Mach-O with no entry command.

Expected:

- loader either rejects with a clear missing-entry error or uses a documented fallback,
- test pins the chosen behavior.

## Unsupported Runtime Features

### TC-V11-UNSUP-001 — Dynamic linker command is rejected

Create a fixture with `LC_LOAD_DYLINKER`.

Expected:

- loader rejects,
- error mentions dynamic linker or unsupported `dyld` requirement.

### TC-V11-UNSUP-002 — Dynamic library command is rejected

Create a fixture with `LC_LOAD_DYLIB`.

Expected:

- loader rejects,
- error mentions dynamic libraries are unsupported.

### TC-V11-UNSUP-003 — Dyld info command is rejected when runtime work is required

Create fixtures with `LC_DYLD_INFO` and `LC_DYLD_INFO_ONLY`.

Expected:

- loader rejects if rebasing/binding would be required,
- error explains that dynamic loader metadata is unsupported.

### TC-V11-UNSUP-004 — Relocation-bearing fixture is rejected clearly

Create or simulate a Mach-O that advertises relocations not handled by v1.1.

Expected:

- loader rejects,
- error mentions unsupported relocations rather than failing during execution with
  an obscure bad address.

### TC-V11-UNSUP-005 — Fat/universal binary has deterministic behavior

Create a tiny fat-header fixture.

Expected:

- implementation either rejects fat binaries clearly or reports inspection-only
  metadata,
- it never treats the fat header as raw executable instructions by accident.

## Program Execution

### TC-V11-EXEC-001 — Minimal Mach-O exits successfully

Run a deterministic Mach-O fixture that exits through fake syscall `93` with status
`0`.

Expected:

- `./emulator run <fixture>` exits with status `0`,
- no unsupported loader errors occur.

### TC-V11-EXEC-002 — Mach-O propagates non-zero fake exit status

Run a fixture that exits with status `42`.

Expected:

- CLI process exits with status `42`,
- behavior matches ELF and raw syscall examples.

### TC-V11-EXEC-003 — Mach-O writes stdout through fake syscall

Run a fixture that calls fake `write(1, buffer, length)` with data in a mapped
Mach-O segment.

Expected:

- stdout contains exactly the expected bytes,
- final exit status is as expected.

### TC-V11-EXEC-004 — Mach-O writes stderr through fake syscall

Run a fixture that calls fake `write(2, buffer, length)`.

Expected:

- stderr contains exactly the expected bytes,
- stdout is unchanged except for documented emulator output.

### TC-V11-EXEC-005 — Mach-O can read initialized and zero-filled data

Run a fixture that reads both initialized data and BSS-like zero-filled bytes.

Expected:

- final registers or process output prove both reads are correct.

### TC-V11-EXEC-006 — Instruction-limit behavior remains stable

Run a Mach-O fixture with an intentional infinite loop.

Expected:

- emulator stops at the instruction limit,
- error includes `pc` and opcode context,
- behavior matches raw/ELF instruction-limit policy.

## CLI Modes

### TC-V11-CLI-001 — `run` accepts supported Mach-O

Run:

```sh
./emulator run examples/v1_1/minimal_exit.macho
```

Expected:

- program executes,
- exit status and output match fixture expectations.

### TC-V11-CLI-002 — `trace` uses stable decoded output for Mach-O

Run a tiny Mach-O fixture under trace mode.

Expected:

- trace lines include `pc`, raw opcode, and decoded instruction text,
- first `pc` equals the Mach-O entry point,
- mapped data bytes do not appear as executed instructions unless intentionally branched to.

### TC-V11-CLI-003 — `regs` prints final register state for Mach-O

Run a fixture that leaves known values in registers.

Expected:

- final register dump matches expected values,
- output format matches existing `regs` mode.

### TC-V11-CLI-004 — `dump` can inspect Mach-O mapped data

Run:

```sh
./emulator dump <fixture> <data-address> <length>
```

Expected:

- dump output contains initialized segment data,
- zero-filled range appears as zero bytes,
- decimal and hexadecimal address/length parsing behavior matches v1.0.

### TC-V11-CLI-005 — `debug` starts at Mach-O entry

Run the debugger with a script that prints registers, steps once, and quits.

Expected:

- initial `pc` is the Mach-O entry,
- `step` executes the entry instruction,
- debugger command parsing behavior remains unchanged.

### TC-V11-CLI-006 — Breakpoints work on Mach-O addresses

Run a debugger script that sets a breakpoint at a mapped Mach-O instruction address
and continues.

Expected:

- breakpoint is accepted,
- execution stops at the requested address,
- `continue`/breakpoint semantics match v0.5/v1.0 behavior.

### TC-V11-CLI-007 — Loader errors are consistent across modes

Run the same invalid Mach-O fixture with `run`, `trace`, `regs`, `dump`, and `debug`.

Expected:

- each command exits non-zero,
- each reports the same loader validation category,
- no mode proceeds into CPU execution after loader rejection.

## Inspection and Symbols

### TC-V11-INSP-001 — Basic Mach-O metadata is inspectable if exposed

If the implementation adds an inspect command or debug output for loader metadata,
run it on a valid fixture.

Expected:

- CPU type, number of load commands, segment names, virtual addresses, file ranges,
  and entry address are reported accurately.

### TC-V11-INSP-002 — Symbol-table inspection is correct if implemented

If v1.1 includes symbol-table inspection, create a fixture with a tiny symbol table.

Expected:

- symbol names and addresses are reported correctly,
- malformed string-table offsets are rejected,
- missing symbol tables are handled gracefully.

### TC-V11-INSP-003 — Inspection-only unsupported files do not execute accidentally

If fat/universal or complex dynamic files are inspectable but not executable, try to
run them.

Expected:

- inspection may succeed,
- execution fails with a clear unsupported-feature error,
- no raw fallback execution occurs.

## Error Message Quality

### TC-V11-ERR-001 — Header errors name the invalid field

For invalid magic, CPU type, load-command count, and load-command size fixtures,
verify the error text.

Expected:

- each error identifies the relevant field or validation category,
- messages are learner-readable.

### TC-V11-ERR-002 — Segment errors include segment identity

For invalid segment range, overlap, and file-size fixtures, verify the error text.

Expected:

- error includes segment name when available,
- error identifies memory range or file range failure.

### TC-V11-ERR-003 — Unsupported-feature errors distinguish missing emulator feature

For dynamic linker, dynamic library, relocation, and fat-binary fixtures, verify the
error text.

Expected:

- errors say the feature is unsupported by v1.1,
- errors do not imply the input file is necessarily corrupt.

### TC-V11-ERR-004 — CPU execution errors still include instruction context

Run a valid Mach-O fixture containing an unsupported instruction at the entry point.

Expected:

- loader succeeds,
- CPU execution fails,
- error includes `pc`, raw opcode, and decode/unsupported-instruction context.

## Documentation

### TC-V11-DOC-001 — README links to v1.1 test plan and lesson

Inspect README links.

Expected:

- README includes `docs/test-plan-v1.1.md`,
- README includes the v1.1 lesson if the lesson exists,
- links are not broken.

### TC-V11-DOC-002 — Supported Mach-O profile is documented

Inspect README, lesson, and example docs.

Expected:

- docs state supported magic/architecture/load-command profile,
- docs explain that normal dynamically linked macOS/iOS apps are out of scope,
- docs explain how v1.1 differs from a real Darwin process emulator.

### TC-V11-DOC-003 — CLI usage mentions Mach-O only where implemented

Inspect help output and README command examples.

Expected:

- CLI help describes supported program formats accurately,
- it does not overpromise normal Apple app compatibility.

### TC-V11-DOC-004 — Optional toolchain behavior is documented

If optional real Mach-O example builds exist, inspect docs and skip output.

Expected:

- docs identify required optional tools,
- missing tools produce clear skip messages,
- skipped examples are not later executed as if generated.

## Edge Cases and Fuzz-Like Coverage

### TC-V11-EDGE-001 — One-byte mutations around header fields are safe

Mutate one byte at a time in the Mach-O header of a valid fixture.

Expected:

- each mutated file either loads correctly for harmless mutations or rejects cleanly,
- no crash, assertion failure, memory error, or infinite loop occurs.

### TC-V11-EDGE-002 — One-byte mutations around load-command sizes are safe

Mutate bytes that affect `cmd`, `cmdsize`, `ncmds`, and `sizeofcmds`.

Expected:

- parser never desynchronizes into out-of-bounds reads,
- malformed inputs reject clearly.

### TC-V11-EDGE-003 — Boundary memory addresses are handled correctly

Test segments starting at address `0`, ending exactly at `EMU_MEMORY_SIZE`, and
crossing `EMU_MEMORY_SIZE` by one byte.

Expected:

- exact-boundary mappings follow documented policy,
- out-of-bounds-by-one mapping is rejected.

### TC-V11-EDGE-004 — Empty file and directory input are handled

Run CLI modes against an empty file and, where supported by the host shell, a
directory path.

Expected:

- errors are clear,
- no crash occurs,
- exit status is non-zero.

### TC-V11-EDGE-005 — Very large declared counts do not cause excessive allocation

Create headers with huge `ncmds`, section counts, or string-table sizes.

Expected:

- parser rejects based on file/table bounds,
- no large unbounded allocation is attempted.

### TC-V11-EDGE-006 — Malformed fixtures do not modify CPU state after failed load

Attempt to load invalid Mach-O files into an initialized emulator state.

Expected:

- failed load does not leave partially initialized `pc`, memory, or segment metadata
  that can be executed accidentally.

## Release Acceptance

### TC-V11-REL-001 — `make release-check` includes v1.1 coverage

Run:

```sh
make release-check
```

Expected:

- release gate includes v1.1 deterministic tests,
- docs and hygiene checks account for v1.1 files,
- archive validation passes from a fresh extracted archive.

### TC-V11-REL-002 — Release archive includes v1.1 docs/tests/examples

Create a release archive and inspect it.

Expected:

- v1.1 test plan is included,
- v1.1 tests and fixture writers are included,
- generated artifacts are excluded unless intentionally tracked,
- `.git` inclusion behavior matches the project release policy.

### TC-V11-REL-003 — Fresh archive can run v1.1 deterministic suite

Extract the release archive into a new directory and run the deterministic tests.

Expected:

- v1.1 fixtures can be generated from tracked sources,
- tests pass without relying on files outside the archive,
- optional real-toolchain checks skip or pass clearly.

## Definition of Done

v1.1 is complete when:

- raw and ELF behavior from v1.0 remains unchanged,
- supported Mach-O files are detected, parsed, mapped, and either run or inspected
  according to the documented v1.1 profile,
- unsupported Apple runtime features are rejected with clear learner-facing errors,
- comprehensive unit, integration, CLI, docs, edge-case, and release tests are part
  of `make test` and `make release-check`,
- deterministic tests do not require Apple developer tools,
- optional real-toolchain tests pass when available and skip clearly otherwise,
- docs explain exactly what v1.1 supports and what remains future work.
# v1.2 Test Plan — Virtual Memory and Page Permissions

## Version Goal

v1.2 teaches the next memory milestone: move from a simple flat byte array with
bounds checks to a small, page-based **virtual memory model** with mapped regions,
permissions, stack guarding, and clearer fault reporting.

Before v1.2, the emulator can run raw binaries, simple ELF64 executables, tiny
freestanding C examples, and deliberately small Mach-O arm64 fixtures. Those
loaders can record segment permissions, but the emulator does not yet enforce a
real read/write/execute policy. v1.2 should make memory behavior more explicit:
learners should be able to ask "which page is this address in?", "is this page
mapped?", and "does this access have permission?" before an instruction succeeds.

The v1.2 promise is:

```text
map pages -> enforce R/W/X permissions -> report precise memory faults -> inspect mappings in the debugger
```

v1.2 is still **not** a production MMU. It should not implement page tables,
translation registers, TLBs, ASLR, process isolation, demand paging, copy-on-write,
thread stacks, signals, or operating-system VM policy. The goal is a deterministic
teaching model that makes invalid memory behavior easy to understand and debug.

## Scope

### In Scope

- v0.1 through v1.1 regression behavior must continue to pass unless a behavior is
  intentionally tightened by documented page-permission enforcement.
- Introduce a page-sized memory model on top of the existing fixed guest address
  space.
- Track mapped and unmapped virtual address ranges.
- Track page permissions:
  - read,
  - write,
  - execute.
- Enforce permissions for:
  - instruction fetch,
  - data reads,
  - data writes,
  - memory dumps and debugger memory reads according to a documented policy.
- Represent loader-created mappings for raw `.bin`, ELF64, and Mach-O inputs.
- Preserve the existing teaching-friendly memory size unless a larger size is
  explicitly chosen and documented.
- Add a stack mapping with at least one guard page.
- Reject or fault on stack underflow into the guard page.
- Produce deterministic fault messages that distinguish:
  - unmapped access,
  - read-permission fault,
  - write-permission fault,
  - execute-permission fault,
  - invalid/overflowing address range.
- Add public inspection support:
  - CLI `info` should show mappings and permissions,
  - debugger should list memory mappings,
  - debugger should identify the mapping for a specific address.
- Use ELF and Mach-O segment permissions when creating loader mappings, while
  keeping the policy simple and documented.
- Keep deterministic generated fixtures for permission and guard-page examples.
- Add unit, integration, CLI, debugger, docs, malformed-input, and release tests.
- Document the virtual memory model in a v1.2 lesson and README updates.

### Out of Scope

- Real ARMv8 page tables or MMU registers.
- TLB modeling.
- Address-space identifiers or process isolation.
- Kernel/user privilege enforcement.
- Signals, exceptions, `SIGSEGV`, or real OS fault delivery.
- Demand paging or lazy file-backed pages.
- Copy-on-write.
- Memory-mapped files.
- Shared libraries or dynamic loader mapping policy.
- Heap allocator, `brk`, `mmap`, or more syscalls.
- Thread stacks or TLS.
- ASLR/PIE relocation.
- Fine-grained sub-page permissions.
- Cycle/performance modeling of memory access.
- Enforcing code-signing, W^X policy beyond the simple permissions documented here,
  or Apple/Linux platform security behavior.
- Rewriting the emulator into a production VM subsystem.

## Implementation Assumptions

These assumptions should become explicit implementation decisions before tests are
finalized.

1. The teaching page size is fixed and documented, preferably `4096` bytes.
2. The existing guest memory capacity remains deterministic; tests should not rely
   on host-specific allocation sizes.
3. Every byte address belongs either to a mapped page or to no mapping.
4. A mapping consists of a page-aligned start address, page-aligned length, and
   permission bits.
5. Mappings may be created by loaders and by emulator initialization for the stack.
6. Mappings may not overlap unless the implementation documents an exact replace or
   merge policy.
7. Loader segment addresses that are not page-aligned are either rounded according
   to a documented policy or rejected clearly.
8. Loader segment file contents still copy into the exact guest virtual addresses
   described by the executable format.
9. Zero-fill still works when `mem_size > file_size`, but the zero-filled bytes must
   be inside mapped writable/readable memory according to the documented segment
   policy.
10. Instruction fetch requires execute permission.
11. Data reads require read permission.
12. Data writes require write permission.
13. The fake `write` syscall reads guest memory through the same read-permission
    path or through an explicitly documented kernel/helper path.
14. CLI `dump` and debugger `mem` either require read permission or explicitly use
    an inspector override; the selected policy must be tested and documented.
15. Fault checks happen before memory bytes are returned or modified.
16. Multi-byte accesses must validate the entire access range, not only the first
    byte.
17. An access crossing from one page into another requires permission on every
    touched page.
18. Address arithmetic must be checked for overflow.
19. Zero-length mappings are rejected or ignored according to a documented policy.
20. Zero-length memory accesses are rejected or treated as no-ops according to a
    documented policy.
21. The stack starts mapped near the top of guest memory and grows downward.
22. At least one unmapped guard page sits below the stack mapping.
23. Resetting the emulator returns the memory map to the loader-created/program
    initial state.
24. `make clean` removes all generated v1.2 fixtures and temporary outputs.
25. Existing v1.1 Mach-O `info` behavior remains useful and gains mapping details
    rather than being replaced.

## Recommended Constants

Exact names may differ, but tests should pin the behavior through public APIs and
commands.

```c
#define EMU_PAGE_SIZE             4096u
#define EMU_MAP_READ              0x1u
#define EMU_MAP_WRITE             0x2u
#define EMU_MAP_EXEC              0x4u
#define EMU_FAULT_UNMAPPED        ...
#define EMU_FAULT_READ_PERMISSION ...
#define EMU_FAULT_WRITE_PERMISSION ...
#define EMU_FAULT_EXEC_PERMISSION ...
#define EMU_STACK_GUARD_PAGES     1u
```

Recommended permission text for CLI/debugger output:

```text
r--
rw-
r-x
rw-
--- guard
```

The exact formatting may differ, but tests should require stable, learner-readable
permission labels.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.2.md
lessons/v1.2-virtual-memory.md
examples/v1_2/README.md
examples/v1_2/generate_vm_fixtures.py
tests/v1_2/test_v1_2.c
tests/v1_2/test_cli_virtual_memory.sh
tests/v1_2/test_debugger_virtual_memory.sh
tests/v1_2/test_docs_virtual_memory.sh
```

Optional supporting files:

```text
tests/fixtures/vm_fixture_writer.py
examples/v1_2/permission_faults.s
examples/v1_2/stack_guard.s
examples/v1_2/mapping_inspection.txt
```

The implementation may choose different filenames, but the test suite should keep
VM tests separate from v0.1-v1.1 tests so v1.2 remains a clear memory-model
milestone.

## Test Data Strategy

v1.2 tests should use deterministic fixtures only.

1. **Raw binaries** for simple instruction-fetch and stack-guard behavior.
2. **Generated ELF fixtures** with explicit segment permission combinations.
3. **Generated Mach-O fixtures** with explicit segment protection metadata.
4. **Debugger scripts** that inspect mappings and faulting addresses.
5. **Malformed metadata fixtures** for unaligned, overlapping, overflowing, or
   impossible mappings.

Tests should not require a host cross compiler. Optional real-toolchain smoke tests
may run when tools are available, but deterministic byte-generated fixtures must be
the source of truth.

---

# Acceptance Test Cases

## Regression and Build Gates

### TC-V12-BUILD-001 — Fresh build succeeds

Run:

```sh
make clean
make
```

Expected:

- build exits with status `0`,
- emulator binary exists,
- warning policy remains clean.

### TC-V12-BUILD-002 — Full deterministic suite succeeds

Run:

```sh
make clean && make test
```

Expected:

- v0.1 through v1.2 deterministic tests pass,
- optional toolchain tests pass or skip clearly,
- command exits with status `0`.

### TC-V12-BUILD-003 — Release check includes v1.2

Run:

```sh
EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN=1 make release-check
```

Expected:

- docs checks include v1.2 docs,
- hygiene checks include v1.2 generated artifacts,
- fresh archive check runs v1.2 deterministic tests,
- no stale mention says virtual memory is still only a future feature after v1.2
  implementation lands.

### TC-V12-BUILD-004 — Repeated tests are idempotent

Run:

```sh
make test
make test
```

Expected:

- both runs pass,
- generated VM fixtures are reproducible,
- no mapping state leaks between test binaries or CLI invocations.

### TC-V12-BUILD-005 — Clean removes generated VM artifacts

Run:

```sh
make examples
make test
make clean
```

Expected:

- generated v1.2 raw/ELF/Mach-O fixtures are removed,
- temporary debugger outputs are removed,
- source fixture writers remain,
- `git status --short` does not show generated VM artifacts.

### TC-V12-BUILD-006 — Sanitizer build covers VM tests

Run sanitizer targets if available, for example:

```sh
make test-asan
make test-ubsan
```

Expected:

- VM mapping tests pass under sanitizers,
- malformed fixture tests do not trigger host memory errors,
- integer-overflow checks do not rely on undefined behavior.

## Page Model Unit Tests

### TC-V12-PAGE-001 — Page size constant is stable

Validate the public or internal page-size constant.

Expected:

- page size is the documented value,
- page size is a power of two,
- tests and docs agree on the value.

### TC-V12-PAGE-002 — Page alignment helpers round down/up correctly

Check representative addresses:

- `0x0000`,
- `0x0001`,
- `0x0fff`,
- `0x1000`,
- `0x1001`,
- last valid guest address.

Expected:

- round-down returns page base,
- round-up returns next page boundary when needed,
- overflow is detected near the end of address space.

### TC-V12-PAGE-003 — Mapping creation requires page-aligned base

Attempt to create mappings at aligned and unaligned bases.

Expected:

- aligned base succeeds,
- unaligned base is rejected or rounded only if that policy is documented,
- error message names the unaligned address.

### TC-V12-PAGE-004 — Mapping creation requires page-sized length

Attempt to create mappings with aligned length, unaligned length, and zero length.

Expected:

- aligned length succeeds,
- zero/unaligned behavior follows the documented policy,
- rejected lengths produce clear errors.

### TC-V12-PAGE-005 — Mapping cannot exceed guest memory

Create a mapping whose end address is past guest memory.

Expected:

- mapping is rejected,
- no partial mapping remains,
- error identifies the invalid range.

### TC-V12-PAGE-006 — Mapping arithmetic overflow is rejected

Create a mapping with `base + length` wrapping around.

Expected:

- mapping is rejected before touching memory,
- error says the mapping range overflows or is invalid.

### TC-V12-PAGE-007 — Overlapping mappings are rejected

Create two mappings whose ranges overlap by one byte or one page.

Expected:

- second mapping is rejected,
- original mapping remains unchanged,
- error identifies overlap.

### TC-V12-PAGE-008 — Adjacent mappings are allowed

Create two mappings where the first ends exactly where the second starts.

Expected:

- both mappings exist,
- lookup returns the correct mapping on each side of the boundary,
- no false overlap error occurs.

### TC-V12-PAGE-009 — Permission bits are stored exactly

Create mappings with `r--`, `rw-`, `r-x`, `--x`, and `rwx` if allowed.

Expected:

- stored permissions match requested permissions,
- invalid permission combinations are rejected only if the docs say so,
- printed permission labels are stable.

### TC-V12-PAGE-010 — Mapping lookup finds containing range

Lookup addresses at:

- first byte,
- middle byte,
- last byte,
- one byte before,
- one byte after.

Expected:

- containing addresses return the mapping,
- outside addresses return unmapped.

## Access Enforcement Unit Tests

### TC-V12-ACCESS-001 — Read from readable page succeeds

Map a readable page, write fixture bytes through initialization, then read them.

Expected:

- read returns expected bytes,
- no fault is recorded.

### TC-V12-ACCESS-002 — Read from unreadable page faults

Map a write-only or execute-only page according to supported permission policy and
attempt a data read.

Expected:

- access fails,
- fault type is read-permission fault,
- memory is not modified.

### TC-V12-ACCESS-003 — Write to writable page succeeds

Map a writable page and write 1, 2, 4, and 8 byte values.

Expected:

- writes succeed,
- later reads from readable mappings show little-endian bytes,
- unrelated bytes remain unchanged.

### TC-V12-ACCESS-004 — Write to read-only page faults

Map `r--` memory and attempt writes of different sizes.

Expected:

- each write fails before modifying memory,
- fault type is write-permission fault,
- error includes address and size.

### TC-V12-ACCESS-005 — Fetch from executable page succeeds

Map an executable page containing a supported instruction.

Expected:

- instruction fetch succeeds,
- CPU can execute the instruction,
- fault state remains clear.

### TC-V12-ACCESS-006 — Fetch from non-executable page faults

Place a valid instruction in a readable but non-executable page and set `pc` there.

Expected:

- execution stops before decode,
- fault type is execute-permission fault,
- error mentions the faulting `pc`.

### TC-V12-ACCESS-007 — Access to unmapped page faults

Attempt read, write, and execute accesses to unmapped addresses.

Expected:

- fault type is unmapped access,
- operation type is visible in the error,
- no host crash occurs.

### TC-V12-ACCESS-008 — Multi-byte access crossing page boundary checks both pages

Map two adjacent pages with different permissions and perform an 8-byte access that
starts near the end of the first page.

Expected:

- access succeeds only if both pages allow the operation,
- otherwise it faults before partial modification,
- error identifies the crossing range.

### TC-V12-ACCESS-009 — Multi-byte access crossing into unmapped page faults

Map one page only and perform a 4- or 8-byte access that crosses into the next page.

Expected:

- access fails,
- no partial write occurs,
- error says the full range is invalid or crosses unmapped memory.

### TC-V12-ACCESS-010 — Last-byte access at end of memory succeeds when mapped

Map the final page and read/write the final valid byte.

Expected:

- single-byte access succeeds,
- multi-byte access beyond the end faults.

### TC-V12-ACCESS-011 — Address range overflow is rejected

Attempt an access with an address and size whose sum wraps.

Expected:

- access fails before lookup,
- fault type/message identifies invalid or overflowing range.

### TC-V12-ACCESS-012 — Fault state is reset between independent runs

Trigger a memory fault, reset or reload the emulator, then run a valid program.

Expected:

- valid program succeeds,
- stale fault information does not appear in later output.

## Loader Mapping Tests

### TC-V12-LOAD-001 — Raw binary maps code with execute permission

Load a raw `.bin` program.

Expected:

- raw program bytes are mapped at the documented raw load address,
- entry page is executable,
- program can run as before.

### TC-V12-LOAD-002 — Raw binary does not make the entire address space executable

After loading a raw program, inspect mappings.

Expected:

- only documented program/stack regions are mapped,
- unrelated memory is unmapped or non-executable,
- executing outside the raw mapping faults.

### TC-V12-LOAD-003 — ELF `PF_X` segment becomes executable

Generate an ELF with an executable text segment.

Expected:

- mapping includes execute permission,
- entry inside text runs,
- `info` shows the executable permission.

### TC-V12-LOAD-004 — ELF non-executable data segment rejects fetch

Generate an ELF with code bytes in a data-only segment and set entry there or branch
there.

Expected:

- load rejects invalid entry or execution faults,
- error says execute permission is missing.

### TC-V12-LOAD-005 — ELF read-only text rejects data writes

Run a program that attempts `STR` into its text segment.

Expected:

- execution stops with write-permission fault,
- text bytes remain unchanged.

### TC-V12-LOAD-006 — ELF writable data allows data writes

Run a program that writes into a writable data segment.

Expected:

- write succeeds,
- later read returns the written value,
- program exits normally.

### TC-V12-LOAD-007 — ELF `.bss` zero-fill remains readable/writable when segment is writable

Generate an ELF with `memsz > filesz` on a writable data segment.

Expected:

- zero-filled bytes read as `0`,
- writes to zero-filled region succeed,
- `info` shows the full mapped memory range.

### TC-V12-LOAD-008 — Mach-O `initprot` or selected permission policy is honored

Generate a Mach-O fixture with text and data segments.

Expected:

- text segment mapping is executable/readable according to policy,
- data segment mapping is writable/readable according to policy,
- `info` shows stable permission labels.

### TC-V12-LOAD-009 — Mach-O data segment rejects instruction fetch

Branch or set entry into a non-executable Mach-O data mapping.

Expected:

- load rejects invalid entry or execution faults,
- error identifies execute permission.

### TC-V12-LOAD-010 — Loader rejects segment whose permission metadata is impossible

Create an ELF or Mach-O segment with invalid flags if format permits malformed bits.

Expected:

- loader either masks unknown bits according to docs or rejects the file clearly,
- behavior is deterministic.

### TC-V12-LOAD-011 — Segment crossing page boundary maps all required pages

Create a segment whose virtual address plus memory size spans several pages.

Expected:

- every touched page is mapped,
- final byte of segment is accessible according to permissions,
- byte after segment faults or belongs to another documented mapping.

### TC-V12-LOAD-012 — Segment starting at unaligned virtual address follows documented policy

Create an ELF/Mach-O fixture with an unaligned segment start.

Expected:

- loader rounds mapping down/up or rejects according to docs,
- copied bytes still appear at the intended guest address if loaded,
- no accidental access is granted outside the documented rounded range.

### TC-V12-LOAD-013 — Overlapping loader segments remain rejected

Generate overlapping ELF and Mach-O segments.

Expected:

- load fails before execution,
- error identifies segment overlap,
- no partial program is run.

### TC-V12-LOAD-014 — Adjacent loader segments remain valid

Generate adjacent text/data segments.

Expected:

- load succeeds,
- boundary address belongs to the correct segment,
- permissions differ correctly on each side.

### TC-V12-LOAD-015 — Loader failure leaves no stale mappings

Attempt to load a malformed executable, then load a valid executable in the same
process/test.

Expected:

- valid executable has only its own mappings,
- stale malformed mappings are absent.

## Stack and Guard Page Tests

### TC-V12-STACK-001 — Stack mapping exists after load

Load any valid program and inspect mappings.

Expected:

- stack mapping exists,
- stack is writable and readable,
- stack is not executable unless docs explicitly allow it.

### TC-V12-STACK-002 — Stack pointer starts inside stack mapping

After loading raw, ELF, and Mach-O programs, inspect `sp`.

Expected:

- `sp` is within or at the top boundary of the documented stack mapping,
- first normal push lands in mapped writable memory.

### TC-V12-STACK-003 — Stack guard page is unmapped or no-access

Inspect the page below the stack mapping.

Expected:

- guard page is present in mapping list as no-access or absent according to docs,
- debugger clearly identifies it as guard/unmapped.

### TC-V12-STACK-004 — Stack underflow into guard page faults

Run a program that repeatedly pushes until it crosses below the stack.

Expected:

- execution stops with unmapped or guard-page fault,
- error includes the faulting address,
- host process does not crash.

### TC-V12-STACK-005 — Normal stack frame operations still work

Run v0.3/v0.4 stack and nested-call examples.

Expected:

- normal `STP`/`LDP` frame usage still passes,
- no false guard-page fault occurs.

### TC-V12-STACK-006 — Stack execute attempt faults

Place instruction bytes on the stack and branch to `sp`.

Expected:

- fetch from stack faults with execute-permission error,
- docs explain stack is not executable.

### TC-V12-STACK-007 — Boundary push at first valid stack byte succeeds

Set up a controlled push that writes exactly to the lowest valid stack address but
not below it.

Expected:

- access succeeds,
- next lower push faults.

## CPU Integration Tests

### TC-V12-CPU-001 — Fetch permission is checked before decode

Put an unsupported opcode in a non-executable page and set `pc` there.

Expected:

- execute-permission fault is reported,
- unsupported-opcode error is not reported first.

### TC-V12-CPU-002 — Data read permission is checked before load decode completes side effects

Run `LDR` from an unreadable page.

Expected:

- read-permission fault occurs,
- destination register remains unchanged.

### TC-V12-CPU-003 — Data write permission is checked before write-back side effects

Run pre-index or post-index store that would update a base register while writing
to a non-writable page.

Expected:

- write-permission fault occurs,
- memory remains unchanged,
- base register write-back policy is documented and tested.

### TC-V12-CPU-004 — Pair load/store crossing page boundary respects permissions

Run `LDP`/`STP` near a page boundary.

Expected:

- operation succeeds only if the full accessed range is allowed,
- no partial register/memory update occurs on fault, or documented partial behavior
  is explicitly tested.

### TC-V12-CPU-005 — Branch into unmapped page faults at fetch

Run a branch whose target is unmapped.

Expected:

- branch updates `pc` if that is existing CPU behavior,
- next fetch stops with unmapped execute fault,
- trace shows enough context to understand the transition.

### TC-V12-CPU-006 — Branch into non-executable mapped page faults at fetch

Run a branch into a readable data page.

Expected:

- execution stops with execute-permission fault,
- data page bytes are not decoded as instructions.

### TC-V12-CPU-007 — Syscall write respects guest memory read policy

Call fake `write` with a buffer in readable and unreadable memory.

Expected:

- readable buffer writes output as before,
- unreadable/unmapped buffer fails with documented runtime error or fake errno,
- behavior matches docs.

### TC-V12-CPU-008 — Guest exit status still propagates

Run raw, ELF, and Mach-O examples that exit normally.

Expected:

- page model does not change documented fake exit behavior,
- CLI exit statuses remain stable.

## CLI Mapping and Fault Tests

### TC-V12-CLI-001 — `info` shows mappings for raw binary

Run:

```sh
./emulator info examples/v1_2/simple_raw.bin
```

Expected:

- output includes `format: raw` or equivalent,
- mapping list includes program and stack regions,
- permissions are visible and stable.

### TC-V12-CLI-002 — `info` shows mappings for ELF

Run `info` on a generated ELF fixture.

Expected:

- output includes ELF format,
- each loadable segment appears as a mapping,
- permissions reflect ELF flags.

### TC-V12-CLI-003 — `info` shows mappings for Mach-O

Run `info` on a generated Mach-O fixture.

Expected:

- output includes Mach-O format,
- segment names and permissions appear,
- symbol inspection from v1.1 still works.

### TC-V12-CLI-004 — Write to read-only text prints clear fault

Run a fixture that stores into code/text memory.

Expected:

- command exits nonzero,
- stderr contains write-permission fault wording,
- stderr includes address and size.

### TC-V12-CLI-005 — Execute from data prints clear fault

Run a fixture that branches to data memory.

Expected:

- command exits nonzero,
- stderr contains execute-permission fault wording,
- trace/debug output can identify target address.

### TC-V12-CLI-006 — Read unmapped memory prints clear fault

Run a fixture that loads from an unmapped address.

Expected:

- command exits nonzero,
- stderr says unmapped read or equivalent,
- address appears in hex.

### TC-V12-CLI-007 — `trace` includes fault context

Run trace mode on a program that eventually faults.

Expected:

- trace shows instructions up to the fault,
- fault line includes operation, address, and permission reason,
- trace output remains deterministic.

### TC-V12-CLI-008 — `regs` after fault follows documented policy

Run `regs` on a faulting program.

Expected:

- either final register state is printed with fault status or command fails without
  register dump according to docs,
- behavior is stable and tested.

### TC-V12-CLI-009 — `dump` readable memory succeeds

Run `dump` on a readable mapping.

Expected:

- bytes are printed as before,
- output includes expected data,
- permission policy is respected.

### TC-V12-CLI-010 — `dump` unreadable/unmapped memory fails or is marked inspector-only

Run `dump` on unreadable and unmapped addresses.

Expected:

- behavior matches documented policy,
- failure message is clear if permission checks apply,
- no host crash occurs.

### TC-V12-CLI-011 — Help text mentions `info`/mapping support

Run:

```sh
./emulator
./emulator --help
```

Expected:

- usage lists current commands,
- mapping/permission inspection is discoverable,
- no stale v1.1-only wording hides v1.2 behavior.

### TC-V12-CLI-012 — Invalid mapping command arguments fail cleanly

Invoke new CLI/debugger mapping commands with missing, extra, and malformed
arguments.

Expected:

- command exits nonzero or returns debugger error,
- no command partially executes,
- usage message is specific.

## Debugger Tests

### TC-V12-DBG-001 — Debugger lists mappings

Start debugger and run mapping-list command, for example `maps`.

Expected:

- output lists program, stack, and guard/unmapped regions,
- permissions are readable,
- addresses are stable hex values.

### TC-V12-DBG-002 — Debugger identifies mapping for address

Run a command such as `map <addr>` for text, data, stack, guard, and unmapped
addresses.

Expected:

- mapped addresses show mapping name/range/permissions,
- guard/unmapped addresses are identified clearly.

### TC-V12-DBG-003 — Debugger step stops on execute fault

Set a breakpoint before a branch into non-executable memory, then step/continue.

Expected:

- debugger stops at the fault,
- prompt remains usable or exits according to documented error policy,
- fault details are printed once.

### TC-V12-DBG-004 — Debugger memory read follows dump policy

Use debugger `mem` on readable, unreadable, and unmapped pages.

Expected:

- behavior matches CLI `dump` policy,
- messages are consistent.

### TC-V12-DBG-005 — Breakpoints in unmapped memory are rejected

Attempt to set a breakpoint at an unmapped address.

Expected:

- debugger rejects it with clear error or allows pending breakpoints only if docs say
  so,
- behavior is deterministic.

### TC-V12-DBG-006 — Breakpoints in non-executable mapped memory are rejected or warned

Attempt to set a breakpoint on a data page.

Expected:

- debugger warns or rejects according to docs,
- later execution behavior remains deterministic.

### TC-V12-DBG-007 — Mapping commands reject malformed input

Use missing, non-numeric, negative, overflowing, and extra arguments.

Expected:

- debugger reports usage errors,
- no crash,
- session remains usable.

### TC-V12-DBG-008 — Mapping list remains stable after reset/run

In one debugger session, list mappings, run to completion/fault, reset if supported,
and list again.

Expected:

- mapping list is stable unless documented program loading changes it,
- stale temporary fault state is absent after reset.

## Malformed and Edge-Case Fixture Tests

### TC-V12-EDGE-001 — ELF segment with `filesz > memsz` remains rejected

Expected:

- loader rejects before mapping,
- error remains specific.

### TC-V12-EDGE-002 — ELF segment with permissions but zero memory size follows policy

Expected:

- zero-size segment is ignored or rejected according to docs,
- no zero-length mapping corrupts mapping table.

### TC-V12-EDGE-003 — Mach-O segment with `filesize > vmsize` remains rejected

Expected:

- loader rejects before mapping,
- no partial mappings remain.

### TC-V12-EDGE-004 — Mach-O zero-sized segment follows policy

Expected:

- zero-sized segment behavior matches docs,
- `info` output does not print a bogus range.

### TC-V12-EDGE-005 — Maximum number of mappings is enforced

Generate a fixture with more segments/pages than the supported mapping table.

Expected:

- loader rejects with clear capacity error,
- no host memory overwrite occurs.

### TC-V12-EDGE-006 — Many small adjacent mappings remain sorted or lookup-safe

Generate several adjacent mappings.

Expected:

- load succeeds if within capacity,
- lookup works for every boundary,
- `info` output remains deterministic.

### TC-V12-EDGE-007 — Mapping at address zero follows documented policy

Attempt to map page zero.

Expected:

- allowed or rejected according to docs,
- null-like addresses are not accidentally special unless documented.

### TC-V12-EDGE-008 — Mapping at highest valid page follows documented policy

Attempt to map the final page in guest memory.

Expected:

- mapping succeeds if exactly in bounds,
- any access beyond final byte faults.

### TC-V12-EDGE-009 — Permissionless page behaves as guard/no-access

Create a mapping with no permissions if supported.

Expected:

- read/write/execute all fault,
- `info`/debugger labels it clearly.

### TC-V12-EDGE-010 — Duplicate identical mapping is rejected or merged consistently

Create the same mapping twice.

Expected:

- behavior follows documented policy,
- no duplicate lookup ambiguity appears.

### TC-V12-EDGE-011 — Loader maps segment smaller than one page safely

Create a tiny segment containing a few bytes.

Expected:

- page-level mapping covers the containing page according to policy,
- bytes outside segment but inside page have documented initialization/access
  behavior.

### TC-V12-EDGE-012 — File-backed bytes do not leak into page padding unexpectedly

Load a segment whose file bytes end before page boundary.

Expected:

- padding bytes are zero or inaccessible according to docs,
- host/uninitialized memory is never exposed.

### TC-V12-EDGE-013 — Mutated mapping metadata rejects cleanly

Apply deterministic mutations to segment address, size, flags, and counts.

Expected:

- invalid variants reject clearly,
- valid variants behave according to docs,
- no mutation crashes the host process.

### TC-V12-EDGE-014 — Fault messages preserve original operation context

Trigger read, write, and execute faults at the same address.

Expected:

- messages differ by operation,
- learner can tell what instruction/action caused the fault.

## Documentation and Lesson Tests

### TC-V12-DOC-001 — README links v1.2 test plan and lesson

Expected:

- README test-plan list includes v1.2,
- lesson list includes v1.2 after implementation,
- planned-version text no longer contradicts implemented behavior.

### TC-V12-DOC-002 — v1.2 lesson explains pages before permissions

Expected:

- lesson teaches page size, mappings, and address ranges first,
- then read/write/execute permissions,
- then faults and debugger usage.

### TC-V12-DOC-003 — Zero-fill explanation remains consistent with page mappings

Expected:

- v1.0/v1.1 zero-fill language still makes sense under v1.2,
- v1.2 docs clarify page padding and BSS-like zero-fill.

### TC-V12-DOC-004 — Fault names are consistent across docs and CLI

Expected:

- docs mention the same fault categories used by the emulator,
- examples use current output wording or intentionally stable substrings.

### TC-V12-DOC-005 — Out-of-scope MMU features are explicit

Expected:

- docs say v1.2 is not a real ARM MMU,
- page tables, TLBs, ASLR, demand paging, signals, and kernel mode remain future or
  out of scope.

### TC-V12-DOC-006 — Examples README documents generated fixtures

Expected:

- `examples/v1_2/README.md` explains how to generate fixtures,
- commands are copy-pasteable,
- optional tooling is clearly marked.

### TC-V12-DOC-007 — Debugger command docs include mapping commands

Expected:

- debugger command list mentions `maps`/`map <addr>` or chosen names,
- invalid-argument behavior is documented.

### TC-V12-DOC-008 — Release docs mention clean behavior for VM fixtures

Expected:

- generated VM artifacts are documented as ignored/cleaned,
- release hygiene checks know about them.

## Release Acceptance

### TC-V12-REL-001 — v1.2 examples run from clean checkout

From a clean checkout, run the documented v1.2 example commands.

Expected:

- fixture generation succeeds,
- at least one normal program runs,
- at least one permission-fault program fails with expected error,
- at least one debugger script demonstrates mappings.

### TC-V12-REL-002 — Fresh archive includes VM source but not generated junk

Create a release archive from `HEAD` and inspect it.

Expected:

- v1.2 docs/tests/fixture writers are present,
- generated binaries/logs are absent unless intentionally versioned,
- `.git` inclusion follows the existing project packaging policy.

### TC-V12-REL-003 — Fresh archive test passes after extraction

Extract the release archive into a new directory and run:

```sh
make test
```

Expected:

- v1.2 tests pass in the extracted copy,
- no test depends on untracked files from the original checkout.

### TC-V12-REL-004 — CLI failure statuses are stable

Run representative permission/unmapped fault programs under `run`, `trace`, and
`regs`.

Expected:

- exit status policy is documented,
- failure statuses are stable,
- normal guest `exit` status remains distinguishable from emulator failure.

### TC-V12-REL-005 — Scope discipline is preserved

Review docs, tests, and examples.

Expected:

- v1.2 does not claim real MMU support,
- no page-table/TLB/kernel-mode behavior is accidentally required,
- future v1.3/v1.4 roadmap items remain future work.

# v1.3 Test Plan — Memory-Mapped Devices

## Version Goal

v1.3 teaches the next platform milestone: move from memory that is only RAM plus
loader-created mappings to a small **memory-mapped device bus**. The emulator
should be able to route guest memory accesses either to normal RAM or to fixed
virtual device ranges, while keeping faults deterministic and easy to explain.

Before v1.3, v1.2 can enforce page mappings and permissions for raw binaries,
ELF64 executables, Mach-O fixtures, stack memory, instruction fetch, data reads,
data writes, debugger inspection, and CLI memory dumping. v1.3 should preserve
that model and add a second destination for selected data accesses:

```text
CPU data access -> bus route -> RAM page or device register -> result/fault
```

The v1.3 promise is:

```text
register devices -> route data reads/writes -> emulate UART/timer/random -> inspect the platform map
```

v1.3 is still **not** a real system-on-chip. It should not model interrupts,
DMA, device trees, PCI, MMIO ordering, caches, privilege levels, kernel drivers,
real wall-clock time, host entropy, or production hardware behavior. The goal is
a deterministic teaching platform where learners can print without fake syscalls
and can observe how simple hardware registers differ from ordinary RAM.

## Scope

### In Scope

- v0.1 through v1.2 regression behavior must continue to pass unless a behavior is
  intentionally tightened and documented by the v1.3 bus model.
- Add a device-bus layer used by data memory accesses.
- Route each data read/write to exactly one target:
  - RAM mapping,
  - registered device range,
  - deterministic unmapped/invalid fault.
- Register fixed memory-mapped devices at documented addresses.
- Add an initial UART console device.
- Add an initial deterministic timer device.
- Add an initial deterministic random-number device.
- Provide stable behavior for byte, halfword, word, and doubleword accesses when
  those widths are supported by existing load/store instructions.
- Define and test device access policy for:
  - read-only registers,
  - write-only registers,
  - read/write registers,
  - invalid offsets,
  - unsupported widths,
  - unaligned addresses,
  - accesses that cross register or device boundaries.
- Keep instruction fetch restricted to executable RAM mappings; device ranges are
  not executable.
- Preserve v1.2 page permission behavior for RAM.
- Define whether device ranges appear as mappings in CLI/debugger inspection, and
  test the selected output.
- Make UART output deterministic and testable from CLI `run`, `trace`, and
  debugger execution.
- Make timer and random behavior deterministic for tests.
- Keep fake syscall behavior from v0.7 working; MMIO UART is an additional output
  path, not a replacement.
- Add unit, integration, CLI, debugger, docs, malformed-access, edge-case, and
  release tests.
- Document the bus model, device memory map, and device-register semantics.

### Out of Scope

- Real ARM exception levels, `SCTLR_ELx`, `TTBR_ELx`, `MAIR_ELx`, or MMU device
  memory attributes.
- Interrupts, IRQ/FIQ delivery, GIC, or interrupt controller modeling.
- DMA or devices reading/writing guest RAM independently.
- Device trees, ACPI, PCI, USB, virtio, or board discovery.
- Real host terminal modes or asynchronous input.
- Real wall-clock timer accuracy.
- Non-deterministic host entropy as the source of test-visible random values.
- Memory barriers, cache coherency, exclusive accesses, atomics, or ordering
  models.
- Privilege/user/kernel access restrictions.
- Dynamic device hotplug.
- `mmap`, heap allocation, dynamic linking, or OS-level driver APIs.
- A production-quality UART, timer, or RNG peripheral.
- Changing the existing teaching RAM size unless explicitly documented.

## Implementation Assumptions

These assumptions should become explicit implementation decisions before tests are
finalized.

1. The guest address type remains 64-bit.
2. The v1.2 RAM capacity remains deterministic, but device addresses may live
   outside the RAM byte-array size.
3. Device access is only for data reads/writes, not instruction fetch.
4. Instruction fetch from a device address must fail deterministically as a
   non-executable/unmapped execute fault according to the documented v1.3 policy.
5. Device ranges do not overlap RAM mappings.
6. Device ranges do not overlap each other.
7. Device range start and size are stable and documented.
8. The suggested initial map is:

   ```text
   0x0000000000001000 - code/program area
   0x0000000000010000 - data area
   0x0000000000080000 - stack area
   0x0000000009000000 - UART
   0x0000000009010000 - timer
   0x0000000009020000 - random device
   ```

9. Device ranges are probably page-sized for learner readability, but register
   behavior is still defined at byte offsets inside the range.
10. A data access is routed atomically: either the whole access is valid for one
    target, or the access faults before any RAM or device state changes.
11. A multi-byte access may not silently split across two devices or across RAM
    and a device.
12. Address arithmetic must be checked for overflow before route lookup.
13. RAM permission checks still use v1.2 read/write permissions.
14. Device permission checks use device-register access policy, not RAM page
    permission bits, unless the implementation intentionally models device ranges
    as synthetic mappings with documented permission labels.
15. The UART data register accepts byte writes that append a byte to stdout.
16. Wider UART writes have a documented policy: either rejected as unsupported or
    emitted little-endian byte order. Tests should pin the chosen behavior.
17. UART status reads return deterministic bits.
18. UART invalid offsets fault deterministically and do not emit output.
19. Timer reads are deterministic. Recommended teaching behavior: a monotonically
    increasing tick counter advanced by executed instructions or explicit device
    reads, not host wall-clock time.
20. Timer writes are either rejected or reset/configure the deterministic counter;
    the chosen policy must be tested.
21. Random reads are deterministic under test. Recommended teaching behavior: a
    small seeded PRNG with a documented default seed.
22. Random writes are either rejected or set the seed; the chosen policy must be
    tested.
23. Resetting or reloading a program resets device state unless a persistent-device
    policy is documented.
24. Debugger `run`, `step`, and `continue` use the same device state transition
    rules as CLI `run`.
25. Trace mode should include the instruction that caused device output; it does
    not need to print an additional device trace unless documented.
26. CLI `dump` and debugger `mem` should not treat device registers as ordinary
    memory unless an explicit inspector policy is documented.
27. The release archive must include source fixture generators, docs, and tests,
    but not generated binaries/logs.

## Recommended Device Map and Registers

Exact names may differ, but tests should pin public behavior through APIs,
fixtures, and CLI/debugger output.

```c
#define EMU_DEVICE_UART_BASE   0x09000000ull
#define EMU_DEVICE_TIMER_BASE  0x09010000ull
#define EMU_DEVICE_RANDOM_BASE 0x09020000ull
#define EMU_DEVICE_SIZE        0x00001000ull
```

Recommended UART registers:

```text
UART + 0x00  DATA    write byte -> append byte to stdout
UART + 0x04  STATUS  read word  -> bit 0 means writable, bit 1 means readable if input exists
```

Recommended timer registers:

```text
TIMER + 0x00  TICKS_LO  read word  -> low 32 bits of deterministic tick counter
TIMER + 0x04  TICKS_HI  read word  -> high 32 bits of deterministic tick counter
TIMER + 0x08  RESET     write word -> reset counter to 0, if reset writes are supported
```

Recommended random registers:

```text
RANDOM + 0x00 VALUE     read word  -> next deterministic PRNG value
RANDOM + 0x04 SEED      write word -> set deterministic PRNG seed, if seed writes are supported
```

Recommended CLI/debugger labels:

```text
dev 0x0000000009000000-0x0000000009001000 name=uart perms=rw-
dev 0x0000000009010000-0x0000000009011000 name=timer perms=r-- or rw-
dev 0x0000000009020000-0x0000000009021000 name=random perms=r-- or rw-
```

The exact formatting may differ, but tests should require stable,
learner-readable names, ranges, and access labels.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.3.md
lessons/v1.3-memory-mapped-devices.md
examples/v1_3/README.md
examples/v1_3/generate_device_fixtures.py
tests/fixtures/device_fixture_writer.py
tests/v1_3/test_v1_3.c
tests/v1_3/test_cli_devices.sh
tests/v1_3/test_debugger_devices.sh
tests/v1_3/test_docs_devices.sh
```

Optional supporting fixtures:

```text
examples/v1_3/mmio_uart_hello.s
examples/v1_3/mmio_timer_read.s
examples/v1_3/mmio_random_read.s
examples/v1_3/mmio_invalid_offset.s
examples/v1_3/mmio_cross_boundary.s
examples/v1_3/platform_map.txt
```

The implementation may choose different filenames, but the test suite should keep
v1.3 device tests separate from v1.2 VM tests so the device-bus milestone remains
clear.

## Test Data Strategy

v1.3 tests should use deterministic fixtures only.

1. **Raw binaries** for simple MMIO instruction sequences.
2. **Generated ELF fixtures** that access MMIO addresses from code and data.
3. **Generated Mach-O fixtures** only if they add meaningful loader coverage; raw
   and ELF fixtures are enough for most device behavior.
4. **Debugger scripts** that step across MMIO accesses and inspect platform maps.
5. **Malformed access fixtures** for invalid offsets, bad widths, unaligned
   addresses, address overflow, and cross-boundary accesses.
6. **Golden stdout/stderr files** for UART and fault-message behavior.
7. **Golden trace snippets** for device-access programs.

Tests should not require a host cross compiler. Optional real-toolchain smoke tests
may run when tools are available, but deterministic byte-generated fixtures must be
the source of truth.

---

# Acceptance Test Cases

## Regression and Build Gates

### TC-V13-BUILD-001 — Fresh build succeeds

Run:

```sh
make clean
make
```

Expected:

- build exits with status `0`,
- emulator binary exists,
- warning policy remains clean.

### TC-V13-BUILD-002 — Full deterministic suite succeeds

Run:

```sh
make clean && make test
```

Expected:

- v0.1 through v1.3 deterministic tests pass,
- optional toolchain tests pass or skip clearly,
- command exits with status `0`.

### TC-V13-BUILD-003 — v1.2 memory behavior remains stable

Run representative v1.2 VM tests and examples after v1.3 device registration is
added.

Expected:

- raw, ELF64, and Mach-O mappings keep their v1.2 permissions,
- stack guard faults still happen,
- RAM write/execute/read faults are unchanged,
- device ranges do not weaken RAM permission checks.

### TC-V13-BUILD-004 — Repeated tests are idempotent

Run:

```sh
make test
make test
```

Expected:

- both runs pass,
- generated device fixtures are stable,
- UART logs and debugger temp files are overwritten or cleaned deterministically,
- no test depends on stale device state from the previous run.

### TC-V13-BUILD-005 — `make clean` removes v1.3 artifacts

Generate all v1.3 examples and tests, then run:

```sh
make clean
```

Expected:

- generated `.bin`, `.elf`, `.macho`, logs, temp debugger scripts, and golden
  comparison outputs are removed,
- source fixture writers, docs, and handwritten examples remain,
- no generated v1.3 artifact remains tracked accidentally.

## Device Bus Routing

### TC-V13-BUS-001 — RAM access still routes to RAM

Execute a program that stores to and loads from a writable RAM mapping.

Expected:

- store succeeds,
- load returns the stored value,
- no device callback runs,
- final registers match the v1.2 behavior.

### TC-V13-BUS-002 — UART range routes to UART device

Execute a program that writes one byte to `0x09000000`.

Expected:

- data write does not modify RAM,
- UART device receives exactly one byte,
- CLI stdout contains that byte,
- program can continue to halt or exit.

### TC-V13-BUS-003 — Timer range routes to timer device

Execute a program that reads from `0x09010000`.

Expected:

- read is handled by timer,
- returned value follows deterministic timer policy,
- no RAM mapping is required at that address.

### TC-V13-BUS-004 — Random range routes to random device

Execute a program that reads from `0x09020000`.

Expected:

- read is handled by random device,
- returned value follows deterministic PRNG policy,
- no RAM mapping is required at that address.

### TC-V13-BUS-005 — Unmapped non-device address still faults

Access an address that is neither RAM nor a registered device.

Expected:

- emulator reports an unmapped memory/device fault,
- fault includes access type, address, width, and instruction context,
- no device state changes.

### TC-V13-BUS-006 — Device ranges are matched by whole range, not only base

Access a valid register inside the UART page, such as status at `UART + 0x04`.

Expected:

- access routes to UART,
- status value follows documented policy,
- route lookup does not require the exact base address only.

### TC-V13-BUS-007 — Last byte inside device range is recognized

Access the last valid byte offset of a device range with a one-byte operation, if
that offset is documented as valid or generically reserved.

Expected:

- route lookup identifies the device range,
- register policy decides success or invalid-offset fault,
- address is not mistaken for unmapped merely because it is near the end.

### TC-V13-BUS-008 — First byte after device range is unmapped

Access `device_base + device_size`.

Expected:

- address is outside the device,
- access faults as unmapped unless another device intentionally starts there,
- no off-by-one inclusion bug exists.

### TC-V13-BUS-009 — Device ranges reject overlap at registration

Unit-test device registration with two overlapping ranges.

Expected:

- registration fails deterministically,
- error identifies the conflicting ranges or device names,
- previous registrations remain unchanged.

### TC-V13-BUS-010 — Adjacent device ranges are allowed

Unit-test two devices where one range ends exactly at the next range start.

Expected:

- both registrations succeed,
- last byte of the first range routes to first device,
- first byte of the second range routes to second device.

### TC-V13-BUS-011 — Device range overflow is rejected

Attempt to register or access a device range whose `base + size` overflows
`uint64_t`.

Expected:

- overflow is detected,
- registration or access fails before state changes,
- diagnostic is deterministic.

### TC-V13-BUS-012 — Zero-size device range is rejected

Attempt to register a device with size `0`.

Expected:

- registration fails,
- no route is added,
- diagnostic is clear.

## UART Device

### TC-V13-UART-001 — Single-byte UART hello

Run a fixture that writes `H`, `i`, and newline to the UART data register.

Expected:

- stdout is exactly `Hi\n`,
- stderr is empty,
- guest exits or halts normally,
- final register state is stable.

### TC-V13-UART-002 — UART output works without fake `write` syscall

Run an MMIO UART program with no `SVC #0` instruction.

Expected:

- output still appears,
- syscall counters or syscall-specific behavior are not involved,
- docs can truthfully state that v1.3 prints through MMIO.

### TC-V13-UART-003 — Fake syscall output still works

Run an existing v0.7/v0.9 fake `write` example.

Expected:

- output matches pre-v1.3 behavior,
- MMIO device registration does not break fake syscalls,
- invalid syscall behavior remains unchanged.

### TC-V13-UART-004 — UART and syscall output ordering is stable

Run a program that writes `A` through UART, then `B` through fake syscall, then
`C` through UART.

Expected:

- stdout is exactly `ABC`,
- output ordering is deterministic,
- buffering does not reorder device output and syscall output.

### TC-V13-UART-005 — UART status register reports writable

Read `UART + 0x04`.

Expected:

- value has the documented writable bit set,
- unsupported or reserved bits are stable,
- repeated reads return the same value unless docs state otherwise.

### TC-V13-UART-006 — UART data read policy is enforced

Read from `UART + 0x00`.

Expected, depending on documented policy:

- either returns a deterministic value such as `0` or `-1`,
- or faults as read-from-write-only register,
- test locks in the chosen behavior.

### TC-V13-UART-007 — UART invalid offset faults

Write to `UART + 0x10` when that offset is not documented.

Expected:

- access faults as invalid device offset,
- no output byte is emitted,
- fault message includes device name and offset.

### TC-V13-UART-008 — UART unsupported width is deterministic

Perform a halfword, word, or doubleword write to the UART data register.

Expected, depending on documented policy:

- either the write is rejected as unsupported width,
- or bytes are emitted in documented little-endian order,
- behavior is identical across `run`, `trace`, and debugger.

### TC-V13-UART-009 — UART NUL byte is preserved

Write byte `0x00` followed by `X` to UART.

Expected:

- captured stdout contains both bytes in order,
- tests compare binary output rather than C strings,
- no truncation occurs at NUL.

### TC-V13-UART-010 — UART high-bit byte is preserved

Write byte `0xff` to UART.

Expected:

- captured stdout contains exactly byte `0xff`,
- output is not sign-extended or converted to text,
- no locale-dependent behavior appears.

### TC-V13-UART-011 — UART write fault is atomic

Attempt an invalid multi-byte UART write that would partly include a valid data
register and partly include invalid offsets.

Expected:

- entire write faults,
- no partial bytes are emitted,
- program state after fault is deterministic.

### TC-V13-UART-012 — Large UART output remains deterministic

Run a fixture that writes a fixed longer string, such as 1024 bytes.

Expected:

- stdout exactly matches the golden file,
- no dropped or duplicated bytes,
- emulator exits normally.

## Timer Device

### TC-V13-TIMER-001 — Timer low register is readable

Read `TIMER + 0x00`.

Expected:

- read succeeds,
- returned value is deterministic,
- register width policy is honored.

### TC-V13-TIMER-002 — Timer high register is readable

Read `TIMER + 0x04`.

Expected:

- read succeeds,
- value corresponds to the documented high 32 bits,
- initial tests see the expected high value, usually `0`.

### TC-V13-TIMER-003 — Timer is monotonic under documented policy

Read the timer twice with known instructions between reads.

Expected:

- second value is greater than or equal to first value,
- exact delta matches documented instruction-count or read-count policy if pinned,
- no host wall-clock dependency exists.

### TC-V13-TIMER-004 — Timer resets if reset register is supported

Write to `TIMER + 0x08`, then read low/high registers.

Expected:

- if reset is supported, counter returns to documented reset value,
- if reset is not supported, write faults as write-to-read-only/invalid register,
- behavior is tested and documented either way.

### TC-V13-TIMER-005 — Timer read width policy is enforced

Read timer registers using byte, halfword, word, and doubleword load forms.

Expected:

- supported widths return documented little-endian slices or full values,
- unsupported widths fault clearly,
- no out-of-bounds register bytes are read silently.

### TC-V13-TIMER-006 — Timer invalid offset faults

Read or write `TIMER + 0x20`.

Expected:

- invalid device offset fault,
- diagnostic includes timer name and offset,
- timer state remains unchanged.

### TC-V13-TIMER-007 — Timer state resets between program loads

Run the same timer fixture twice in separate CLI invocations or emulator resets.

Expected:

- first observed timer value is identical between runs,
- tests are independent and reproducible,
- previous run does not leak timer state.

### TC-V13-TIMER-008 — Timer does not advance during failed access if documented

Trigger an invalid timer access, then inspect timer state in a controlled unit test.

Expected:

- invalid access does not partially update timer state unless explicitly documented,
- state transition policy is deterministic.

## Random Device

### TC-V13-RNG-001 — Random value register is readable

Read `RANDOM + 0x00`.

Expected:

- read succeeds,
- returned value is deterministic under the default seed,
- value is stable in golden tests.

### TC-V13-RNG-002 — Consecutive random reads advance sequence

Read random value twice.

Expected:

- values match the documented PRNG sequence,
- if the first two values differ, exact expected values are pinned,
- if a constant stub is intentionally chosen, docs state that and tests pin it.

### TC-V13-RNG-003 — Random seed write resets sequence if supported

Write a seed value, then read two values.

Expected:

- if seeding is supported, sequence restarts from the seed deterministically,
- if seeding is not supported, write faults clearly,
- no host entropy is required for deterministic tests.

### TC-V13-RNG-004 — Random invalid offset faults

Read or write `RANDOM + 0x20`.

Expected:

- invalid offset fault,
- diagnostic includes random-device name and offset,
- PRNG state does not advance on invalid access.

### TC-V13-RNG-005 — Random unsupported width is deterministic

Read random value using unsupported widths.

Expected:

- unsupported widths fault, or documented little-endian slices are returned,
- PRNG advances exactly as documented,
- no accidental double-advance occurs for a wide read.

### TC-V13-RNG-006 — Random state resets between runs

Run the same fixture twice.

Expected:

- values are identical across independent runs,
- tests never depend on global host process state,
- fixture order does not affect PRNG output.

## Access Width, Alignment, and Boundary Edge Cases

### TC-V13-EDGE-001 — Byte MMIO access at valid register succeeds

Use `STRB`/`LDRB` against a documented byte-capable register.

Expected:

- access succeeds,
- only one byte is consumed or produced,
- no adjacent register is affected.

### TC-V13-EDGE-002 — Halfword MMIO policy is enforced

Use a halfword access against each device type.

Expected:

- success or fault follows the documented width policy,
- little-endian behavior is pinned if successful,
- no silent truncation occurs.

### TC-V13-EDGE-003 — Word MMIO policy is enforced

Use a 32-bit access against each device type.

Expected:

- supported registers work,
- unsupported registers fault,
- returned/stored values are stable.

### TC-V13-EDGE-004 — Doubleword MMIO policy is enforced

Use a 64-bit access against each device type.

Expected:

- supported wide registers work or unsupported accesses fault,
- no register-pair crossing happens unless explicitly documented,
- diagnostics include width.

### TC-V13-EDGE-005 — Unaligned MMIO access policy is enforced

Perform a word access at `UART + 0x01` or similar.

Expected:

- unaligned access either faults clearly or follows documented byte-lane policy,
- no host undefined behavior occurs,
- behavior is consistent across devices.

### TC-V13-EDGE-006 — Access crossing register boundary is atomic

Perform a multi-byte access that starts at the last byte of a valid register and
extends into an invalid/reserved offset.

Expected:

- entire access faults,
- no partial device side effect occurs,
- fault identifies boundary or invalid offset.

### TC-V13-EDGE-007 — Access crossing device boundary is atomic

Perform a multi-byte access starting near the end of a device range and extending
past `base + size`.

Expected:

- entire access faults,
- no partial device side effect occurs,
- route lookup reports cross-boundary or unmapped tail.

### TC-V13-EDGE-008 — Access crossing RAM-to-device boundary is rejected

If a test-only setup creates RAM adjacent to a device, perform a multi-byte access
that spans both.

Expected:

- emulator rejects split-target access,
- RAM remains unchanged,
- device state remains unchanged.

### TC-V13-EDGE-009 — Address plus width overflow is rejected

Access address `UINT64_MAX - 1` with width `4`.

Expected:

- overflow fault occurs before route lookup,
- no state changes,
- diagnostic is stable.

### TC-V13-EDGE-010 — Zero-width helper access policy is tested

Unit-test internal bus/memory helper with width `0`, if such helper is public to
tests.

Expected:

- zero-width access is rejected or no-ops according to documented policy,
- no device callback sees a zero-width access accidentally.

### TC-V13-EDGE-011 — Device access after halted program does not occur

Run a fixture that places MMIO instructions after `HLT` or guest `exit`.

Expected:

- instructions after stop are not executed,
- no device side effects happen after stop,
- output matches only pre-stop writes.

### TC-V13-EDGE-012 — Device access at instruction limit remains deterministic

Run a loop that repeatedly writes to UART until the instruction limit is reached.

Expected:

- output count is deterministic,
- emulator reports instruction-limit failure as before,
- no partial instruction/device side effect occurs beyond the executed limit.

## CPU Instruction Integration

### TC-V13-CPU-001 — `STRB` can write UART data

Use the supported byte-store instruction to write a character to UART.

Expected:

- stdout contains the character,
- data path uses bus write,
- final registers and `pc` are stable.

### TC-V13-CPU-002 — `LDR` can read timer word

Use a supported 32-bit or 64-bit load form to read timer state.

Expected:

- destination register receives expected value,
- load does not require RAM mapping at timer address,
- trace/disassembly remain readable.

### TC-V13-CPU-003 — `LDRB` can read a byte-capable device register if supported

Read a byte from a documented readable register.

Expected:

- low byte is returned,
- upper bits of the destination register follow existing W/X register rules,
- behavior matches RAM load semantics where applicable.

### TC-V13-CPU-004 — Pre-index MMIO address update policy is correct

Use a pre-index load/store that targets a device register.

Expected:

- base register update follows existing ARM64 semantics,
- device side effect happens only if final access is valid,
- on fault, base-register update policy is documented and tested.

### TC-V13-CPU-005 — Post-index MMIO address update policy is correct

Use a post-index load/store that targets a device register.

Expected:

- access uses original base address,
- base register is updated according to existing semantics if access succeeds,
- fault behavior for base update is documented and tested.

### TC-V13-CPU-006 — Pair load/store to device policy is enforced

Use `STP`/`LDP` against a device address.

Expected:

- either rejected as unsupported multi-register device access,
- or handled through documented consecutive-width policy,
- no partial first-register side effect occurs when second part is invalid.

### TC-V13-CPU-007 — Device faults include current instruction context

Trigger an invalid device access from an instruction.

Expected:

- error includes `pc`, raw opcode, access address, width, and device/fault type,
- message style is consistent with v1.2 memory faults,
- tests match stable substrings rather than fragile full prose where appropriate.

## Loader and Program Format Integration

### TC-V13-LOAD-001 — Raw binary can access MMIO addresses

Run a raw fixture that uses immediate construction to access UART.

Expected:

- raw loader behavior remains unchanged,
- MMIO access succeeds,
- stdout matches golden output.

### TC-V13-LOAD-002 — ELF64 program can access MMIO addresses

Run a generated ELF fixture that writes to UART.

Expected:

- ELF segment mappings load normally,
- MMIO address does not need an ELF `PT_LOAD` segment,
- stdout matches golden output.

### TC-V13-LOAD-003 — Mach-O fixture can access MMIO addresses if included

Run a generated Mach-O fixture that writes to UART.

Expected:

- Mach-O loader behavior remains unchanged,
- MMIO access succeeds,
- unsupported Mach-O profiles are still rejected as before.

### TC-V13-LOAD-004 — Loader rejects segments overlapping device ranges

Create a generated ELF or Mach-O fixture whose loadable segment overlaps
`0x09000000`.

Expected:

- loader rejects the program before execution,
- diagnostic mentions overlap with reserved device range or invalid mapping,
- no RAM bytes or device state are modified.

### TC-V13-LOAD-005 — Entry point inside device range is rejected

Create a fixture with entry point set to a device address.

Expected:

- loader or first fetch rejects execution,
- error is deterministic,
- device range is not treated as executable code.

### TC-V13-LOAD-006 — Program data may contain device addresses as constants

Load an ELF/Mach-O fixture with `0x09000000` stored in data, then use it as a
pointer.

Expected:

- loader does not reject mere constants,
- access routes to device at runtime,
- program output is correct.

## CLI Behavior

### TC-V13-CLI-001 — `run` prints UART output

Run a UART hello fixture.

Expected:

- stdout contains guest UART output,
- emulator diagnostics do not pollute stdout on success,
- exit status is success.

### TC-V13-CLI-002 — `trace` preserves UART output and trace output policy

Trace a UART fixture.

Expected:

- instruction trace remains readable,
- UART output appears according to documented stdout/stderr policy,
- tests can distinguish trace lines from guest bytes.

### TC-V13-CLI-003 — `regs` can execute MMIO program

Run `regs` on a fixture that reads timer/random values into registers.

Expected:

- final register dump includes expected values,
- device output behavior follows documented `regs` policy,
- no unexpected trace noise appears.

### TC-V13-CLI-004 — `dump` device address policy is stable

Run `dump <program> 0x09000000 4`.

Expected, depending on documented policy:

- either `dump` rejects device ranges as not ordinary memory,
- or it performs side-effect-free inspector reads for readable registers,
- write-only registers are not accidentally consumed as output.

### TC-V13-CLI-005 — `info` shows device map

Run `info` on a program.

Expected:

- output includes RAM mappings and device ranges,
- UART/timer/random names and ranges are visible,
- permissions/access labels are learner-readable.

### TC-V13-CLI-006 — Invalid device access has stable failure status

Run a fixture that writes to an invalid device offset.

Expected:

- CLI exits with the documented emulator-failure status,
- stdout contains no partial UART output unless side effect happened before the
  faulting instruction,
- stderr contains stable diagnostic substrings.

### TC-V13-CLI-007 — Help text mentions device platform if public

Run:

```sh
./emulator help
```

Expected:

- if v1.3 exposes device behavior publicly, help or README points to the memory
  map,
- stale text does not say fake syscalls are the only output mechanism.

## Debugger Behavior

### TC-V13-DBG-001 — Debugger can step over UART write

Run a debugger script that steps to a UART store and steps once.

Expected:

- one byte appears in captured output,
- `pc` advances correctly,
- debugger remains interactive/scriptable after the step.

### TC-V13-DBG-002 — Debugger `continue` preserves UART output

Set a breakpoint after several UART writes, then continue.

Expected:

- output up to breakpoint is exactly expected,
- debugger stops at breakpoint,
- device side effects are not skipped during continue.

### TC-V13-DBG-003 — Debugger `maps` lists devices

Run debugger command:

```text
maps
```

Expected:

- RAM mappings and device ranges are shown,
- device entries are labeled distinctly from RAM when documented,
- output includes UART, timer, and random device names.

### TC-V13-DBG-004 — Debugger `map <device-address>` identifies device

Run debugger command:

```text
map 0x09000000
```

Expected:

- command reports UART device range,
- output includes valid access summary or register hints,
- address parsing accepts hex and decimal according to existing debugger policy.

### TC-V13-DBG-005 — Debugger `mem` device policy is stable

Run debugger memory inspection against a device address.

Expected, depending on documented policy:

- either debugger rejects device memory inspection,
- or it performs documented side-effect-free reads,
- it never emits UART output simply because the user inspected a write-only data
  register.

### TC-V13-DBG-006 — Debugger reports invalid device access fault

Step a program into an invalid device offset.

Expected:

- debugger shows a deterministic fault,
- fault includes `pc` and device offset,
- subsequent commands behave according to existing stopped-after-error policy.

### TC-V13-DBG-007 — Breakpoints at device addresses are rejected or harmless

Attempt to set a breakpoint at `0x09000000`.

Expected:

- debugger rejects non-executable device address, or accepts but never hits it
  according to documented breakpoint policy,
- no device side effect occurs from setting/listing/deleting the breakpoint.

### TC-V13-DBG-008 — Trace mode inside debugger includes MMIO instruction

Enable debugger trace and step through an MMIO access.

Expected:

- trace line shows `pc`, raw opcode, and decoded instruction,
- device output occurs once,
- trace formatting remains stable.

## Fault Reporting and Atomicity

### TC-V13-FAULT-001 — Invalid device offset fault category exists

Unit-test or CLI-test an invalid offset.

Expected:

- fault category distinguishes invalid device offset from ordinary unmapped RAM,
- message is clear for learners,
- tests pin public substrings or enum values.

### TC-V13-FAULT-002 — Unsupported device width fault category exists

Perform an unsupported-width access.

Expected:

- fault category or message identifies unsupported width,
- width value is included,
- no device state changes.

### TC-V13-FAULT-003 — Device read/write permission faults are distinct

Read a write-only register or write a read-only register.

Expected:

- fault distinguishes read vs write permission problem,
- device name and offset are included,
- no unrelated RAM permission fault is reported.

### TC-V13-FAULT-004 — Cross-target access faults before side effects

Attempt one access that would span valid UART data and unmapped address space.

Expected:

- no byte is emitted,
- no RAM changes,
- fault identifies cross-boundary or invalid range.

### TC-V13-FAULT-005 — Faulting MMIO instruction does not advance hidden device state

Trigger invalid random/timer access and inspect state through a valid read in a
unit test or fresh controlled program.

Expected:

- PRNG/timer state is unchanged unless docs state otherwise,
- valid subsequent read returns expected value.

### TC-V13-FAULT-006 — Fault diagnostics do not leak host-specific text

Run all invalid-device fixtures on different shells/environments.

Expected:

- messages do not include host-specific errno strings except where intentionally
  documented,
- line endings are stable,
- golden tests remain portable.

## Documentation and Lesson Tests

### TC-V13-DOC-001 — v1.3 test plan exists and is linked

Expected:

- `docs/test-plan-v1.3.md` exists,
- README links the v1.3 test plan when v1.3 work begins,
- previous test-plan links remain intact.

### TC-V13-DOC-002 — v1.3 lesson exists

Expected:

- `lessons/v1.3-memory-mapped-devices.md` exists when implementation lands,
- lesson explains RAM vs device routing,
- lesson states that this is not a real SoC.

### TC-V13-DOC-003 — Device memory map is documented

Expected:

- UART, timer, and random base addresses are documented,
- register offsets and access policies are documented,
- examples use the same addresses as implementation/tests.

### TC-V13-DOC-004 — README current status is not stale

Expected after implementation lands:

- README says v1.3 device profile is implemented/tested,
- README no longer describes memory-mapped devices solely as future work,
- limitations still list interrupts, DMA, real timers, and production SoC behavior
  as out of scope.

### TC-V13-DOC-005 — Example README explains how to run fixtures

Expected:

- `examples/v1_3/README.md` explains generating/running device examples,
- examples do not require external cross-compilers for deterministic tests,
- expected stdout/stderr behavior is documented.

### TC-V13-DOC-006 — Docs avoid overclaiming hardware accuracy

Review docs and lessons.

Expected:

- no text claims real PL011, real ARM timer, real hardware RNG, interrupts, or
  kernel driver support unless actually implemented,
- docs frame devices as teaching peripherals,
- future v1.4 toy-kernel work remains future work.

## Release and Packaging

### TC-V13-REL-001 — Release check includes v1.3

Run:

```sh
EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN=1 make release-check
```

Expected:

- docs checks include v1.3 docs,
- hygiene checks include v1.3 generated artifacts,
- clean checks remove v1.3 outputs,
- fresh archive check runs v1.3 deterministic tests.

### TC-V13-REL-002 — Fresh archive includes device source but not generated junk

Create a release archive from `HEAD` and inspect it.

Expected:

- v1.3 docs/tests/fixture writers are present,
- generated binaries/logs/temp debugger files are absent unless intentionally
  versioned,
- `.git` inclusion follows the existing project packaging policy.

### TC-V13-REL-003 — Fresh archive test passes after extraction

Extract the release archive into a new directory and run:

```sh
make test
```

Expected:

- v1.3 tests pass in the extracted copy,
- no test depends on untracked files from the original checkout,
- device fixture generation works from a clean tree.

### TC-V13-REL-004 — Sanitizer checks pass or skip clearly

Run:

```sh
make test-asan
make test-ubsan
```

Expected:

- sanitizer builds pass when toolchain supports them,
- unsupported toolchains skip clearly,
- device callbacks and route arithmetic have no sanitizer findings.

### TC-V13-REL-005 — Compiler matrix remains healthy

Run:

```sh
make test-cc-matrix
```

Expected:

- available compilers pass,
- unavailable compilers skip clearly,
- no device implementation relies on non-standard compiler extensions unless
  guarded and documented.

### TC-V13-REL-006 — Scope discipline is preserved

Review docs, tests, examples, and public behavior.

Expected:

- v1.3 does not claim interrupts, real timers, DMA, page tables, or kernel mode,
- device behavior is deterministic,
- future v1.4 toy-kernel roadmap items remain future work.

## Minimum Acceptance Checklist

v1.3 is ready when all of the following are true:

- `make test` passes from a clean checkout.
- UART MMIO can print a string without using fake syscalls.
- Timer and random devices have deterministic tested behavior.
- Invalid device offsets, widths, permissions, unaligned accesses, cross-boundary
  accesses, and address-overflow cases are tested.
- RAM/page-permission behavior from v1.2 remains intact.
- CLI `info` and debugger mapping commands expose device ranges clearly.
- Docs and lessons explain the memory map and limitations.
- Release archive validation includes v1.3 docs/tests and excludes generated junk.

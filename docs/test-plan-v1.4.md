# v1.4 Test Plan — Exceptions, Traps, and Interrupt Skeleton

## Version Goal

v1.4 teaches the next platform milestone after v1.3 memory-mapped devices:
**controlled exceptional control flow**.

Before v1.4, the emulator can run raw binaries, simple ELF64 executables, the
supported Mach-O teaching profile, page-protected guest memory, fake syscalls,
debugger sessions, and deterministic memory-mapped UART/timer/random devices.
Faults are already reported, but most of them remain terminal runtime errors.

v1.4 should introduce a small, deterministic teaching model for exceptions and a
minimal interrupt skeleton:

```text
normal execution -> synchronous exception, trap, or pending interrupt -> vector entry -> handler -> return or halt
```

The v1.4 promise is:

```text
raise exception -> capture cause/context -> jump to a vector -> run a tiny handler -> return safely
```

v1.4 is still **not** a production ARM exception-level model. It should not
attempt full `EL0`/`EL1`/`EL2`/`EL3`, system register completeness, GIC modeling,
real asynchronous host signals, preemption, real kernel scheduling, virtualized
interrupt controllers, or architecture-accurate exception syndrome coverage.
The goal is a deterministic learning bridge from emulator runtime faults toward
toy kernel experiments.

## Scope

### In Scope

- v0.1 through v1.3 regression behavior must continue to pass unless a behavior
  is intentionally tightened and documented by the v1.4 exception model.
- Add a small exception controller/state block to the emulator runtime.
- Define a stable set of teaching exception causes.
- Convert selected terminal faults into catchable synchronous exceptions when an
  exception vector is configured.
- Keep unrecoverable emulator bugs as terminal host-side errors.
- Add an instruction or control mechanism for configuring a vector base.
- Add a deterministic exception-frame/context model that preserves enough state
  for handlers to inspect cause, fault address, interrupted `pc`, and selected
  flags/registers.
- Add a deterministic exception-return mechanism.
- Support synchronous exceptions for:
  - invalid/unsupported instruction,
  - explicit trap/break instruction if implemented,
  - memory read/write/fetch faults,
  - device access faults,
  - syscall/trap policy faults,
  - divide-by-zero if the existing instruction behavior is tightened.
- Add a minimal pending-interrupt skeleton for teaching purposes, most likely
  driven by the deterministic v1.3 timer device.
- Define when pending interrupts are sampled.
- Define interrupt mask/enable policy.
- Keep interrupts deterministic and test-controlled.
- Preserve debugger usability across exception entry and return.
- Preserve trace readability across exception entry and return.
- Add CLI, debugger, unit, docs, fixture-generation, regression, sanitizer, and
  release tests.
- Document the exception model, the difference between synchronous exceptions and
  pending interrupts, and the limitations of the teaching profile.

### Out of Scope

- Full ARM exception levels or privilege separation.
- Full AArch64 system-register behavior.
- `VBAR_ELx`, `ESR_ELx`, `ELR_ELx`, `SPSR_ELx`, `DAIF`, `SPSel`, `SP_ELx`, or
  `ERET` exact architectural semantics unless explicitly implemented as teaching
  aliases.
- Real interrupt controller/GIC behavior.
- Real asynchronous host signal delivery.
- Host thread preemption or timing-based interrupts.
- DMA, cache maintenance, memory barriers, atomics, exclusive monitors, or memory
  ordering models.
- Nested exception levels beyond a simple documented nesting policy.
- User/kernel memory access restrictions.
- Dynamic linking, process isolation, or multitasking.
- A complete OS ABI.
- Full ARM syndrome encoding accuracy.
- Making invalid guest programs crash the host process.

## Implementation Assumptions

These assumptions should become explicit implementation decisions before tests are
finalized.

1. The emulator remains single-threaded and deterministic.
2. The guest address type remains 64-bit.
3. v1.4 exception handling is opt-in for catchable exceptions: if no vector is
   configured, existing fatal error behavior remains stable and user-readable.
4. The exception vector base is aligned and must point to executable mapped RAM.
5. Device ranges are never valid exception-vector targets.
6. Exception entry stores the interrupted `pc` before modifying `pc`.
7. The exception return address policy is documented:
   - faulting instruction re-executed after return, or
   - next instruction resumed after return, depending on cause.
8. Faulting memory/device operations are atomic: no partial state changes occur
   before exception entry.
9. Exception entry itself must be atomic: if the vector or frame storage is
   invalid, execution stops with a deterministic double-fault-style error.
10. The handler can inspect the cause and fault address through either reserved
    registers, memory-mapped exception registers, or a small context frame.
11. The chosen context format is stable and documented.
12. Existing `SVC #0` fake syscalls continue to work.
13. Non-zero `SVC` immediates either remain terminal errors or become catchable
    traps; the chosen policy must be tested.
14. Breakpoint/trap instructions for debugger use must not conflict with v0.5
    software breakpoints unless the behavior is intentionally redesigned.
15. Pending interrupts are sampled at a documented boundary, recommended:
    after an instruction retires and before fetching the next instruction.
16. Timer-driven interrupts are deterministic and based on instruction count or
    explicit timer-device state, not host wall-clock time.
17. Interrupts are disabled during exception entry unless nesting is explicitly
    supported.
18. Nested exceptions have a documented limit or are rejected deterministically.
19. Exception handling does not bypass v1.2 page permissions or v1.3 device
    access policy.
20. Debugger `step` reports exception entry as visible control flow.
21. Debugger `continue` can run through handled exceptions.
22. Trace output includes enough information to explain exception entry and
    return without overwhelming beginner output.
23. CLI exit codes for unhandled exceptions remain stable.
24. Release archives include v1.4 docs, fixtures, and source tests, but not
    generated binaries/logs.

## Proposed Teaching Exception Model

Exact names may differ, but tests should pin public behavior through API state,
fixture programs, CLI output, and debugger output.

Recommended exception causes:

```text
0x01  INVALID_INSTRUCTION
0x02  BREAKPOINT_OR_TRAP
0x03  SVC_TRAP
0x10  FETCH_FAULT
0x11  READ_FAULT
0x12  WRITE_FAULT
0x13  EXEC_PERMISSION_FAULT
0x14  READ_PERMISSION_FAULT
0x15  WRITE_PERMISSION_FAULT
0x20  DEVICE_FAULT
0x30  DIVIDE_BY_ZERO
0x40  TIMER_INTERRUPT
```

Recommended minimal exception context:

```text
cause              stable cause code
fault_address      address associated with memory/device/fetch fault, or 0
interrupted_pc     pc of instruction that caused the exception or was interrupted
resume_pc          documented return target
flags              saved NZCV flags
depth              nesting depth, if nesting is tracked
```

Recommended handler entry convention:

```text
pc = vector_base + cause_slot_offset
```

or, for a simpler first pass:

```text
pc = vector_base
handler reads cause and branches itself
```

The selected convention must be tested in both direct unit tests and runnable
fixtures.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.4.md
lessons/v1.4-exceptions-and-interrupts.md
examples/v1_4/README.md
examples/v1_4/generate_exception_fixtures.py
tests/fixtures/exception_fixture_writer.py
tests/v1_4/test_v1_4.c
tests/v1_4/test_cli_exceptions.sh
tests/v1_4/test_debugger_exceptions.sh
tests/v1_4/test_docs_exceptions.sh
```

Optional supporting fixtures:

```text
examples/v1_4/handled_invalid_instruction.s
examples/v1_4/handled_memory_fault.s
examples/v1_4/handled_device_fault.s
examples/v1_4/timer_interrupt_once.s
examples/v1_4/timer_interrupt_masked.s
examples/v1_4/unhandled_fault.s
examples/v1_4/nested_exception_limit.s
examples/v1_4/vector_table.txt
```

The implementation may choose different filenames, but the test suite should keep
v1.4 exception tests separate from v1.3 device tests so the control-flow
milestone remains clear.

## Test Data Strategy

v1.4 tests should use deterministic fixtures only.

1. **Raw binaries** for direct instruction, memory, and device exception paths.
2. **Generated ELF fixtures** for vector-base, mapped-segment, and stack/context
   coverage.
3. **Generated Mach-O fixtures** only if they add loader-specific value.
4. **Debugger scripts** that step into exception handlers, inspect context, and
   continue after returns.
5. **Malformed fixture files** for vector targets, invalid mappings, and invalid
   entries.
6. **Golden output files** only for stable CLI/debugger user-visible text.

Generated binaries should not be committed unless the project intentionally keeps
binary fixtures. Fixture generators should be source-controlled and deterministic.

## Test Case Matrix

### A. Regression Gate

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-REG-001 | v0.1 instruction sandbox still passes | Run v0.1 unit and CLI tests. | `NOP`, `HLT`, `MOVZ`, `ADD`, and `SUB` behavior is unchanged. |
| TC-V14-REG-002 | v0.2 branch behavior still passes | Run v0.2 tests and branch examples. | Branch, loop, condition flag, and instruction-limit behavior is unchanged. |
| TC-V14-REG-003 | v0.3 memory/stack behavior still passes | Run v0.3 tests. | Normal RAM load/store and stack fixtures behave as before. |
| TC-V14-REG-004 | v0.4 function behavior still passes | Run v0.4 tests. | `BL`, `RET`, nested calls, and invalid-return diagnostics remain stable. |
| TC-V14-REG-005 | v0.5 debugger behavior still passes | Run v0.5 debugger scripts. | Existing breakpoints, stepping, registers, and memory commands remain stable. |
| TC-V14-REG-006 | v0.6 trace/disassembly behavior still passes | Run v0.6 tests. | Normal trace output remains readable and deterministic. |
| TC-V14-REG-007 | v0.7 fake syscall behavior still passes | Run v0.7 tests. | `write`, `exit`, invalid fd, unknown syscall, and invalid buffers remain stable. |
| TC-V14-REG-008 | v0.8 ELF loader behavior still passes | Run v0.8 tests. | Supported ELF files load; invalid files are rejected as before. |
| TC-V14-REG-009 | v0.9 tiny C behavior still passes | Run v0.9 mandatory tests. | Freestanding C examples run or fail exactly as documented. |
| TC-V14-REG-010 | v1.0 release checks still pass | Run release-docs, hygiene, clean, and archive checks. | Release gate remains deterministic. |
| TC-V14-REG-011 | v1.1 Mach-O behavior still passes | Run v1.1 tests. | Mach-O teaching profile remains unchanged. |
| TC-V14-REG-012 | v1.2 virtual memory behavior still passes | Run v1.2 tests. | Page permissions, stack guard, maps, and dump behavior remain stable. |
| TC-V14-REG-013 | v1.3 devices still pass | Run v1.3 tests. | UART, timer, random, device faults, and debugger maps remain stable. |

### B. Exception Configuration

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-CFG-001 | Default exception state is disabled or documented | Initialize emulator and inspect exception state. | Vector base, masks, pending flags, and context are deterministic. |
| TC-V14-CFG-002 | Valid vector base can be configured | Configure vector base to an aligned executable RAM address. | Configuration succeeds and is visible through unit/API inspection. |
| TC-V14-CFG-003 | Unaligned vector base is rejected | Configure vector base to an unaligned address. | Configuration fails with stable diagnostic and no partial state change. |
| TC-V14-CFG-004 | Unmapped vector base is rejected or double-faults on use | Configure or use an unmapped vector. | Behavior follows documented policy and never crashes host. |
| TC-V14-CFG-005 | Non-executable vector base is rejected | Point vector base at readable/writable non-executable page. | Exception entry is refused or double-faults deterministically. |
| TC-V14-CFG-006 | Device vector base is rejected | Point vector base at UART/timer/random range. | Device address is not accepted as executable handler memory. |
| TC-V14-CFG-007 | Vector base outside 64-bit range arithmetic is safe | Use near-overflow vector plus cause offset. | Overflow is detected and reported deterministically. |
| TC-V14-CFG-008 | Reset/reload clears exception state | Configure exception state, reload program, inspect state. | State resets according to documented policy. |

### C. Exception Context and Entry

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-CTX-001 | Exception entry captures cause | Trigger a known invalid instruction. | Cause code equals documented invalid-instruction cause. |
| TC-V14-CTX-002 | Exception entry captures interrupted `pc` | Trigger fault at known instruction address. | Context contains the faulting/interrupted `pc`. |
| TC-V14-CTX-003 | Exception entry captures fault address | Trigger read fault from known bad address. | Context contains the bad data address. |
| TC-V14-CTX-004 | Exception entry captures flags | Set NZCV, trigger trap, inspect handler context. | Saved flags match pre-exception flags. |
| TC-V14-CTX-005 | Exception entry preserves general registers | Fill registers, trigger handled exception. | Handler-visible registers match documented preservation policy. |
| TC-V14-CTX-006 | Exception entry sets `pc` to correct vector | Trigger exception with configured vector. | Next executed instruction is at expected vector address. |
| TC-V14-CTX-007 | Exception entry does not partially execute faulting op | Faulting store to RAM/device is handled. | No RAM/device side effect occurred before handler entry. |
| TC-V14-CTX-008 | Exception entry increments depth if nesting tracked | Trigger exception inside handler. | Depth is reported or nested exception is rejected per policy. |
| TC-V14-CTX-009 | Exception context is stable after handler modifies registers | Handler changes scratch registers. | Saved context remains readable until documented clear point. |
| TC-V14-CTX-010 | Context clear/reset policy is deterministic | Return from exception, inspect context. | Context remains or clears exactly as documented. |

### D. Exception Return

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-RET-001 | Handler can return to next instruction for trap-like cause | Trigger explicit trap with handler returning. | Execution resumes at documented resume address. |
| TC-V14-RET-002 | Handler can retry faulting instruction when documented | Handler fixes memory mapping or register then returns. | Faulting instruction re-executes and succeeds, if retry policy exists. |
| TC-V14-RET-003 | Handler can skip faulting instruction when documented | Handler advances resume address. | Program continues after the faulting instruction. |
| TC-V14-RET-004 | Return without active exception is rejected | Execute exception-return instruction in normal code. | Program faults or reports stable misuse diagnostic. |
| TC-V14-RET-005 | Return to unaligned address is rejected | Handler sets unaligned resume target. | Deterministic fault; no host crash. |
| TC-V14-RET-006 | Return to unmapped address is rejected | Handler sets resume target to unmapped address. | Fetch fault or double-fault policy is applied. |
| TC-V14-RET-007 | Return to device address is rejected | Handler sets resume target to UART address. | Device ranges remain non-executable. |
| TC-V14-RET-008 | Return restores interrupt mask if saved | Mask interrupts, take exception, return. | Mask state follows documented save/restore policy. |
| TC-V14-RET-009 | Return restores flags if documented | Handler changes flags, returns. | Flags are restored or intentionally preserved per policy. |
| TC-V14-RET-010 | Multiple handled exceptions can return sequentially | Program triggers two handled exceptions. | Both are handled and program completes deterministically. |

### E. Synchronous Exception Causes

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-SYNC-001 | Invalid instruction can be handled | Run fixture containing unsupported opcode. | Handler receives invalid-instruction cause. |
| TC-V14-SYNC-002 | Explicit trap/break can be handled | Run fixture with chosen trap instruction. | Handler receives trap cause without confusing debugger breakpoint state. |
| TC-V14-SYNC-003 | Non-zero `SVC` policy is stable | Run `SVC #1` fixture. | Either catchable SVC trap or existing terminal error occurs as documented. |
| TC-V14-SYNC-004 | Unknown syscall remains stable | Run v0.7 unknown syscall fixture. | Existing fake `-ENOSYS` behavior remains unless explicitly redesigned. |
| TC-V14-SYNC-005 | Fetch from unmapped page can be handled | Branch to unmapped executable address with vector configured. | Handler receives fetch-fault cause and fault address. |
| TC-V14-SYNC-006 | Fetch from non-executable page can be handled | Branch to mapped `rw-` address. | Handler receives execute-permission cause. |
| TC-V14-SYNC-007 | Fetch from device range can be handled | Branch to UART base with vector configured. | Handler receives fetch/device execute fault according to policy. |
| TC-V14-SYNC-008 | Read from unmapped page can be handled | `LDR` from unmapped address. | Handler receives read-fault cause. |
| TC-V14-SYNC-009 | Read from write-only/invalid device register can be handled | `LDR` from invalid UART write-only register if policy says write-only. | Handler receives device/read fault with no state change. |
| TC-V14-SYNC-010 | Write to read-only page can be handled | `STR` to `r--` mapping. | Handler receives write-permission cause. |
| TC-V14-SYNC-011 | Write to read-only/invalid device register can be handled | `STR` to invalid timer/random register. | Handler receives device/write fault with no state change. |
| TC-V14-SYNC-012 | Unaligned memory access policy is catchable if applicable | Execute unaligned `LDR`/`STR`. | Cause and fault address are stable. |
| TC-V14-SYNC-013 | Address arithmetic overflow is catchable or terminal as documented | Use address near `UINT64_MAX` plus width. | Overflow does not wrap silently. |
| TC-V14-SYNC-014 | Divide by zero behavior is stable | Execute `UDIV`/`SDIV` by zero fixture. | Catchable divide exception or documented architectural result is stable. |
| TC-V14-SYNC-015 | Pair load/store fault is atomic | Fault one half of `LDP`/`STP`. | No partial register/memory update occurs before exception. |

### F. Interrupt Skeleton

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-IRQ-001 | Interrupts disabled by default or documented | Run timer fixture without enabling interrupts. | No surprise handler entry occurs. |
| TC-V14-IRQ-002 | Interrupt enable bit allows pending interrupt entry | Enable interrupts and arm deterministic timer. | Handler runs at deterministic instruction boundary. |
| TC-V14-IRQ-003 | Interrupt mask suppresses pending interrupt | Arm timer while interrupts are masked. | Pending bit remains or clears exactly as documented; handler does not run. |
| TC-V14-IRQ-004 | Pending interrupt is delivered after unmask | Mask, arm timer, unmask. | Handler entry occurs at documented sampling point. |
| TC-V14-IRQ-005 | Interrupt sampling point is stable | Use fixture with visible register updates around sampling boundary. | Handler sees expected pre/post-instruction state. |
| TC-V14-IRQ-006 | Timer interrupt is deterministic across runs | Run same fixture twice. | Handler count and final registers match exactly. |
| TC-V14-IRQ-007 | Timer interrupt does not use host wall-clock time | Run fixture under slow/fast host conditions if possible. | Guest-visible behavior depends only on emulator state. |
| TC-V14-IRQ-008 | Interrupt during handler obeys nesting policy | Arm another interrupt inside handler. | Nested interrupt is masked, queued, or rejected according to policy. |
| TC-V14-IRQ-009 | Interrupt return resumes interrupted code | Timer fires in a loop. | Program resumes and finishes with expected registers/output. |
| TC-V14-IRQ-010 | Pending interrupt is cleared exactly once | Handler returns; continue execution. | Same interrupt is not repeatedly delivered unless re-armed. |
| TC-V14-IRQ-011 | Interrupt does not occur after halt | Arm timer immediately before `HLT`. | No device/interrupt side effect occurs after halted state. |
| TC-V14-IRQ-012 | Interrupt at instruction limit is deterministic | Arm timer near instruction-limit boundary. | Either interrupt or limit error wins by documented priority. |

### G. Loader, Memory, and Mapping Interaction

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-LOAD-001 | Raw binary can install/use vector | Run raw fixture with vector handler in program memory. | Handled exception completes. |
| TC-V14-LOAD-002 | ELF fixture can install/use vector | Run ELF with handler segment and faulting code. | Entry, handler, and mapped context work. |
| TC-V14-LOAD-003 | Mach-O fixture can install/use vector if included | Run optional Mach-O exception fixture. | Loader profile remains compatible or test is skipped clearly. |
| TC-V14-LOAD-004 | Vector in `.bss`/zero-filled memory requires executable mapping | ELF places vector in zero-fill region. | Non-executable vector rejected unless segment permissions allow execute. |
| TC-V14-LOAD-005 | Stack guard fault can be handled or remains terminal by policy | Fault into guard page with vector configured. | Behavior is stable and documented. |
| TC-V14-LOAD-006 | Handler stack use obeys page permissions | Handler pushes/pops frame. | Stack mapping permissions are respected. |
| TC-V14-LOAD-007 | Handler cannot execute from writable-only RAM | Point vector at `rw-` mapping. | Execute-permission policy is enforced. |
| TC-V14-LOAD-008 | Loader rejects segments overlapping exception device/register space | Generate invalid ELF/Mach-O overlap if new reserved region exists. | Loader reports stable error. |
| TC-V14-LOAD-009 | Program constants may contain vector/device addresses | Load fixture containing addresses as data only. | Loader accepts constants; execution policy applies only when used. |
| TC-V14-LOAD-010 | Reloading program clears pending interrupts | Arm interrupt, reload/run again. | Pending state does not leak across loads unless documented. |

### H. Device and Syscall Interaction

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-DEV-001 | UART output before exception is preserved | Program writes UART, triggers handled exception, writes UART again. | Output order is deterministic. |
| TC-V14-DEV-002 | Faulting UART write produces no partial byte | Use invalid/crossing UART write. | No byte is emitted before handler entry. |
| TC-V14-DEV-003 | Timer interrupt and timer MMIO reads are coherent | Handler reads timer registers. | Values match documented timer/interrupt policy. |
| TC-V14-DEV-004 | Random device state is unaffected by unrelated exception | Read random, trigger memory exception, read random. | PRNG sequence advances only on documented random reads/writes. |
| TC-V14-DEV-005 | Device fault handler can recover by changing address | Handler fixes register/address then returns. | Program can continue if recovery is in scope. |
| TC-V14-DEV-006 | Fake syscall output around exception remains ordered | Program uses `SVC write`, handled exception, UART write. | Output ordering is stable. |
| TC-V14-DEV-007 | `exit` syscall inside handler is handled consistently | Handler invokes fake exit. | Program terminates with expected low 8-bit status. |
| TC-V14-DEV-008 | Unknown syscall inside handler follows policy | Handler invokes unknown syscall. | Fake `-ENOSYS` or nested exception behavior is documented. |

### I. Debugger Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-DBG-001 | `step` can enter exception handler | Script: step faulting instruction. | Debugger shows `pc` at handler and exposes cause somehow. |
| TC-V14-DBG-002 | `step` can return from exception handler | Script through handler return. | Debugger shows resumed `pc` correctly. |
| TC-V14-DBG-003 | `continue` runs through handled exception | Set breakpoint after handler and continue. | Breakpoint is hit after recovery. |
| TC-V14-DBG-004 | Breakpoints inside handler work | Set breakpoint at vector address. | Debugger stops at handler before executing it. |
| TC-V14-DBG-005 | Breakpoint at faulting instruction remains usable | Break at instruction that will fault, continue, step. | Breakpoint and exception entry do not conflict. |
| TC-V14-DBG-006 | `regs` displays useful post-exception state | Stop inside handler and run `regs`. | `pc`, general registers, flags, and documented context are readable. |
| TC-V14-DBG-007 | `mem` can inspect context frame if memory-backed | Stop inside handler and inspect frame. | Context bytes match documented layout. |
| TC-V14-DBG-008 | `maps` includes any exception-context mapping if public | Run `maps`. | Output is stable and learner-readable. |
| TC-V14-DBG-009 | `trace on` in debugger shows exception entry | Enable trace and continue through exception. | Trace includes faulting instruction, exception entry, handler, return. |
| TC-V14-DBG-010 | Debugger handles unhandled exception | Continue into unhandled fault. | Session reports stable error and remains controllable or exits cleanly. |
| TC-V14-DBG-011 | Debugger handles pending interrupt | Step through timer interrupt fixture. | Interrupt entry appears at documented boundary. |
| TC-V14-DBG-012 | Invalid debugger commands still reject extra args | Run old parser edge cases. | v0.5 command strictness remains. |

### J. CLI Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-CLI-001 | `run` completes handled exception fixture | Run handled invalid-instruction fixture. | CLI exits success or documented guest exit status. |
| TC-V14-CLI-002 | `run` reports unhandled exception | Run unhandled memory fault fixture. | CLI exits non-zero with stable diagnostic. |
| TC-V14-CLI-003 | `trace` shows exception entry and return | Trace handled exception fixture. | Trace is readable and deterministic. |
| TC-V14-CLI-004 | `regs` after handled exception is stable | Run `regs` on handled fixture. | Final registers match golden expectations. |
| TC-V14-CLI-005 | `dump` remains RAM-only unless documented otherwise | Dump vector/context/device addresses. | Output follows v1.2/v1.3 memory policy. |
| TC-V14-CLI-006 | `info` mentions exception support if public | Run `info` on fixture. | Output includes stable exception/vector details or omits by documented policy. |
| TC-V14-CLI-007 | Help text mentions new public commands/options | Run `help`, `--help`, `-h`. | New surface is documented without stale wording. |
| TC-V14-CLI-008 | Exit code for handled guest exit remains low 8 bits | Handler eventually calls fake exit with known value. | Host exit status matches fake syscall policy. |
| TC-V14-CLI-009 | Exit code for emulator-side double fault is stable | Misconfigure vector to cause entry failure. | Host exits non-zero with stable double-fault diagnostic. |
| TC-V14-CLI-010 | Output ordering between trace, UART, and diagnostics is stable | Run mixed fixture under `trace`. | stdout/stderr expectations are deterministic. |

### K. Fault Priority and Edge Cases

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-EDGE-001 | Fetch fault priority over pending interrupt is documented | Pending interrupt and bad next fetch happen together. | Winning event matches priority policy. |
| TC-V14-EDGE-002 | Instruction limit priority over pending interrupt is documented | Limit reached with pending interrupt. | Winning event matches priority policy. |
| TC-V14-EDGE-003 | Breakpoint priority over interrupt is documented | Breakpoint and pending interrupt at same boundary. | Debugger stop vs interrupt entry follows policy. |
| TC-V14-EDGE-004 | Breakpoint priority over synchronous trap is documented | Break at trap instruction. | Debugger breakpoint and guest trap do not double-fire unexpectedly. |
| TC-V14-EDGE-005 | Exception inside exception handler follows nesting policy | Handler triggers invalid instruction. | Nested handling or deterministic nested-fault error occurs. |
| TC-V14-EDGE-006 | Maximum nesting depth is enforced | Recurse exceptions until limit. | Emulator stops with stable diagnostic before stack/host corruption. |
| TC-V14-EDGE-007 | Context frame write fault during entry is deterministic | Make context storage unwritable if memory-backed. | Double-fault-style error with no host crash. |
| TC-V14-EDGE-008 | Vector target decode fault is deterministic | Vector points to unsupported instruction. | Nested/second exception behavior follows policy. |
| TC-V14-EDGE-009 | Handler clobbers vector base | Handler changes vector base then triggers second exception. | New or old vector is used according to documented update timing. |
| TC-V14-EDGE-010 | Handler modifies mapping for interrupted code | Handler changes mapping if API supports it. | Return/fetch policy remains safe. |
| TC-V14-EDGE-011 | Faulting pre-index store does not update base register if policy says atomic | Fault pre-index MMIO/RAM store. | Base register update policy is stable. |
| TC-V14-EDGE-012 | Faulting post-index store does not update base register if policy says atomic | Fault post-index MMIO/RAM store. | Base register update policy is stable. |
| TC-V14-EDGE-013 | Faulting pair store updates no memory/registers | Fault `STP` across invalid boundary. | No partial write occurs. |
| TC-V14-EDGE-014 | Exception return to `HLT` behaves normally | Handler returns to halt instruction. | Program halts cleanly. |
| TC-V14-EDGE-015 | Exception return after guest exit is impossible | Handler tries to return after fake exit state. | Runtime prevents inconsistent execution state. |
| TC-V14-EDGE-016 | Zero-length helper paths are safe | Unit-test exception helper with zero-sized access if exposed. | No out-of-bounds or undefined behavior. |
| TC-V14-EDGE-017 | Address plus vector-slot overflow is safe | Use maximum vector base plus slot offset. | Overflow is rejected deterministically. |
| TC-V14-EDGE-018 | Diagnostic text does not include host-specific paths unexpectedly | Run fault fixtures from temp directory. | Golden diagnostics remain portable. |
| TC-V14-EDGE-019 | Repeated exceptions do not leak memory or state | Loop through many handled traps. | Final state deterministic; sanitizer checks pass. |
| TC-V14-EDGE-020 | Exception state survives trace/debug toggles | Toggle trace around exceptions. | Execution semantics are unchanged by logging. |

### L. Documentation and Examples

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-DOC-001 | v1.4 test plan exists and is linked | Check `docs/test-plan-v1.4.md` and README. | Test plan is reachable from README. |
| TC-V14-DOC-002 | v1.4 lesson exists and is linked | Check `lessons/v1.4-exceptions-and-interrupts.md` and README. | Lesson is reachable from README. |
| TC-V14-DOC-003 | Exception cause table is documented | Search docs for cause codes/names. | Public causes match implementation constants/tests. |
| TC-V14-DOC-004 | Vector configuration is documented | Search docs/examples. | Learner can reproduce a handled exception fixture. |
| TC-V14-DOC-005 | Exception return policy is documented | Search docs for resume/retry/skip semantics. | Policy matches tests. |
| TC-V14-DOC-006 | Interrupt limitations are documented | Search docs for out-of-scope claims. | Docs do not overclaim real hardware/GIC accuracy. |
| TC-V14-DOC-007 | Debugger exception behavior is documented | Search debugger docs/lesson. | Step/continue/trace behavior is explained. |
| TC-V14-DOC-008 | Example README explains fixture generation | Check `examples/v1_4/README.md`. | Commands are copy-pasteable and deterministic. |
| TC-V14-DOC-009 | README current implementation status is updated | Check README status section. | v1.4 status is not stale once implemented. |
| TC-V14-DOC-010 | Docs avoid unsupported architecture promises | Grep for claims like full EL1/GIC/kernel support. | Unsupported claims are absent or clearly scoped. |

### M. Build, Sanitizer, and Release

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V14-REL-001 | `make test` includes v1.4 tests | Run `make test`. | v1.4 unit/CLI/docs tests run or skip clearly if optional tooling is absent. |
| TC-V14-REL-002 | `make clean` removes v1.4 generated artifacts | Generate v1.4 fixtures, run clean. | Generated binaries/logs/temp files are removed. |
| TC-V14-REL-003 | `make release-check` includes v1.4 | Run release check from clean tree. | v1.4 docs/tests are covered. |
| TC-V14-REL-004 | Release archive includes source fixtures/tests/docs | Build archive and inspect file list. | v1.4 source files are present. |
| TC-V14-REL-005 | Release archive excludes generated junk | Build archive after running tests. | v1.4 binaries/logs/tmp artifacts are absent. |
| TC-V14-REL-006 | Fresh archive test passes after extraction | Extract release archive into temp dir, run release check. | Test suite passes from archived source. |
| TC-V14-REL-007 | ASan check passes or skips clearly | Run `make test-asan`. | No sanitizer findings, or tool absence is explicit. |
| TC-V14-REL-008 | UBSan check passes or skips clearly | Run `make test-ubsan`. | No undefined-behavior findings, or tool absence is explicit. |
| TC-V14-REL-009 | Compiler matrix remains healthy | Run `make test-cc-matrix`. | Supported compilers pass or skip clearly. |
| TC-V14-REL-010 | No generated fixture drift | Regenerate v1.4 fixtures twice. | Outputs are byte-for-byte stable or intentionally ignored. |

## Acceptance Criteria

v1.4 is accepted when all of the following are true:

1. Existing v0.1 through v1.3 tests pass.
2. The exception configuration surface is deterministic and documented.
3. At least one synchronous exception can be caught by a guest handler and
   returned from safely.
4. Invalid instruction, memory fault, and device fault behavior are tested in
   handled and unhandled modes.
5. Exception context captures cause, interrupted `pc`, and fault address where
   applicable.
6. Exception return behavior is pinned by tests.
7. Timer/pending interrupt skeleton behavior is deterministic, even if minimal.
8. Debugger `step`, `continue`, breakpoints, registers, and trace remain usable
   around exception entry and return.
9. CLI `run`, `trace`, `regs`, `dump`, `info`, and `help` behavior is updated or
   explicitly unchanged.
10. Nested exception and double-fault behavior is deterministic and cannot crash
    the host.
11. Faulting instructions do not partially update RAM, device state, base
    registers, or output before exception entry unless a documented exception is
    intentionally made.
12. Docs and examples explain the teaching model and explicitly avoid claiming
    full ARM exception/interrupt fidelity.
13. Release checks include v1.4 and archives remain clean/reproducible.

## Recommended Test Execution Order

1. Run formatter/static checks if available.
2. Run v1.4 unit tests.
3. Run v1.4 fixture generation tests.
4. Run v1.4 CLI tests.
5. Run v1.4 debugger tests.
6. Run v1.4 docs tests.
7. Run v0.1 through v1.3 regression tests.
8. Run sanitizer targets.
9. Run compiler matrix target.
10. Run release-check from a clean tree.
11. Build and inspect a fresh release archive.

## Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| Exception behavior becomes too architecture-specific too soon. | Keep the v1.4 model explicitly teaching-oriented and document out-of-scope ARM fidelity. |
| Faults become inconsistent between terminal and handled modes. | Share cause/context construction through one helper and test handled/unhandled pairs. |
| Exception entry introduces partial side effects. | Add atomicity tests for RAM, MMIO, pre/post-index, and pair operations. |
| Interrupt tests become flaky. | Drive interrupts only from deterministic timer/instruction-count state. |
| Debugger breakpoints conflict with guest traps. | Define and test priority between debugger stops and guest exception causes. |
| Context storage creates new memory-permission loopholes. | Route all context accesses through documented safe helpers and test permissions. |
| Docs overclaim real kernel readiness. | Add docs tests that check for scope/limitation language. |

## Open Decisions to Resolve Before Implementation

- How does guest code configure the vector base: API-only for tests, special
  instruction, fake syscall, MMIO control register, or loader metadata?
- Is exception context register-backed, memory-backed, or exposed through a small
  MMIO exception-controller range?
- Is exception return a new pseudo-instruction, a specific existing instruction
  pattern, or a fake syscall?
- Do memory/device faults resume by retrying the faulting instruction, skipping
  it, or using handler-written resume address?
- Which causes are catchable in v1.4, and which remain terminal emulator errors?
- Are nested exceptions supported, masked, or rejected?
- What is the priority order among breakpoint, synchronous exception, pending
  interrupt, instruction limit, and halt?
- Is the timer interrupt armed through the existing timer device or through a new
  exception-controller register?
- Should CLI `info` expose vector/exception state, or should that remain debugger
  and test-only for v1.4?
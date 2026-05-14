# v1.5 Test Plan — Toy Kernel Boot and Cooperative Tasks

## Version Goal

v1.5 should turn the v1.4 exception, trap, and deterministic interrupt skeleton
into a small **toy-kernel execution profile**.

Before v1.5, the emulator can execute raw binaries, supported ELF64 and Mach-O
teaching fixtures, enforce page permissions, expose deterministic MMIO devices,
and route selected traps/faults/timer interrupts through a simplified exception
vector. v1.5 should prove that those pieces can support a tiny kernel-shaped
program that boots, configures its runtime, runs multiple cooperative tasks, and
handles simple trap/timer/device interactions without losing determinism.

The v1.5 promise is:

```text
load toy kernel -> initialize vector/devices/stack -> run task A -> yield/trap -> run task B -> finish deterministically
```

v1.5 is still **not** a production operating system. It should not implement
real privilege levels, MMU address spaces, preemptive scheduling, a POSIX ABI,
thread safety, dynamic linking, or a real device tree. The goal is a testable
learning bridge from emulator features toward toy OS experiments.

## Scope

### In Scope

- v0.1 through v1.4 regression behavior must continue to pass.
- Define one documented toy-kernel profile that can be loaded and run from the
  CLI and from unit tests.
- Support a deterministic boot contract for toy kernels:
  - initial `pc`, `sp`, argument registers, and zeroed runtime state,
  - loader metadata exposed through a stable mechanism,
  - exception vector setup requirements,
  - supported memory layout.
- Add or validate a small kernel runtime convention for:
  - kernel entry,
  - panic/halt,
  - cooperative yield,
  - task exit,
  - optional timer tick handling,
  - simple console output through the existing UART/syscall path.
- Add a deterministic task model suitable for teaching:
  - fixed maximum number of tasks,
  - fixed-size task stacks,
  - saved/restored general registers needed for cooperative switching,
  - deterministic round-robin scheduling.
- Add tests for context save/restore correctness.
- Add tests for scheduler behavior when tasks yield, exit, fault, or run past
  instruction limits.
- Preserve debugger visibility across kernel boot, task switches, exception
  entry/return, and halt.
- Preserve trace readability without flooding output.
- Add CLI, unit, debugger, fixture, docs, sanitizer, and release tests.
- Document every public convention introduced by v1.5.

### Out of Scope

- Full ARM exception levels, privilege separation, or architectural `EL0/EL1`.
- Real page-table translation or per-process address spaces.
- Preemptive multitasking based on host wall-clock time.
- A full GIC, real asynchronous interrupts, or real host signal integration.
- Dynamic linking, hosted libc, POSIX process APIs, filesystems, networking, or
  system calls beyond the existing teaching runtime and new toy-kernel traps.
- SMP, threads, atomics, memory-ordering correctness, locks, or races.
- Device tree parsing unless v1.5 explicitly introduces a tiny static metadata
  block.
- Production crash dumps, symbolic stack unwinding, or source-level debugging.
- Running arbitrary third-party kernels.

## Implementation Assumptions

These assumptions must either become implementation decisions or be replaced by
explicit alternatives before tests are finalized.

1. The emulator remains single-threaded and deterministic.
2. The toy-kernel profile is opt-in and does not change normal raw, ELF, Mach-O,
   or existing example behavior.
3. The v1.5 kernel profile can be expressed as either a loader mode, a CLI flag,
   or a documented ELF/raw convention.
4. The initial kernel entry point is 4-byte aligned and executable.
5. The initial kernel stack is mapped, writable, aligned, and has a guard region
   if the v1.2 mapping model can represent it.
6. The kernel vector address is either configured by the host before entry or by
   guest code during early boot; both paths are tested if both are supported.
7. Task stacks are ordinary guest RAM and never overlap code, device ranges, or
   each other.
8. Task descriptors use a stable teaching layout if they are guest-visible.
9. Cooperative context switches occur only at documented yield/trap boundaries.
10. Timer interrupts remain deterministic and instruction-count based.
11. A timer tick may request scheduling, but v1.5 does not require full
    asynchronous preemption unless explicitly implemented.
12. Interrupts are masked or deferred while the scheduler saves/restores state.
13. A task fault is reported deterministically and either panics the kernel or is
    routed to a documented task-fault handler.
14. A fault in the scheduler or exception handler is a deterministic fatal
    double-fault-style error.
15. `HLT` remains a guest halt and must not silently mean task exit unless the
    v1.5 ABI documents that convention.
16. Existing fake syscalls keep their v0.7/v0.9 behavior outside the toy-kernel
    profile.
17. Debugger breakpoints are host debugging controls and do not corrupt guest
    task state.
18. Trace output includes kernel/task context only when requested or when an
    event changes control flow.
19. CLI exit codes for successful kernel completion, kernel panic, unhandled
    guest fault, invalid kernel image, and host usage errors are stable.
20. Generated fixtures are deterministic and source-controlled generators are
    preferred over checked-in binaries.

## Proposed v1.5 Teaching Model

The exact names may differ, but tests should pin the public behavior through the
C API, CLI output, debugger output, fixtures, and documentation.

### Kernel boot contract

Recommended initial state:

```text
pc       kernel entry point
sp       top of kernel stack
x0       optional boot-info pointer, or 0 if no boot-info block is used
x1       boot-info size, or 0
x2-x7    reserved, zero
x8+      zero unless loader metadata is explicitly defined
flags    N=0 Z=0 C=0 V=0
```

Recommended boot-info fields, if v1.5 adds a guest-visible boot-info block:

```text
magic
version
memory_base
memory_size
kernel_stack_base
kernel_stack_size
task_stack_base
task_stack_size
task_count
uart_base
timer_base
random_base
exception_controller_base
```

### Cooperative task contract

Recommended task states:

```text
EMPTY
READY
RUNNING
BLOCKED_OR_SLEEPING
EXITED
FAULTED
```

Recommended minimal saved context:

```text
pc
sp
x19-x30       callee-saved teaching set
flags         if the scheduler promises flag preservation
task_state
exit_code
```

The implementation may save all registers instead. Tests should cover the
documented guarantee, not an accidental internal layout.

### Toy-kernel traps

Recommended teaching trap calls:

```text
yield
task_exit(status)
panic(code)
console_write(buffer, length) or UART-only console output
```

If traps are encoded with `SVC` or `BRK`, tests must verify that existing v0.7
`SVC #0` fake syscall behavior remains unchanged outside the v1.5 profile.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.5.md
lessons/v1.5-toy-kernel-and-cooperative-tasks.md
examples/v1_5/README.md
examples/v1_5/generate_kernel_fixtures.py
tests/fixtures/kernel_fixture_writer.py
tests/v1_5/test_v1_5.c
tests/v1_5/test_cli_kernel.sh
tests/v1_5/test_debugger_kernel.sh
tests/v1_5/test_docs_kernel.sh
tests/v1_5/test_optional_kernel_examples.sh
```

Optional runnable examples:

```text
examples/v1_5/minimal_kernel.s
examples/v1_5/two_task_yield.s
examples/v1_5/task_exit_order.s
examples/v1_5/timer_tick_kernel.s
examples/v1_5/task_fault_panic.s
examples/v1_5/uart_console_kernel.s
examples/v1_5/stack_overflow_guard.s
examples/v1_5/bad_boot_info.s
```

## Test Data Strategy

1. Prefer tiny deterministic raw binaries for direct boot and scheduler paths.
2. Use generated ELF fixtures for loader-specific boot metadata and segment
   permission coverage.
3. Use generated malformed images for invalid boot contracts and boundary cases.
4. Use debugger scripts for task-switch and exception visibility checks.
5. Keep golden output only for stable CLI/debugger user-facing text.
6. Do not commit generated `.bin`, `.elf`, logs, or temporary files unless the
   project intentionally keeps binary fixtures.
7. Make every fixture generator reproducible with no network or host-specific
   paths.

## Test Case Matrix

### A. Regression Gate

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-REG-001 | v0.1 instruction sandbox still passes | Run v0.1 unit and CLI tests. | Base instruction behavior is unchanged. |
| TC-V15-REG-002 | v0.2 branch and loop behavior still passes | Run v0.2 tests and examples. | Branch targets, condition flags, loops, and instruction limits remain stable. |
| TC-V15-REG-003 | v0.3 memory and stack behavior still passes | Run v0.3 tests. | Ordinary RAM and stack load/store behavior is unchanged. |
| TC-V15-REG-004 | v0.4 function call behavior still passes | Run v0.4 tests. | `BL`, `RET`, `RET Xn`, and nested call behavior remains stable. |
| TC-V15-REG-005 | v0.5 debugger still passes | Run existing debugger scripts. | Breakpoints, stepping, memory inspection, and trace toggles behave as before. |
| TC-V15-REG-006 | v0.6 trace/disassembly still passes | Run v0.6 unit and CLI tests. | Instruction formatting and trace readability remain stable. |
| TC-V15-REG-007 | v0.7 fake syscalls still pass | Run v0.7 tests and examples. | `write`, `exit`, bad fd, and unknown syscall behavior remains stable. |
| TC-V15-REG-008 | v0.8 ELF loader still passes | Run v0.8 loader tests. | Supported ELF64 fixtures load and malformed ELF fixtures are rejected. |
| TC-V15-REG-009 | v0.9 freestanding C tests still pass | Run v0.9 tests and optional examples when toolchain exists. | Tiny C programs preserve expected exit/status/output behavior. |
| TC-V15-REG-010 | v1.0 release gate still passes | Run v1.0 release tests. | Help, docs, hygiene, archive checks remain stable. |
| TC-V15-REG-011 | v1.1 Mach-O loader still passes | Run v1.1 tests. | Supported Mach-O profile and rejection cases remain stable. |
| TC-V15-REG-012 | v1.2 virtual memory still passes | Run v1.2 tests. | Page permissions, stack mapping, and diagnostics remain stable. |
| TC-V15-REG-013 | v1.3 MMIO devices still pass | Run v1.3 tests. | UART, timer, random, and invalid-device behavior remains stable. |
| TC-V15-REG-014 | v1.4 exceptions/interrupts still pass | Run v1.4 tests. | Exception vector, context, `BRK`, `ERET`, timer interrupt, and debugger visibility remain stable. |
| TC-V15-REG-015 | Full release check includes v1.5 | Run the named release target. | All previous tests and v1.5 tests are included in the release gate. |

### B. Kernel Boot Contract

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-BOOT-001 | Minimal raw kernel boots and halts | Run a raw fixture with one valid entry and `HLT`. | CLI exits successfully, `pc` starts at entry, `sp` starts at kernel stack top. |
| TC-V15-BOOT-002 | Minimal ELF kernel boots at entry point | Run a generated ELF kernel fixture. | Loader uses ELF entry, maps segments, creates kernel stack, and halts successfully. |
| TC-V15-BOOT-003 | Boot registers are deterministic | In unit test, inspect CPU before first instruction. | Documented registers match boot contract; reserved registers are zero. |
| TC-V15-BOOT-004 | Boot-info pointer is valid when enabled | Run kernel that reads boot-info. | `x0` points to readable memory and fields contain documented values. |
| TC-V15-BOOT-005 | Boot-info can be absent if disabled | Run kernel profile without boot-info. | `x0 == 0`, `x1 == 0`, and kernel still runs if it does not require metadata. |
| TC-V15-BOOT-006 | Kernel stack is mapped writable | Kernel stores and loads through `sp`. | Store/load succeeds and final register proves round trip. |
| TC-V15-BOOT-007 | Kernel stack guard catches underflow | Kernel writes below stack mapping. | Deterministic write fault or kernel panic occurs; no host crash. |
| TC-V15-BOOT-008 | Misaligned entry is rejected | Generate fixture with unaligned entry. | Loader/runner rejects image with user-readable error. |
| TC-V15-BOOT-009 | Unmapped entry is rejected | Generate fixture with entry outside mapped executable region. | Loader/runner rejects image deterministically. |
| TC-V15-BOOT-010 | Non-executable entry is rejected | Generate fixture with entry in readable but non-executable mapping. | Exec-permission error is reported before guest execution. |
| TC-V15-BOOT-011 | Overlapping kernel stack and code rejected | Configure stack over code segment. | Runner rejects setup before execution. |
| TC-V15-BOOT-012 | Stack outside RAM rejected | Configure kernel stack outside guest RAM. | Runner rejects setup with stable diagnostic. |
| TC-V15-BOOT-013 | Device range is not valid stack | Configure stack inside MMIO range. | Runner rejects setup. |
| TC-V15-BOOT-014 | Invalid boot-info magic rejected by kernel fixture | Run kernel that validates bad magic. | Kernel panics with documented code or returns documented failure. |
| TC-V15-BOOT-015 | CLI usage validates kernel flags | Invoke kernel mode with missing or malformed args. | Usage error is clear and exit code is stable. |

### C. Exception Vector and Trap Integration

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-EXC-001 | Host-configured vector works during kernel boot | Run fixture with host vector option. | First trap enters configured vector and returns. |
| TC-V15-EXC-002 | Guest-configured vector works during kernel boot | Kernel writes vector through exception-controller MMIO. | Later trap enters guest-configured vector. |
| TC-V15-EXC-003 | Vector must be 4-byte aligned | Configure unaligned vector. | Configuration fails with stable error. |
| TC-V15-EXC-004 | Vector must be executable RAM | Configure vector in data page. | Configuration fails or trap produces deterministic fatal vector error. |
| TC-V15-EXC-005 | Vector cannot target device range | Configure vector at MMIO address. | Configuration fails. |
| TC-V15-EXC-006 | Yield trap routes through exception machinery | Task executes documented yield trap. | Context cause is trap/yield; scheduler resumes next task. |
| TC-V15-EXC-007 | Task exit trap records status | Task exits with nonzero status. | Task state becomes `EXITED` and status is preserved. |
| TC-V15-EXC-008 | Panic trap halts kernel | Kernel calls panic trap. | CLI reports kernel panic and stable exit code. |
| TC-V15-EXC-009 | Non-kernel `SVC #0` behavior unchanged | Run v0.7 or v0.9 syscall fixture. | Existing fake syscall behavior is unchanged. |
| TC-V15-EXC-010 | Nonzero `SVC` policy documented | Run fixture using nonzero `SVC`. | Behavior matches v1.5 policy in both kernel and non-kernel profiles. |
| TC-V15-EXC-011 | `BRK` policy documented in kernel mode | Task executes `BRK`. | Breakpoint/trap is handled or rejected according to documented policy. |
| TC-V15-EXC-012 | `ERET` outside handler rejected | Guest executes `ERET` in normal task. | Deterministic invalid-return/error path. |
| TC-V15-EXC-013 | Exception context survives scheduler entry | Trigger yield and inspect context. | Cause, interrupted `pc`, resume `pc`, and flags match expected values. |
| TC-V15-EXC-014 | Handler fault is deterministic | Handler performs invalid memory access. | Double-fault-style error or kernel panic occurs without host crash. |
| TC-V15-EXC-015 | Nested trap policy enforced | Handler triggers another yield/trap. | Nested exception is handled only if supported; otherwise stable nested-fault diagnostic. |

### D. Cooperative Task Creation and State

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-TASK-001 | Single task runs to exit | Boot kernel with one task. | Task enters `RUNNING`, exits, and kernel halts success. |
| TC-V15-TASK-002 | Two tasks yield round-robin | Boot two tasks that append markers and yield. | Marker order is `A B A B` or documented round-robin order. |
| TC-V15-TASK-003 | Three tasks yield round-robin | Boot three tasks. | Scheduler visits ready tasks in stable order. |
| TC-V15-TASK-004 | Exited task is not rescheduled | Task A exits; Task B yields twice. | A is skipped after exit; B continues. |
| TC-V15-TASK-005 | All tasks exited halts kernel | All tasks call task_exit. | Kernel halts with success and no spurious instruction-limit failure. |
| TC-V15-TASK-006 | Empty task table rejected or ignored | Boot with zero tasks. | Behavior matches docs: immediate success, panic, or setup rejection. |
| TC-V15-TASK-007 | Maximum task count supported | Boot exactly max tasks. | All tasks run in stable order. |
| TC-V15-TASK-008 | Task count over maximum rejected | Boot max+1 tasks. | Setup fails or kernel reports task-create error deterministically. |
| TC-V15-TASK-009 | Duplicate task stack rejected | Configure two tasks sharing a stack. | Setup/kernel rejects overlap. |
| TC-V15-TASK-010 | Task stack overlaps code rejected | Configure task stack on code page. | Setup/kernel rejects overlap before unsafe execution. |
| TC-V15-TASK-011 | Task stack overlaps MMIO rejected | Configure task stack in device range. | Setup/kernel rejects overlap. |
| TC-V15-TASK-012 | Misaligned task entry rejected | Add task with unaligned entry. | Setup/kernel rejects task. |
| TC-V15-TASK-013 | Non-executable task entry rejected | Add task in non-exec memory. | Setup/kernel rejects task or produces deterministic fetch fault. |
| TC-V15-TASK-014 | Task state transitions are visible | Inspect internal task table in unit test or debugger. | States transition through documented sequence only. |
| TC-V15-TASK-015 | Blocked task not scheduled | Mark task blocked/sleeping if supported. | Scheduler skips blocked task until wake condition. |

### E. Context Save and Restore

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-CTX-001 | `pc` restored after yield | Task yields mid-sequence then resumes. | Task resumes at instruction after yield. |
| TC-V15-CTX-002 | `sp` restored per task | Two tasks use separate stacks and yield. | Each task reads its own stack data after resume. |
| TC-V15-CTX-003 | Callee-saved registers preserved | Tasks set `x19-x30`, yield, and validate. | Documented saved registers retain values. |
| TC-V15-CTX-004 | Caller-saved policy documented | Tasks set `x0-x18`, yield, and inspect. | Values match documented guarantee; tests do not rely on unspecified registers. |
| TC-V15-CTX-005 | Flags preserved if promised | Task sets NZCV, yields, then branches on flags. | Branch result matches documented flag preservation policy. |
| TC-V15-CTX-006 | Link register preserved if promised | Task uses call frame across yield. | Return path remains correct. |
| TC-V15-CTX-007 | Frame pointer preserved if promised | Task uses `x29` stack frame across yield. | Locals remain accessible and function returns. |
| TC-V15-CTX-008 | Large register patterns survive switches | Fill saved registers with unique bit patterns. | No cross-task contamination. |
| TC-V15-CTX-009 | Zero values survive switches | Save zero in every preserved register. | Zero is not replaced by stale state. |
| TC-V15-CTX-010 | Maximum 64-bit values survive switches | Save `0xffffffffffffffff` patterns. | Values are restored exactly. |
| TC-V15-CTX-011 | 32-bit writes zero-extend before save | Task writes to `wN`, yields, reads `xN`. | Upper bits follow existing AArch64 teaching behavior. |
| TC-V15-CTX-012 | Stack alignment preserved | Task checks `sp % 16 == 0` after every yield. | Alignment remains stable. |
| TC-V15-CTX-013 | Context switch does not clobber exception context | Trigger yield then inspect saved exception fields. | Scheduler metadata and exception context are not mixed. |
| TC-V15-CTX-014 | Failed task creation does not corrupt running task | Attempt invalid task create if API exists. | Current task continues with registers intact. |
| TC-V15-CTX-015 | Repeated switches stay stable | Run thousands of yield cycles under instruction limit. | Final counters are correct and no leak/corruption occurs. |

### F. Scheduler Semantics

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-SCHED-001 | Round-robin order is deterministic | Run fixed task set multiple times. | Output/state order is identical across runs. |
| TC-V15-SCHED-002 | Scheduler handles task exit during first quantum | Task exits immediately. | Next ready task runs. |
| TC-V15-SCHED-003 | Scheduler handles task exit after multiple yields | Task yields then exits. | Task is removed from ready set only after exit. |
| TC-V15-SCHED-004 | Scheduler handles last-task yield | One ready task yields. | It resumes itself or halts per documented policy. |
| TC-V15-SCHED-005 | Scheduler handles no-ready-task state | All tasks blocked if blocking exists. | Kernel idles, halts, or panics according to docs. |
| TC-V15-SCHED-006 | Instruction limit still protects infinite task | Task never yields or halts. | Instruction-limit runtime error occurs deterministically. |
| TC-V15-SCHED-007 | Yield count does not overflow quickly | Task yields near counter boundary if counters exposed. | Overflow is prevented or handled deterministically. |
| TC-V15-SCHED-008 | Scheduler rejects invalid current task id | Corrupt current task id through fixture/test hook. | Deterministic kernel panic or setup rejection. |
| TC-V15-SCHED-009 | Scheduler preserves halted state | Task halts unexpectedly. | Behavior matches docs: task exit, kernel halt, or panic. |
| TC-V15-SCHED-010 | Scheduler can run tasks loaded from ELF sections | Build ELF with multiple task entry symbols if supported. | Entries run in documented order. |
| TC-V15-SCHED-011 | Scheduler event trace is concise | Run trace on multi-task fixture. | Trace shows yield/task switch events without dumping excessive internals. |
| TC-V15-SCHED-012 | Scheduler state appears in `info` if exposed | Run `emulator info` or debugger state command. | Task count/current task/state summary is shown. |

### G. Timer Tick and Interrupt Interaction

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-TMR-001 | Timer tick can be armed in kernel mode | Configure deterministic interval. | Pending timer interrupt appears at documented instruction boundary. |
| TC-V15-TMR-002 | Timer tick enters kernel vector | Run fixture with enabled interrupts. | Timer cause enters vector and handler returns. |
| TC-V15-TMR-003 | Timer tick can request scheduling | Task runs until timer handler requests yield if supported. | Scheduler switches only at documented safe point. |
| TC-V15-TMR-004 | Timer masked during scheduler critical section | Force timer deadline during context save. | Timer is deferred until scheduler exits critical section. |
| TC-V15-TMR-005 | Pending timer survives masking | Disable interrupts, pass deadline, re-enable. | Pending timer is delivered once. |
| TC-V15-TMR-006 | Pending timer clear works | Guest clears pending flag. | No stale interrupt is delivered. |
| TC-V15-TMR-007 | Interval zero disables timer | Set interval to zero. | No timer interrupts occur. |
| TC-V15-TMR-008 | Interval one fires predictably | Set interval to one instruction. | Delivery order matches documented sample boundary. |
| TC-V15-TMR-009 | Large interval does not fire early | Set huge interval and run small program. | No timer interrupt occurs. |
| TC-V15-TMR-010 | Timer and yield same boundary order documented | Task reaches yield when timer is pending. | Event priority matches docs. |
| TC-V15-TMR-011 | Timer and task exit same boundary order documented | Task exits when timer is pending. | Event priority matches docs. |
| TC-V15-TMR-012 | Timer does not starve tasks | Run multiple tasks with frequent ticks. | Tasks still complete in deterministic order. |

### H. Memory Safety and Permission Edge Cases

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-MEM-001 | Kernel write to read-only code page faults | Kernel stores to code mapping. | Write-permission fault or kernel panic is deterministic. |
| TC-V15-MEM-002 | Kernel execute from data page faults | Kernel branches to data page. | Exec-permission fault is deterministic. |
| TC-V15-MEM-003 | Task read from unmapped memory faults | Task reads unmapped address. | Task fault handling policy is applied. |
| TC-V15-MEM-004 | Task write beyond stack top faults | Task overflows stack upward/downward per layout. | Guard/mapping fault is deterministic. |
| TC-V15-MEM-005 | Task stack boundary last byte allowed | Write exactly inside final valid stack byte. | Access succeeds. |
| TC-V15-MEM-006 | Task stack boundary first invalid byte rejected | Write one byte outside stack. | Access faults. |
| TC-V15-MEM-007 | Cross-page access validates both pages | Load/store spanning valid and invalid pages. | No partial write; deterministic fault. |
| TC-V15-MEM-008 | Cross-device access rejected | Load/store spans RAM into device or between devices. | Deterministic device/range fault. |
| TC-V15-MEM-009 | Boot-info is read-only if promised | Kernel attempts to write boot-info. | Write fault if docs promise read-only, otherwise write behavior is documented. |
| TC-V15-MEM-010 | Task descriptor protection documented | Task writes descriptor memory. | Allowed or faulting behavior matches docs. |
| TC-V15-MEM-011 | Null pointer access policy stable | Kernel/task accesses address zero. | Fault or valid behavior matches memory map docs. |
| TC-V15-MEM-012 | Highest valid RAM address boundary | Access `EMU_MEMORY_SIZE - access_size`. | In-bounds access succeeds if mapped. |
| TC-V15-MEM-013 | First byte past RAM rejected | Access `EMU_MEMORY_SIZE`. | Fault without host crash. |
| TC-V15-MEM-014 | 64-bit address wrap rejected | Access near `UINT64_MAX` with size crossing wrap. | Range validation rejects access. |
| TC-V15-MEM-015 | Failed access does not corrupt registers unexpectedly | Trigger memory fault and inspect state. | Register changes match documented atomicity. |

### I. Device and Console Interaction

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-DEV-001 | Kernel writes console through UART | Run kernel console fixture. | stdout contains exact expected text. |
| TC-V15-DEV-002 | Multiple tasks write deterministic console output | Two tasks write markers and yield. | Output order matches scheduler order. |
| TC-V15-DEV-003 | Bad UART width rejected | Kernel writes unsupported width to UART. | Device fault or stable error. |
| TC-V15-DEV-004 | Timer MMIO still readable in kernel mode | Kernel reads v1.3 timer registers. | Values remain deterministic. |
| TC-V15-DEV-005 | Random MMIO remains deterministic after seed | Kernel seeds and reads random device. | Expected pseudo-random sequence repeats across runs. |
| TC-V15-DEV-006 | Exception controller MMIO remains coherent | Kernel reads cause/resume/depth after trap. | Values match v1.4 semantics. |
| TC-V15-DEV-007 | Invalid device address faults | Kernel accesses unmapped MMIO range. | Device fault or kernel panic is deterministic. |
| TC-V15-DEV-008 | Device fault in task applies task-fault policy | Task accesses invalid device. | Task is marked faulted or kernel panics per docs. |
| TC-V15-DEV-009 | Device output flushes before exit | Kernel writes then exits. | Output is visible before process exits. |
| TC-V15-DEV-010 | Zero-length console write is harmless | Kernel writes length zero if supported. | No output and no error. |

### J. Loader and Image Validation

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-LOAD-001 | Raw kernel image accepted | Run valid raw fixture. | Boot succeeds. |
| TC-V15-LOAD-002 | ELF kernel image accepted | Run valid ELF fixture. | Boot succeeds and mappings match ELF permissions. |
| TC-V15-LOAD-003 | Mach-O kernel policy documented | Try Mach-O fixture in kernel mode. | Accepted or rejected according to docs with stable output. |
| TC-V15-LOAD-004 | Truncated kernel image rejected | Generate empty/truncated image. | Loader rejects without host crash. |
| TC-V15-LOAD-005 | Oversized kernel image rejected | Generate image larger than memory. | Loader rejects with clear error. |
| TC-V15-LOAD-006 | Overlapping ELF segments rejected | Generate overlapping segments. | Loader rejects before boot. |
| TC-V15-LOAD-007 | Writable-executable segment policy documented | Generate `RWX` segment. | Accepted or rejected according to security/teaching policy. |
| TC-V15-LOAD-008 | Missing entry symbol/entry rejected | Generate image without valid entry. | Loader rejects. |
| TC-V15-LOAD-009 | Boot-info does not overlap loaded segments | Generate conflicting boot-info placement. | Setup rejects overlap. |
| TC-V15-LOAD-010 | Task metadata does not overflow image parsing | Generate malicious task count/size fields. | Parser rejects without out-of-bounds read/write. |
| TC-V15-LOAD-011 | Endianness rejection preserved | Generate wrong-endian ELF if applicable. | Loader rejects. |
| TC-V15-LOAD-012 | Unsupported architecture rejected | Generate non-AArch64 image. | Loader rejects. |

### K. CLI Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-CLI-001 | Help documents kernel mode | Run `emulator help`. | v1.5 kernel command/flags appear with concise usage. |
| TC-V15-CLI-002 | Kernel run command succeeds | Run valid kernel fixture. | Exit code and output match expected. |
| TC-V15-CLI-003 | Kernel trace command shows events | Run trace on two-task fixture. | Boot, task switch, yield, and halt events are visible. |
| TC-V15-CLI-004 | Kernel regs command remains stable | Run regs after kernel completion. | Register dump is deterministic and not polluted by debug-only fields. |
| TC-V15-CLI-005 | Kernel info command shows metadata | Run info on kernel fixture. | Shows image kind, entry, stack, vector, tasks if supported. |
| TC-V15-CLI-006 | Invalid flag value rejected | Pass malformed stack size/task count/vector. | Usage error and stable exit code. |
| TC-V15-CLI-007 | Unknown flag rejected | Pass unsupported kernel flag. | Usage error and stable exit code. |
| TC-V15-CLI-008 | Missing program path rejected | Invoke kernel command without image. | Usage error. |
| TC-V15-CLI-009 | Nonexistent file rejected | Run missing image path. | File error is user-readable. |
| TC-V15-CLI-010 | Output is deterministic across repeated runs | Run same fixture multiple times. | stdout/stderr/exit code are identical. |
| TC-V15-CLI-011 | Kernel panic exit code stable | Run panic fixture. | Documented nonzero exit code and panic message. |
| TC-V15-CLI-012 | Unhandled task fault exit code stable | Run task fault fixture. | Documented nonzero exit code and fault message. |
| TC-V15-CLI-013 | Instruction limit option still works | Run infinite task with low limit if flag exists. | Limit error occurs at predictable count. |
| TC-V15-CLI-014 | CLI rejects generated temp artifacts in release check | Run release hygiene check. | No generated v1.5 binaries/logs are committed accidentally. |

### L. Debugger Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-DBG-001 | Debugger can start kernel fixture | Run debugger script with `regs` then `quit`. | Starts at kernel entry with expected `pc`/`sp`. |
| TC-V15-DBG-002 | Step through boot | Script `step` several instructions. | `pc` advances and boot state remains inspectable. |
| TC-V15-DBG-003 | Step into yield trap | Step task to yield instruction. | Debugger reports trap/exception entry visibly. |
| TC-V15-DBG-004 | Continue through yield | Set breakpoint after resume and continue. | Breakpoint hits in resumed task. |
| TC-V15-DBG-005 | Breakpoint in task A does not affect task B | Set breakpoint in A; run two tasks. | Breakpoint hits only for A's address. |
| TC-V15-DBG-006 | Breakpoint in scheduler works | Set breakpoint in scheduler/vector if symbols/addresses known. | Debugger stops at scheduler code. |
| TC-V15-DBG-007 | Memory inspection can read task stacks | Use `mem`/`x` on stack addresses. | Expected stack data is shown. |
| TC-V15-DBG-008 | Map inspection shows kernel/task mappings | Use `maps` and `map`. | Kernel code, stacks, boot-info, and devices are clear if exposed. |
| TC-V15-DBG-009 | Task state command works if added | Use `tasks`/`kernel` command if implemented. | Shows current task and states deterministically. |
| TC-V15-DBG-010 | Existing debugger commands reject extra args | Test malformed commands. | v0.5 parsing strictness remains. |
| TC-V15-DBG-011 | Overlong debugger input still handled | Feed overlong line. | Clean error, no crash. |
| TC-V15-DBG-012 | Trace on/off in debugger includes kernel events | Toggle trace and continue. | Event lines appear only while trace is on. |
| TC-V15-DBG-013 | Debugger handles kernel panic | Continue panic fixture. | Stops/reports panic with stable message. |
| TC-V15-DBG-014 | Debugger handles all-tasks-exited success | Continue successful fixture. | Reports normal halt/completion. |

### M. Documentation and Lessons

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-DOC-001 | README links v1.5 test plan | Run docs grep test. | README includes `docs/test-plan-v1.5.md`. |
| TC-V15-DOC-002 | README links v1.5 lesson when added | Run docs grep test. | README includes lesson link if lesson exists. |
| TC-V15-DOC-003 | Test plan lists scope and out-of-scope | Grep docs. | Clear sections exist. |
| TC-V15-DOC-004 | Lesson explains boot contract | Grep lesson. | Entry, stack, registers, and boot-info are documented. |
| TC-V15-DOC-005 | Lesson explains cooperative scheduling | Grep lesson. | Yield, task states, and context switching are documented. |
| TC-V15-DOC-006 | Lesson explains limitations | Grep lesson. | No claims of real OS, real MMU, or preemption unless implemented. |
| TC-V15-DOC-007 | CLI help matches docs | Compare help text keywords to docs. | Public command/flag names are consistent. |
| TC-V15-DOC-008 | Example README lists fixtures | Grep `examples/v1_5/README.md`. | Every generated example is documented. |
| TC-V15-DOC-009 | Docs mention deterministic behavior | Grep v1.5 docs. | Timer/scheduler determinism is explicit. |
| TC-V15-DOC-010 | Docs mention edge cases and failure modes | Grep v1.5 docs. | Panic, unhandled fault, invalid image, and instruction limit are documented. |

### N. Build, Sanitizer, and Portability

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-BLD-001 | Clean build succeeds | Run `make clean && make`. | No warnings with project `CFLAGS`. |
| TC-V15-BLD-002 | Full test target includes v1.5 | Run `make test`. | v1.5 unit/CLI/docs tests run. |
| TC-V15-BLD-003 | Fixture generation target works | Run v1.5 fixture marker target. | Fixtures generated in `tests/v1_5/tmp`. |
| TC-V15-BLD-004 | Examples target includes v1.5 examples | Run `make examples`. | v1.5 examples generated or skipped only with documented optional toolchain message. |
| TC-V15-BLD-005 | Clean removes v1.5 generated files | Run `make clean`. | v1.5 temp binaries/logs are removed. |
| TC-V15-BLD-006 | ASan test target passes | Run `make test-asan`. | No address sanitizer failures. |
| TC-V15-BLD-007 | UBSan test target passes | Run `make test-ubsan`. | No undefined behavior sanitizer failures. |
| TC-V15-BLD-008 | Compiler matrix passes | Run compiler matrix target. | Supported compilers build and test. |
| TC-V15-BLD-009 | Missing optional cross tools handled | Hide `clang`/`ld.lld` for optional examples. | Tests skip optional examples clearly, required generated fixtures still work. |
| TC-V15-BLD-010 | Release archive excludes generated v1.5 temp files | Run release archive check. | Archive contains source/docs/generators but not temp outputs. |

### O. Negative and Fuzz-Like Robustness

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V15-NEG-001 | Random bytes as kernel image rejected or treated as raw safely | Run small random file. | No host crash; stable loader/runtime error. |
| TC-V15-NEG-002 | Empty file rejected | Run empty file. | User-readable error. |
| TC-V15-NEG-003 | One-byte file rejected safely | Run one-byte raw file. | Fetch/invalid image error without out-of-bounds read. |
| TC-V15-NEG-004 | Huge declared task count rejected | Malformed boot metadata declares huge count. | Parser rejects before allocation overflow. |
| TC-V15-NEG-005 | Huge stack size rejected | Configure stack size near `UINT64_MAX`. | Arithmetic overflow check rejects setup. |
| TC-V15-NEG-006 | Address plus size wrap rejected | Configure stack/address pair that wraps. | Setup rejects. |
| TC-V15-NEG-007 | Invalid task state rejected | Corrupt descriptor state if guest-visible. | Scheduler reports deterministic panic/error. |
| TC-V15-NEG-008 | Invalid saved `pc` rejected on resume | Corrupt task context `pc`. | Resume fails with fetch/exec fault, not host crash. |
| TC-V15-NEG-009 | Invalid saved `sp` rejected on resume | Corrupt task context `sp`. | Stack access faults deterministically. |
| TC-V15-NEG-010 | Recursive panic does not recurse forever | Panic handler triggers panic. | Double-panic diagnostic or hard halt. |
| TC-V15-NEG-011 | Malformed debugger script does not crash | Feed invalid commands during kernel mode. | Clean errors and debugger remains usable. |
| TC-V15-NEG-012 | Malformed fixture generator args rejected | Run generator with bad output path/option. | Generator exits nonzero with clear message. |

## Acceptance Criteria

v1.5 is ready to ship when all of the following are true:

1. The v1.5 test plan is committed and linked from `README.md`.
2. The v1.5 lesson and example README are committed if implementation work is
   included in the same release.
3. All v0.1 through v1.4 regression tests pass unchanged or documented behavior
   changes have explicit migration notes.
4. The toy-kernel boot contract is documented and covered by unit and CLI tests.
5. At least one valid kernel fixture boots and halts successfully.
6. At least one two-task fixture demonstrates deterministic cooperative yield.
7. Context save/restore tests prove that documented preserved registers, `pc`,
   `sp`, and flags are handled correctly.
8. Task exit, kernel panic, invalid image, task fault, and instruction-limit
   failures have stable diagnostics and exit codes.
9. Debugger tests cover stepping into a yield/trap and continuing after a task
   switch.
10. Timer/interrupt interaction is either implemented and tested or explicitly
    documented as deferred.
11. Sanitizer and release checks pass.
12. Generated fixtures are reproducible and cleaned by `make clean`.

## Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| v1.5 accidentally becomes a real OS project instead of a teaching milestone. | Keep the profile minimal, deterministic, and explicitly out-of-scope for real MMU/preemption/POSIX behavior. |
| Scheduler tests depend on undocumented internals. | Test public behavior first; use unit-only internals sparingly and document them. |
| Context switching creates subtle register corruption. | Use unique per-task register patterns, repeated switch loops, and sanitizer runs. |
| Timer behavior becomes flaky. | Base ticks on instruction count only and pin delivery boundaries in tests. |
| Debugger output becomes too noisy. | Add focused event lines and keep full internals behind explicit commands or trace mode. |
| Fixture generation depends on local toolchains. | Keep required fixtures generated by Python writers; mark cross-compiled examples optional. |
| Kernel profile breaks existing syscall or exception behavior. | Run full regression gate and include explicit non-kernel compatibility tests. |

## Open Decisions

- Is v1.5 exposed as a new CLI command, a `run --kernel` mode, or only a guest
  convention?
- Does v1.5 use a guest-visible boot-info block, fixed registers only, or both?
- Are task descriptors host-managed, guest-managed, or shared teaching data?
- Which registers are guaranteed to survive cooperative yield?
- Does `HLT` mean kernel halt, task exit, or invalid operation in task context?
- Are timer ticks allowed to cause scheduling in v1.5, or are they only counted
  and reported?
- Does a task fault kill only the task, panic the whole kernel, or enter a
  guest-defined task-fault handler?
- Should the scheduler be implemented in emulator host code for teaching, in
  guest fixture code, or as a hybrid?

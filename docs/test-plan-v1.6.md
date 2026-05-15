# v1.6 Test Plan — Tiny OS Lab and Guest-Managed Tasks

## Version Goal

v1.6 should turn the v1.5 host-configured toy-kernel profile into a small
**Tiny OS Lab** where the guest kernel can create, name, run, inspect, and
control tasks through a documented kernel ABI.

Before v1.6, the emulator can boot an opt-in toy-kernel profile, start
host-configured cooperative tasks, preserve task context, schedule ready tasks,
put tasks to sleep on deterministic timer ticks, isolate task faults, and expose
kernel state through `info`, trace, and debugger output.

The v1.6 promise is:

```text
load kernel -> kernel receives boot info -> kernel creates tasks -> tasks call kernel services -> scheduler runs them deterministically -> kernel reports results
```

v1.6 is still a learning operating-systems lab, not a real OS. It should not
implement real ARM exception levels, SMP, POSIX processes, a full filesystem,
real asynchronous devices, dynamic linking, or production security isolation.
The goal is a testable bridge from host-managed toy tasks to guest-managed
kernel/task experiments.

## Scope

### In Scope

- v0.1 through v1.5 regression behavior must continue to pass.
- Preserve the v1.5 `--kernel` profile and all v1.5 trap behavior.
- Add a guest-visible task-management ABI usable by kernel code.
- Allow the kernel to create tasks at runtime from guest code, not only through
  `--kernel-task` or host APIs.
- Add a small guest-visible task descriptor table or boot-info extension that
  exposes task metadata in a stable beginner-friendly format.
- Add deterministic task IDs, task names or labels, task states, exit status,
  fault cause, and wake tick metadata.
- Add a documented kernel syscall/service dispatch path for user tasks to ask
  the toy kernel for simple services.
- Add at least one deterministic inter-task communication mechanism suitable for
  teaching, such as fixed-size message slots or a tiny mailbox queue.
- Add stronger scheduler tests for mixed host-created and guest-created tasks.
- Add memory-region validation for per-task stacks, task descriptors, boot-info,
  kernel code/data, and MMIO ranges.
- Extend debugger and `info` output so a learner can inspect guest-created tasks
  and kernel-service state.
- Extend examples and lessons with a tiny guest-managed OS demo.
- Add unit, CLI, debugger, docs, fixture, sanitizer, and release tests.
- Document every public convention introduced by v1.6.

### Out of Scope

- Real EL0/EL1 privilege separation.
- Real per-process MMU address spaces or TLB behavior.
- Preemptive scheduling based on host time or host threads.
- Multiprocessor scheduling, atomics, locks, races, or memory-ordering lessons.
- Dynamic linking, hosted libc, process spawning, pipes, networking, or a real
  filesystem.
- Loading arbitrary third-party kernels.
- Full ARM GIC, device tree parsing, ACPI, PCI, or real interrupt-controller
  compatibility.
- Source-level debugging, DWARF, symbolic stack unwinding, or profiling.
- Security isolation strong enough to run untrusted code.

## Implementation Assumptions

1. The emulator remains single-threaded and deterministic.
2. v1.6 is opt-in through `--kernel`; normal raw, ELF64, Mach-O, syscall,
   virtual-memory, MMIO, exception, and v1.5 behavior remains stable.
3. Host-created tasks from v1.5 remain supported for teaching and regression
   coverage.
4. Guest-created tasks use ordinary executable guest addresses and ordinary
   mapped guest stack memory.
5. The guest kernel owns task creation policy. The emulator validates memory
   safety and deterministic bounds.
6. The v1.6 task table has a documented fixed maximum size.
7. Task IDs are stable, deterministic, and never reused unless the ABI explicitly
   documents reuse rules.
8. The task descriptor table is readable by the guest kernel; write access is
   limited or validated according to the documented ABI.
9. Kernel services are encoded with documented toy-kernel traps or a documented
   MMIO/service-page ABI.
10. Existing v1.5 `BRK #0x150` through `BRK #0x155` semantics remain stable.
11. New service errors are reported through deterministic return values, task
    fault metadata, kernel panic, or stable CLI exit codes.
12. Message passing uses fixed-size deterministic buffers; no dynamic allocation
    is required.
13. Timer ticks remain instruction-count based.
14. A task fault remains isolated to the current task unless the guest kernel
    explicitly asks to panic on task faults.
15. Debugger commands are host controls and must not mutate guest state unless
    the command is explicitly a state-changing command.
16. Generated fixtures are deterministic and source-controlled generators are
    preferred over checked-in binaries.

## Proposed v1.6 Teaching Model

The exact names may change during implementation, but tests should pin the final
public behavior through the C API, CLI output, debugger output, fixtures, and
README/lesson documentation.

### Guest-managed task creation

Recommended kernel service:

```text
create_task(entry, stack_base, stack_size, arg0, flags) -> task_id or error
```

Recommended validation:

```text
entry is 4-byte aligned
entry is mapped executable RAM
stack_base..stack_base+stack_size is mapped writable RAM
stack is non-empty and aligned
stack does not overlap kernel code, boot-info, MMIO, or another live stack
task table has free slot
flags contain only documented bits
```

### Guest-visible task descriptor

Recommended descriptor fields:

```text
magic
version
descriptor_size
task_id
state
entry_pc
saved_pc
saved_sp
stack_base
stack_size
exit_code
fault_cause
fault_address
wake_tick
switch_count
name_or_label[16]
```

### Kernel services

Recommended service set:

```text
TASK_CREATE
TASK_YIELD
TASK_EXIT
TASK_SLEEP
TASK_GET_ID
TASK_GET_INFO
TASK_SEND
TASK_RECV
CONSOLE_WRITE
KERNEL_PANIC
```

The implemented v1.6 ABI is `BRK #0x160` with `x8` as the service ID. `x0` is the primary result register, `x1` is used only by documented secondary results, and all other argument registers are volatile across the service trap. Boot-info exposes a supported-service bitmask for discovery. When `--kernel-boot-info` is omitted, boot-info and task descriptors are intentionally absent; services that depend on descriptor addresses, such as `TASK_GET_INFO`, return `BAD_ARGUMENT` instead of returning unmapped pointers.

### Tiny mailbox

Recommended deterministic IPC shape:

```text
mailbox_count: fixed
message_size: fixed, for example 32 bytes
send: copies exactly N bytes or returns WOULD_BLOCK/FULL
recv: copies a whole queued message when the destination buffer is large enough, otherwise returns BAD_ARGUMENT without dequeueing
blocking mode: intentionally unsupported in v1.6; empty/full paths return WOULD_BLOCK
```

Timer/sleep policy is also explicit: timer interrupts before scheduler handoff are consumed by the toy-kernel timer counter instead of becoming unhandled exceptions, all-sleeping tasks with `--timer-interrupt` idle deterministically until the next wake tick, and all-sleeping tasks without a timer report a deadlock.

`emulator info` is a static loader/configuration view and does not execute guest code. It shows host-configured tasks and kernel metadata present before execution; guest-created tasks are visible through trace/debugger output during or after execution, not through a non-executing `info` invocation.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v1.6.md
lessons/v1.6-tiny-os-lab.md
examples/v1_6/README.md
examples/v1_6/generate_tiny_os_fixtures.py
tests/fixtures/tiny_os_fixture_writer.py
tests/v1_6/test_v1_6.c
tests/v1_6/test_cli_tiny_os.sh
tests/v1_6/test_debugger_tiny_os.sh
tests/v1_6/test_docs_tiny_os.sh
tests/v1_6/test_optional_tiny_os_examples.sh
```

Optional runnable examples:

```text
examples/v1_6/minimal_guest_create_task.s
examples/v1_6/two_guest_tasks_yield.s
examples/v1_6/guest_task_args.s
examples/v1_6/mailbox_ping_pong.s
examples/v1_6/sleeping_guest_tasks.s
examples/v1_6/task_fault_supervisor.s
examples/v1_6/max_tasks.s
examples/v1_6/invalid_task_create.s
```

## Test Data Strategy

1. Prefer generated raw fixtures for exact instruction/control-flow behavior.
2. Use generated ELF fixtures when loader metadata or segment permissions matter.
3. Generate malformed fixtures for invalid entries, stacks, descriptors, service
   numbers, and mailbox operations.
4. Use small deterministic memory markers instead of large golden logs.
5. Keep golden CLI/debugger output only for stable user-facing text.
6. Do not commit generated `.bin`, `.elf`, logs, or temporary debugger scripts
   unless the project intentionally stores binary fixtures.
7. Every generator must be reproducible without network access or host-specific
   absolute paths.
8. Include sanitizer runs for the v1.6 C unit tests and CLI fixture paths.

## Test Case Matrix

### A. Regression Gate

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-REG-001 | v0.1 through v1.5 deterministic suite still passes | Run `make test` or the release test target. | All earlier tests pass without changed public behavior. |
| TC-V16-REG-002 | v1.5 toy-kernel traps remain stable | Run v1.5 unit and CLI tests. | `BRK #0x150` through `BRK #0x155` keep documented behavior. |
| TC-V16-REG-003 | Non-kernel raw binaries unchanged | Run representative v0.1 through v0.7 fixtures without `--kernel`. | Output, registers, and exit codes are unchanged. |
| TC-V16-REG-004 | ELF and Mach-O loaders unchanged outside kernel mode | Run v0.8, v1.1, and v1.2 loader/VM tests. | Loader validation and permissions remain stable. |
| TC-V16-REG-005 | Exception vectors still work | Run v1.4 exception tests. | v1.6 services do not break v1.4 exception entry/return. |
| TC-V16-REG-006 | v1.5 host-created tasks still work | Boot a v1.5 fixture using `--kernel-task`. | Host-created tasks still schedule and exit correctly. |
| TC-V16-REG-007 | Release archive includes v1.6 docs/tests | Run release archive check. | Fresh archive contains v1.6 artifacts and passes deterministic checks. |

### B. Boot and ABI Discovery

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-BOOT-001 | v1.6 boot-info version exposed | Run kernel with boot-info enabled. | Boot-info reports a documented version supporting v1.6 extensions. |
| TC-V16-BOOT-002 | v1.5 boot-info compatibility | Run a v1.5 fixture under v1.6 implementation. | Fields used by v1.5 remain in the same location and semantics. |
| TC-V16-BOOT-003 | Descriptor table pointer valid | Kernel reads descriptor table pointer/size. | Pointer is mapped readable/read-only and size matches documented max descriptors. |
| TC-V16-BOOT-004 | Service table or service trap documented | Kernel probes the service ABI. | Supported service IDs are discoverable or documented; unknown service returns stable error. |
| TC-V16-BOOT-005 | ABI absent when boot-info disabled | Run without boot-info if supported. | Descriptor-dependent services such as `TASK_GET_INFO` return `BAD_ARGUMENT`; non-descriptor services still work. |
| TC-V16-BOOT-006 | Descriptor table alignment | Inspect descriptor pointer. | Pointer and descriptor size meet documented alignment. |
| TC-V16-BOOT-007 | Descriptor table does not overlap code | Compare descriptor memory with kernel/program mappings. | No overlap with executable code or MMIO. |
| TC-V16-BOOT-008 | Bad boot-info magic handled | Run fixture that validates intentionally corrupt metadata. | Kernel reports panic/error deterministically; emulator does not crash. |

### C. Guest Task Creation

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-TASK-001 | Kernel creates one task | Kernel calls `create_task` then starts scheduler. | Service returns task ID; task runs and exits. |
| TC-V16-TASK-002 | Kernel creates two tasks | Kernel creates A and B. | Tasks run in deterministic round-robin order. |
| TC-V16-TASK-003 | Kernel creates three tasks | Kernel creates A, B, C. | Scheduler visits all ready tasks in stable order. |
| TC-V16-TASK-004 | Kernel creates exactly max tasks | Create maximum documented task count. | All slots are accepted and all tasks can run. |
| TC-V16-TASK-005 | Creating one beyond max fails | Create max tasks plus one. | Extra create returns stable `NO_SLOT` error; existing tasks remain valid. |
| TC-V16-TASK-006 | Task IDs deterministic | Create tasks in known order twice. | IDs are identical across runs. |
| TC-V16-TASK-007 | Task entry receives argument | Create task with `arg0`. | Task observes documented initial register value. |
| TC-V16-TASK-008 | Task initial registers deterministic | Inspect task at first instruction. | Documented registers are set/zeroed; no host garbage leaks. |
| TC-V16-TASK-009 | Task name/label stored | Create named task if supported. | Descriptor/debugger shows stable truncated or full label per docs. |
| TC-V16-TASK-010 | Host and guest tasks can coexist | Pass one `--kernel-task`, then guest creates another. | Both tasks are represented and schedule in documented order. |
| TC-V16-TASK-011 | Creating task after scheduler start | Running task asks kernel to create another task. | New task becomes ready at documented point. |
| TC-V16-TASK-012 | Creating task after all others exit | Kernel creates a new task after a completed round. | Behavior is documented: allowed with new slot or rejected if slots are one-shot. |
| TC-V16-TASK-013 | Reusing exited task slot policy | Exit task, then create another. | Slot reuse behavior matches docs exactly. |
| TC-V16-TASK-014 | Zero task create then start | Kernel starts scheduler without creating tasks. | Same stable behavior as v1.5 zero-task success or documented v1.6 policy. |

### D. Task Creation Validation

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-VAL-001 | Misaligned entry rejected | Create task with entry not divisible by 4. | Stable `BAD_ENTRY` error; task table unchanged. |
| TC-V16-VAL-002 | Unmapped entry rejected | Create task with entry outside RAM. | Stable error; no host crash. |
| TC-V16-VAL-003 | Non-executable entry rejected | Create task in read/write data page. | Exec-permission error returned. |
| TC-V16-VAL-004 | Entry inside MMIO rejected | Create task at UART/timer/random/controller address. | Stable bad-entry error. |
| TC-V16-VAL-005 | Null stack rejected | Create task with stack base/size zero. | Stable stack validation error. |
| TC-V16-VAL-006 | Unmapped stack rejected | Stack points outside RAM. | Stable error. |
| TC-V16-VAL-007 | Read-only stack rejected | Stack maps to read-only page. | Stable permission error. |
| TC-V16-VAL-008 | Stack overlaps kernel code | Stack range overlaps executable mapping. | Rejected before task becomes ready. |
| TC-V16-VAL-009 | Stack overlaps boot-info | Stack range overlaps boot metadata. | Rejected. |
| TC-V16-VAL-010 | Stack overlaps MMIO | Stack range overlaps device region. | Rejected. |
| TC-V16-VAL-011 | Stack overlaps live task stack | Create two tasks with overlapping stacks. | Second create is rejected. |
| TC-V16-VAL-012 | Stack boundary overflow | Use base+size that wraps address space. | Rejected without integer overflow or host crash. |
| TC-V16-VAL-013 | Invalid flags rejected | Pass unsupported task flags. | Stable `BAD_FLAGS` error. |
| TC-V16-VAL-014 | Descriptor table corruption protected | Guest attempts to write descriptor fields. | Guest write is rejected by the read-only descriptor mapping; internal scheduler state is unchanged. |

### E. Scheduler Semantics

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-SCHED-001 | Round-robin guest-created tasks | Three tasks yield repeatedly. | Visit order is stable and documented. |
| TC-V16-SCHED-002 | Exited task skipped | Task A exits; B and C continue. | A is never scheduled again. |
| TC-V16-SCHED-003 | Faulted task skipped | Task A faults; B exits normally. | A becomes `FAULTED`; B continues. |
| TC-V16-SCHED-004 | Sleeping task skipped until wake | Task sleeps for N ticks. | Other ready tasks run; sleeper wakes at expected tick. |
| TC-V16-SCHED-005 | All sleeping tasks advance on timer | All tasks sleep with timer enabled. | Timer idles forward to the next wake tick and schedules the awakened task deterministically. |
| TC-V16-SCHED-006 | Deadlock without timer | All tasks block waiting for future ticks with no timer. | Stable deadlock diagnostic and exit code. |
| TC-V16-SCHED-007 | Scheduler fairness under create-at-runtime | Task A creates B then yields. | B enters schedule at documented position. |
| TC-V16-SCHED-008 | Instruction limit still enforced | Task loops without yield. | Run stops at instruction limit with stable error; no corrupt task state. |
| TC-V16-SCHED-009 | Panic stops scheduler | Task calls panic service. | Kernel stops with panic exit code; no later task runs. |
| TC-V16-SCHED-010 | Completion status summary | Multiple tasks exit with statuses. | Kernel summary preserves each task status and CLI overall status follows docs. |
| TC-V16-SCHED-011 | Switch count increments | Run tasks through known switches. | Descriptor/debugger switch counts match expected values. |
| TC-V16-SCHED-012 | Context fully preserved | Use all `x0-x30`, `sp`, `pc`, flags across repeated switches. | Every documented register/flag survives. |
| TC-V16-SCHED-013 | Service register policy documented | Set sentinel registers before service call. | `x0`/documented `x1` results update; other argument registers are treated as volatile per docs and never relied on. |

### F. Kernel Service Dispatch

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-SVC-001 | Known service dispatch succeeds | Call each documented service with valid args. | Returns documented success/error values. |
| TC-V16-SVC-002 | Unknown service rejected | Call service ID outside range. | Stable unknown-service error; task can continue or fault per docs. |
| TC-V16-SVC-003 | Wrong argument count/shape rejected | Pass invalid pointer/length/register combo. | Stable error; no host memory access. |
| TC-V16-SVC-004 | Service register policy | Set sentinel registers before service call. | Result registers and volatile-register expectations match the documented ABI. |
| TC-V16-SVC-005 | Service from kernel boot context | Kernel calls service before scheduler start. | Only services documented as boot-safe work; others reject cleanly. |
| TC-V16-SVC-006 | Service from task context | Task calls services after scheduler start. | Allowed services work and update current task. |
| TC-V16-SVC-007 | Service after task exit impossible | Task attempts service after exit path. | No post-exit execution occurs. |
| TC-V16-SVC-008 | Nested service/trap policy | Service path triggers another trap if fixture can do so. | Nested behavior is accepted or rejected exactly as documented. |
| TC-V16-SVC-009 | Console write service validates buffer | Pass valid, empty, unmapped, read-only, and MMIO buffers. | Valid writes work; invalid buffers fail/fault deterministically. |
| TC-V16-SVC-010 | Get current task ID | Task asks for own ID. | Returned ID matches descriptor/debugger. |
| TC-V16-SVC-011 | Get task info for valid ID | Query each live/exited/faulted task. | Returned descriptor fields match internal state. |
| TC-V16-SVC-012 | Get task info for invalid ID | Query non-existent ID. | Stable not-found error. |

### G. Mailbox / IPC

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-IPC-001 | Send then receive one message | Task A sends to B; B receives. | Payload bytes match exactly. |
| TC-V16-IPC-002 | Empty receive nonblocking | Receive from empty mailbox. | Stable `WOULD_BLOCK`. |
| TC-V16-IPC-003 | Full mailbox send nonblocking | Fill mailbox then send one more. | Stable `WOULD_BLOCK`. |
| TC-V16-IPC-004 | FIFO ordering | Send messages 1, 2, 3. | Receiver gets 1, 2, 3. |
| TC-V16-IPC-005 | Message size boundary | Send 0 bytes, exactly max bytes, and max+1 bytes. | Boundary behavior matches docs; max+1 rejected. |
| TC-V16-IPC-006 | Invalid source buffer | Send from unmapped pointer. | Sender receives error or faults per docs. |
| TC-V16-IPC-007 | Invalid destination buffer | Receive into unmapped pointer. | Receiver receives error or faults per docs. |
| TC-V16-IPC-008 | Send to invalid task ID | Send to missing/exited/faulted task. | Stable error. |
| TC-V16-IPC-009 | Receive after sender exits | Sender exits after sending. | Queued message remains or is discarded per docs. |
| TC-V16-IPC-010 | Blocking receive wakeup | If blocking recv exists, B blocks then A sends. | B wakes in deterministic order. |
| TC-V16-IPC-011 | Blocking send wakeup | If blocking send exists, A blocks on full queue then B receives. | A wakes deterministically. |
| TC-V16-IPC-012 | IPC visible in debugger/info | Inspect after send/receive. | Queue counts and blocked tasks are visible if documented. |

### H. Faults and Recovery

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-FAULT-001 | Guest-created task invalid instruction | Task executes invalid opcode. | Task becomes `FAULTED`; others continue. |
| TC-V16-FAULT-002 | Guest-created task bad read | Task reads unmapped memory. | Task fault metadata records cause/address. |
| TC-V16-FAULT-003 | Guest-created task bad write | Task writes read-only/unmapped memory. | Task fault isolated. |
| TC-V16-FAULT-004 | Guest-created task bad execute | Task branches to non-executable page. | Task fault isolated. |
| TC-V16-FAULT-005 | `ERET` outside exception in guest task | Task executes invalid `ERET`. | Same v1.5 fault isolation behavior. |
| TC-V16-FAULT-006 | Fault during create_task service | Use bad pointers/stacks. | Service returns error; does not corrupt task table. |
| TC-V16-FAULT-007 | Kernel boot fault remains fatal | Kernel faults before scheduler policy can isolate a task. | Stable fatal kernel error or panic. |
| TC-V16-FAULT-008 | Scheduler internal invariant failure | Force invalid descriptor state if supported by test hook. | Stable fatal diagnostic, not undefined behavior. |
| TC-V16-FAULT-009 | Panic-on-task-fault mode if added | Enable documented policy. | First task fault panics kernel with stable code. |
| TC-V16-FAULT-010 | Multiple task faults summarized | Two tasks fault, one exits. | Final state records both faults and CLI status follows docs. |

### I. Timer and Sleep

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-TIMER-001 | Sleep from guest-created task | Task sleeps for N ticks. | Wake tick and reschedule point are correct. |
| TC-V16-TIMER-002 | Sleep zero ticks | Task calls sleep(0). | Behaves as yield or immediate return per docs. |
| TC-V16-TIMER-003 | Sleep max tick value | Use largest documented tick count. | No overflow; wake or rejection matches docs. |
| TC-V16-TIMER-004 | Timer tick with ready tasks | Timer enabled while tasks yield. | Tick count advances and scheduling remains deterministic. |
| TC-V16-TIMER-005 | Timer disabled sleep policy | Task sleeps with no timer. | Deadlock/idle behavior is stable. |
| TC-V16-TIMER-006 | Exception vector plus timer policy | Configure v1.4 exception vector and v1.6 tasks. | Timer routes through vector or scheduler according to docs. |
| TC-V16-TIMER-007 | Timer MMIO and kernel services coexist | Kernel configures timer through MMIO then uses sleep service. | Behavior matches documented precedence. |
| TC-V16-TIMER-008 | Timer trace readability | Run with trace. | Tick/scheduler lines are present but not noisy. |

### J. Memory Layout and Permissions

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-MEM-001 | Descriptor table readable | Kernel reads all descriptors. | Reads succeed and values are stable. |
| TC-V16-MEM-002 | Descriptor table write policy | Guest writes descriptor fields. | Write is blocked by read-only mapping; descriptors are refreshed from internal state. |
| TC-V16-MEM-003 | Task stacks are separated | Create many tasks and inspect stack ranges. | No overlap; each stack has documented size/alignment. |
| TC-V16-MEM-004 | Stack overflow boundary | Task writes below/above its stack. | Deterministic fault, no host crash. |
| TC-V16-MEM-005 | Kernel memory protected from task stack setup | Attempt to create stack over kernel memory. | Rejected. |
| TC-V16-MEM-006 | MMIO cannot be used as task memory | Use device range for stack or descriptor pointer. | Rejected. |
| TC-V16-MEM-007 | Boot-info remains stable after task creation | Create tasks then reread boot-info. | Boot-info fields remain unchanged except documented counters. |
| TC-V16-MEM-008 | Address arithmetic overflow | Use near-maximum addresses/ranges. | Rejected safely. |
| TC-V16-MEM-009 | Page-boundary stack | Stack starts/ends on mapping boundaries. | Boundary behavior matches v1.2 permissions. |
| TC-V16-MEM-010 | Unaligned descriptor access | Guest reads descriptor fields with unaligned access if supported. | Behavior matches existing memory model and docs. |

### K. CLI Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-CLI-001 | Help lists v1.6 options/services | Run help. | New flags or service docs are visible without overclaiming. |
| TC-V16-CLI-002 | Minimal Tiny OS demo succeeds | Run generated demo fixture. | Output matches deterministic demo transcript. |
| TC-V16-CLI-003 | Mixed host/guest task demo succeeds | Run fixture with `--kernel-task` plus guest-created task. | Output/order matches docs. |
| TC-V16-CLI-004 | Invalid task create exits stable | Run fixture that fails task creation intentionally. | Stable exit code/output per docs. |
| TC-V16-CLI-005 | Panic exit code stable | Run kernel panic path. | Exit code remains `70` unless docs intentionally change it. |
| TC-V16-CLI-006 | Task fault exit code stable | Run task fault path. | Exit code remains `71` unless docs intentionally change it. |
| TC-V16-CLI-007 | IPC demo output stable | Run mailbox ping-pong fixture. | Output is deterministic and concise. |
| TC-V16-CLI-008 | `info` is static | Run `info` on Tiny OS fixture. | Kernel/service/IPC metadata is shown, but guest-created tasks are not listed because `info` does not execute guest code. |
| TC-V16-CLI-009 | Trace shows create/switch/service events | Run trace. | Trace includes stable event labels and task IDs. |
| TC-V16-CLI-010 | Bad CLI combinations rejected | Combine incompatible flags. | Usage error and exit code are stable. |
| TC-V16-CLI-011 | Generated fixtures are optional-toolchain free | Run fixture generator with system Python only. | Fixtures are generated deterministically. |

### L. Debugger Behavior

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-DBG-001 | Existing debugger commands still work | Run v0.5/v1.2/v1.4/v1.5 debugger tests. | No regressions. |
| TC-V16-DBG-002 | `kernel` shows guest-created tasks | Start debug session and run Tiny OS fixture. | Output includes task IDs, states, entry/saved PC/SP, and counters. |
| TC-V16-DBG-003 | New `tasks` command if added | Invoke documented command. | Lists tasks in stable order. |
| TC-V16-DBG-004 | Inspect descriptor memory | Use memory/dump command on descriptor table. | Values match `kernel`/`tasks` output. |
| TC-V16-DBG-005 | Breakpoint before task create | Break in kernel before create service. | State shows no unexpected tasks. |
| TC-V16-DBG-006 | Breakpoint in task after create | Break at task entry. | Current task ID/state is correct. |
| TC-V16-DBG-007 | Breakpoints survive task switch | Set breakpoints in two tasks. | Each breakpoint hits with correct current task. |
| TC-V16-DBG-008 | Step over service call | Single-step through create/yield/service. | Debugger shows predictable PC/state changes. |
| TC-V16-DBG-009 | Debugger after task fault | Fault one task and stop. | Fault metadata visible; other tasks still inspectable. |
| TC-V16-DBG-010 | Debugger after IPC activity | Stop after send/receive/full/empty mailbox paths. | Mailbox counts and service status are visible; IPC blocking is not supported in v1.6. |
| TC-V16-DBG-011 | Malformed debugger command | Enter malformed v1.6 command. | Friendly error; debugger remains usable. |
| TC-V16-DBG-012 | Overlong debugger input | Send long line. | Input is rejected/truncated safely with no crash. |

### M. Documentation and Lesson Checks

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-DOC-001 | README links v1.6 plan and lesson | Grep README. | Links exist and titles match files. |
| TC-V16-DOC-002 | Lesson follows existing structure | Compare headings with v1.4/v1.5 style. | Lesson has learning ladder, new model, API, examples, mistakes, exercises. |
| TC-V16-DOC-003 | Docs do not overclaim real OS features | Search for unsupported terms. | Docs clearly say v1.6 is a toy lab, not production OS. |
| TC-V16-DOC-004 | CLI help matches docs | Compare documented flags/services to help output. | No stale flags or missing documented commands. |
| TC-V16-DOC-005 | Trap/service table complete | Parse docs for service names/IDs. | Every implemented service is documented exactly once. |
| TC-V16-DOC-006 | Exit-code table complete | Check README/lesson. | Panic, task fault, setup, usage, instruction-limit behavior documented. |
| TC-V16-DOC-007 | Example README commands work | Run documented example commands. | Commands succeed or skip with documented optional dependencies. |
| TC-V16-DOC-008 | Test plan maps to tests | Docs test checks required sections and critical case IDs. | v1.6 test files cover the strict implemented contract. |

### N. Fixture Generation and Examples

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-FIX-001 | Fixture generator creates all expected files | Run v1.6 generator. | Expected raw/ELF fixtures appear with deterministic bytes. |
| TC-V16-FIX-002 | Generator is deterministic | Run generator twice and compare hashes. | Hashes match. |
| TC-V16-FIX-003 | Generator rejects bad output path | Run with invalid path if option exists. | Friendly error; no partial corrupt output. |
| TC-V16-FIX-004 | Optional examples clean up | Run `make clean`. | Generated artifacts are removed. |
| TC-V16-FIX-005 | Optional example test skips missing optional toolchain | Run on base environment. | Test skips optional assembler/compiler paths instead of failing. |
| TC-V16-FIX-006 | Minimal guest-create example | Generate/run example. | Task created by guest kernel exits successfully. |
| TC-V16-FIX-007 | Mailbox example | Generate/run ping-pong example. | Output and task states are deterministic. |
| TC-V16-FIX-008 | Invalid-create example | Generate/run rejection example. | Error path is deterministic and documented. |

### O. Sanitizers, Fuzz-Like Negatives, and Release Hygiene

| ID | Test Case | Steps | Expected Result |
| --- | --- | --- | --- |
| TC-V16-SAN-001 | v1.6 unit tests under ASan | Build/run v1.6 C tests with AddressSanitizer. | No sanitizer findings. |
| TC-V16-SAN-002 | v1.6 CLI tests under ASan | Run key v1.6 fixtures with ASan build. | No sanitizer findings. |
| TC-V16-SAN-003 | v1.6 unit tests under UBSan | Build/run v1.6 C tests with UndefinedBehaviorSanitizer. | No undefined-behavior findings. |
| TC-V16-SAN-004 | Random bytes in kernel mode | Run random small binaries with `--kernel`. | Stable loader/decode errors; no host crash. |
| TC-V16-SAN-005 | Empty file in kernel mode | Run empty file. | Stable loader/setup error. |
| TC-V16-SAN-006 | One-byte/truncated instruction file | Run truncated raw binary. | Stable decode/fetch error. |
| TC-V16-SAN-007 | Malformed service arguments fuzz set | Generate invalid service calls. | Stable errors/faults; no host crash. |
| TC-V16-SAN-008 | Malformed descriptor values fuzz set | Corrupt writable descriptor fields if applicable. | Validation catches corruption safely. |
| TC-V16-SAN-009 | Clean tree after tests | Run release hygiene check. | No generated artifacts remain tracked/unignored. |
| TC-V16-SAN-010 | Fresh archive release check | Create archive and run release check inside it. | Archive is complete and self-consistent. |

## Readiness Checklist

v1.6 is ready only when all of the following are true:

- [ ] v0.1 through v1.5 regression tests pass unchanged.
- [x] Guest-created tasks work from at least raw fixtures.
- [x] Host-created and guest-created tasks can coexist or the incompatibility is
      explicitly rejected and documented.
- [x] Task creation validates entry, stack, flags, table capacity, overlap, and
      integer-overflow cases.
- [x] The scheduler handles ready, running, blocked, exited, and faulted
      guest-created tasks deterministically.
- [x] At least one kernel service path is documented and tested from both kernel
      boot context and task context.
- [x] If IPC/mailbox is included, send/receive success, empty/full, undersized receive, zero-length message, self-send, invalid
      pointer, invalid task, and ordering cases are tested.
- [x] Static `info`, trace, and debugger output expose v1.6 state according to their documented execution model.
- [x] Docs and lessons state the exact v1.6 contract and do not imply real OS
      privilege/security behavior.
- [x] Generated examples are reproducible and optional-example tests pass.
- [x] ASan/UBSan-focused v1.6 runs pass.
- [ ] Release docs, hygiene, clean, and archive checks pass.

## Risks and Regression Watchlist

- Accidentally changing v1.5 trap semantics while adding new services.
- Letting guest-writable descriptor state corrupt emulator invariants.
- Confusing kernel-service ABI errors with task faults or kernel panics.
- Ambiguous task-ID reuse rules causing nondeterministic tests.
- Overly verbose trace output hiding the control-flow story.
- Address-range validation bugs around stack overlap, MMIO, and integer wrap.
- Introducing optional toolchain requirements into the default test path.
- Documentation overclaiming real process isolation or real kernel behavior.

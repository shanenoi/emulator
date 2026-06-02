# Module Ownership and Header Boundaries

This project keeps `include/emulator.h` as the stable public umbrella include for
examples, tests, and small embedding programs. Internal implementation files
should prefer the narrow subsystem headers that describe the code they actually
use. That keeps refactors reviewable and prevents one public header from becoming
the dependency path for every subsystem.

## Public and internal includes

- Use `include/emulator.h` from public examples, black-box tests, and simple
  programs that embed the emulator.
- Use narrow headers from `src/*.c` files when possible, such as `cpu.h`,
  `memory.h`, `loader.h`, `mmio.h`, `devices.h`, `exceptions.h`, or
  `toy_kernel.h`.
- Use `include/emulator_internal.h` only for emulator-core coordination helpers
  shared between the orchestration modules.
- Use `include/loader_internal.h` only inside loader modules; format loaders
  should not expose parsing or mapping helpers through the public API.
- Use `include/emulator_guest.h` only from guest/demo code. It is the
  freestanding guest helper API, not part of the host-side emulator internals.

## Subsystem ownership

### CPU

Owned by `src/cpu.c`, `src/disasm.c`, `include/cpu.h`, and formatting helpers.
CPU owns register state, condition flags, instruction fetch/decode,
decoded instruction representation, decoded execution, instruction formatting,
and compatibility stepping through `cpu_step()`.

CPU code may call memory access APIs for instruction fetches and data accesses,
but it should not know device register behavior, loader format details, CLI
options, debugger command parsing, or toy-kernel scheduling policy.

### Memory

Owned by `src/memory.c` and `include/memory.h`. Memory owns RAM storage, bounds
checks, virtual-memory page metadata, page permissions, typed memory-fault
metadata, and coordination between plain RAM access and MMIO-routed access.

Memory may delegate MMIO address ranges to the MMIO subsystem. It should not own
individual device semantics, ELF/Mach-O parsing, CLI rendering, syscall policy,
or exception-vector behavior.

### MMIO and devices

Owned by `src/mmio.c`, `src/devices.c`, `include/mmio.h`, and
`include/devices.h`. MMIO owns address-routed device transactions, width/range
validation for device accesses, and routing to device handlers. Devices own
stateful peripheral behavior such as UART output, timer counters, deterministic
random reads, keyboard FIFO state, terminal/screen state, and frame pacing state.

Device helpers should not call loader code, debugger command parsing, or CLI
option parsing. Guest-visible device behavior should remain deterministic.

### Emulator orchestration

Owned by `src/emulator.c` and `include/emulator_internal.h`. The emulator core
owns top-level initialization, tracing hooks, one-instruction stepping,
run-loop coordination, and dispatch between normal CPU execution, fake syscalls,
exceptions, breakpoints, and toy-kernel paths.

`emulator_step()` should stay orchestration-focused: fetch/decode once, classify
the decoded instruction/event, then delegate to the appropriate subsystem.

### Exceptions

Owned by `src/exceptions.c` and `include/exceptions.h`. Exceptions own exception
classification, vector-entry behavior, exception return helpers, timer-interrupt
coordination, and halt/fault transitions related to exception handling.

Exception classification should depend on structured memory-fault metadata, not
user-facing error strings.

### Syscalls

Owned by `src/syscall.c`. Fake syscalls own the tiny documented `SVC #0` ABI,
including `write`, `exit`, register results, guest-memory reads for syscall
buffers, and deterministic error behavior. Fake syscall behavior is emulator
policy; it is not a host syscall pass-through layer.

### Toy kernel

Owned by `src/toy_kernel.c` and `include/toy_kernel.h`. The toy-kernel subsystem
owns opt-in kernel profile state, host-created and guest-created task handling,
task scheduling, task state transitions, task faults, service-trap dispatch,
mailbox IPC, and toy-kernel trace/debug metadata.

Toy-kernel code may use memory APIs for task/service data, but it should not own
CPU instruction semantics, loader format parsing, or generic CLI command parsing.

### Loader

Owned by `src/loader.c`, `src/loader_io.c`, `src/loader_map.c`,
`src/loader_stack.c`, `src/loader_raw.c`, `src/loader_elf.c`,
`src/loader_macho.c`, `include/loader.h`, and `include/loader_internal.h`.
The loader owns file reading, raw/ELF/Mach-O format validation and loading,
guest-image setup, mapping policy, segment metadata, initial entry point, and
stack mapping.

Format loaders should use shared mapping/stack helpers instead of duplicating
overlap, same-name merge, permission, metadata, or stack setup policy.

### CLI, debugger, and terminal UI

Owned by `src/main.c`, `src/cli_options.c`, `src/cli_run.c`,
`src/output_format.c`, `src/debugger.c`, `src/debugger_commands.c`, and
`src/terminal_ui.c`, with the matching narrow headers. `src/main.c` should stay a
thin entrypoint. CLI modules own option parsing, run-mode coordination, user
output formatting, terminal rendering, deterministic frame mode, and debugger
REPL command parsing/dispatch.

Debugger core state and stepping helpers stay in `src/debugger.c`; REPL command
syntax and aliases stay in `src/debugger_commands.c`.

## Adding future behavior

- Add new instructions in the CPU/disassembly area.
- Add RAM or page-permission behavior in memory.
- Add new device register behavior in MMIO/devices.
- Add new file-format loading behavior in loader format modules and shared
  mapping policy in `loader_map.c`.
- Add new fake syscall behavior in `src/syscall.c`.
- Add toy-kernel services, task state, or scheduling changes in `src/toy_kernel.c`.
- Add CLI flags in `src/cli_options.c`; add run-mode effects in `src/cli_run.c`;
  add debugger commands in `src/debugger_commands.c`; add terminal/frame display
  changes in `src/terminal_ui.c`.

When a change needs to cross subsystem boundaries, prefer adding a narrow helper
to the owning subsystem instead of reaching into another module's private state.
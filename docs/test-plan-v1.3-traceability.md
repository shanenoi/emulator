# v1.3 Test Traceability — Memory-Mapped Devices

This checklist maps every acceptance case in `docs/test-plan-v1.3.md` to either an executable test, a release target, or an explicit scope decision. It is intentionally separate from the plan so the plan can remain readable while the test suite stays auditable.

| Acceptance case | Coverage / decision |
| --- | --- |
| `TC-V13-BUILD-001` — Fresh build succeeds | Makefile `test`, `clean`, and regression targets; v1.3 scripts are wired into `make test`. |
| `TC-V13-BUILD-002` — Full deterministic suite succeeds | Makefile `test`, `clean`, and regression targets; v1.3 scripts are wired into `make test`. |
| `TC-V13-BUILD-003` — v1.2 memory behavior remains stable | Makefile `test`, `clean`, and regression targets; v1.3 scripts are wired into `make test`. |
| `TC-V13-BUILD-004` — Repeated tests are idempotent | Makefile `test`, `clean`, and regression targets; v1.3 scripts are wired into `make test`. |
| `TC-V13-BUILD-005` — `make clean` removes v1.3 artifacts | Makefile `test`, `clean`, and regression targets; v1.3 scripts are wired into `make test`. |
| `TC-V13-BUS-001` — RAM access still routes to RAM | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-002` — UART range routes to UART device | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-003` — Timer range routes to timer device | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-004` — Random range routes to random device | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-005` — Unmapped non-device address still faults | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-006` — Device ranges are matched by whole range, not only base | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-007` — Last byte inside device range is recognized | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-008` — First byte after device range is unmapped | `tests/v1_3/test_v1_3.c::test_device_map_and_routing`. |
| `TC-V13-BUS-009` — Device ranges reject overlap at registration | N/A for v1.3 fixed-device bus: no dynamic registration API exists; scope is documented as fixed UART/timer/random ranges. |
| `TC-V13-BUS-010` — Adjacent device ranges are allowed | N/A for v1.3 fixed-device bus: no dynamic registration API exists; scope is documented as fixed UART/timer/random ranges. |
| `TC-V13-BUS-011` — Device range overflow is rejected | N/A for v1.3 fixed-device bus: no dynamic registration API exists; scope is documented as fixed UART/timer/random ranges. |
| `TC-V13-BUS-012` — Zero-size device range is rejected | N/A for v1.3 fixed-device bus: no dynamic registration API exists; scope is documented as fixed UART/timer/random ranges. |
| `TC-V13-UART-001` — Single-byte UART hello | `tests/v1_3/test_cli_devices.sh` (`uart_hi.bin`) and `test_uart_device_policy`. |
| `TC-V13-UART-002` — UART output works without fake `write` syscall | `test_cli_devices.sh` (`uart_no_svc.bin`). |
| `TC-V13-UART-003` — Fake syscall output still works | Existing v0.7 syscall tests plus `uart_syscall_order.bin` in `test_cli_devices.sh`. |
| `TC-V13-UART-004` — UART and syscall output ordering is stable | `test_cli_devices.sh` (`uart_syscall_order.bin`). |
| `TC-V13-UART-005` — UART status register reports writable | `test_uart_device_policy` and `uart_status_read.bin` fixture generation. |
| `TC-V13-UART-006` — UART data read policy is enforced | `test_uart_device_policy` and debugger/CLI invalid-access checks. |
| `TC-V13-UART-007` — UART invalid offset faults | `test_cli_devices.sh` (`uart_bad_offset.bin`) and `test_uart_device_policy`. |
| `TC-V13-UART-008` — UART unsupported width is deterministic | `test_cli_devices.sh` (`uart_word_width.bin`, `uart_half_width.bin`) and `test_uart_device_policy`. |
| `TC-V13-UART-009` — UART NUL byte is preserved | `test_cli_devices.sh` (`uart_nul_high.bin`) and `test_uart_device_policy`. |
| `TC-V13-UART-010` — UART high-bit byte is preserved | `test_cli_devices.sh` (`uart_nul_high.bin`) and `test_uart_device_policy`. |
| `TC-V13-UART-011` — UART write fault is atomic | `test_cli_devices.sh` invalid/cross-device UART cases and `test_uart_device_policy` output-length checks. |
| `TC-V13-UART-012` — Large UART output remains deterministic | `test_cli_devices.sh` (`uart_large.bin`). |
| `TC-V13-TIMER-001` — Timer low register is readable | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-002` — Timer high register is readable | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-003` — Timer is monotonic under documented policy | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-004` — Timer resets if reset register is supported | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-005` — Timer read width policy is enforced | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-006` — Timer invalid offset faults | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-007` — Timer state resets between program loads | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-TIMER-008` — Timer does not advance during failed access if documented | `tests/v1_3/test_v1_3.c::test_timer_device_policy` plus `test_cli_devices.sh` timer fixtures. |
| `TC-V13-RNG-001` — Random value register is readable | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-RNG-002` — Consecutive random reads advance sequence | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-RNG-003` — Random seed write resets sequence if supported | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-RNG-004` — Random invalid offset faults | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-RNG-005` — Random unsupported width is deterministic | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-RNG-006` — Random state resets between runs | `tests/v1_3/test_v1_3.c::test_random_device_policy` plus `test_cli_devices.sh` random fixtures. |
| `TC-V13-EDGE-001` — Byte MMIO access at valid register succeeds | `test_uart_device_policy` and `test_cli_devices.sh` byte UART fixtures. |
| `TC-V13-EDGE-002` — Halfword MMIO policy is enforced | `test_uart_device_policy`; CLI `uart_half_width.bin` and `random_half.bin`. |
| `TC-V13-EDGE-003` — Word MMIO policy is enforced | `test_timer_device_policy`, `test_random_device_policy`, and `test_cpu_device_integration`. |
| `TC-V13-EDGE-004` — Doubleword MMIO policy is enforced | `test_uart_device_policy`, `test_timer_device_policy`, `test_random_device_policy`, and CPU doubleword failure checks. |
| `TC-V13-EDGE-005` — Unaligned MMIO access policy is enforced | `test_timer_device_policy`; CLI `timer_unaligned.bin` and `uart_cross_register.bin`. |
| `TC-V13-EDGE-006` — Access crossing register boundary is atomic | `test_cli_devices.sh` (`uart_cross_register.bin`) and `test_uart_device_policy`. |
| `TC-V13-EDGE-007` — Access crossing device boundary is atomic | `test_cli_devices.sh` (`edge_device_boundary.bin`) and `test_uart_device_policy`. |
| `TC-V13-EDGE-008` — Access crossing RAM-to-device boundary is rejected | N/A for the fixed v1.3 address map: devices live outside the 1 MiB RAM array and RAM mappings that overlap devices are rejected (`test_loader_device_boundaries`). |
| `TC-V13-EDGE-009` — Address plus width overflow is rejected | `test_device_map_and_routing` address-overflow check. |
| `TC-V13-EDGE-010` — Zero-width helper access policy is tested | `test_device_map_and_routing` zero-width helper check. |
| `TC-V13-EDGE-011` — Device access after halted program does not occur | `test_cpu_device_integration` and CLI `uart_after_hlt.bin`. |
| `TC-V13-EDGE-012` — Device access at instruction limit remains deterministic | `test_device_execution_and_limits`. |
| `TC-V13-CPU-001` — `STRB` can write UART data | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-002` — `LDR` can read timer word | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-003` — `LDRB` can read a byte-capable device register if supported | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-004` — Pre-index MMIO address update policy is correct | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-005` — Post-index MMIO address update policy is correct | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-006` — Pair load/store to device policy is enforced | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-CPU-007` — Device faults include current instruction context | `tests/v1_3/test_v1_3.c::test_cpu_device_integration` plus matching CLI fixtures for addressing modes and pair-store policy. |
| `TC-V13-LOAD-001` — Raw binary can access MMIO addresses | `test_cli_devices.sh` raw fixtures. |
| `TC-V13-LOAD-002` — ELF64 program can access MMIO addresses | `test_cli_devices.sh` (`uart_elf.elf`). |
| `TC-V13-LOAD-003` — Mach-O fixture can access MMIO addresses if included | Optional in plan; Mach-O parser remains covered by v1.1/v1.2 tests, no extra v1.3 Mach-O MMIO fixture is required. |
| `TC-V13-LOAD-004` — Loader rejects segments overlapping device ranges | `test_cli_devices.sh` (`overlap_device.elf`) and `test_loader_device_boundaries`. |
| `TC-V13-LOAD-005` — Entry point inside device range is rejected | `test_cli_devices.sh` (`entry_device.elf`) and `test_device_execution_and_limits` fetch-policy check. |
| `TC-V13-LOAD-006` — Program data may contain device addresses as constants | `test_cli_devices.sh` (`uart_constant.elf`). |
| `TC-V13-CLI-001` — `run` prints UART output | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-002` — `trace` preserves UART output and trace output policy | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-003` — `regs` can execute MMIO program | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-004` — `dump` device address policy is stable | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-005` — `info` shows device map | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-006` — Invalid device access has stable failure status | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-CLI-007` — Help text mentions device platform if public | `tests/v1_3/test_cli_devices.sh`. |
| `TC-V13-DBG-001` — Debugger can step over UART write | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-002` — Debugger `continue` preserves UART output | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-003` — Debugger `maps` lists devices | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-004` — Debugger `map <device-address>` identifies device | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-005` — Debugger `mem` device policy is stable | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-006` — Debugger reports invalid device access fault | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-007` — Breakpoints at device addresses are rejected or harmless | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-DBG-008` — Trace mode inside debugger includes MMIO instruction | `tests/v1_3/test_debugger_devices.sh`. |
| `TC-V13-FAULT-001` — Invalid device offset fault category exists | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-FAULT-002` — Unsupported device width fault category exists | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-FAULT-003` — Device read/write permission faults are distinct | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-FAULT-004` — Cross-target access faults before side effects | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-FAULT-005` — Faulting MMIO instruction does not advance hidden device state | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-FAULT-006` — Fault diagnostics do not leak host-specific text | `tests/v1_3/test_v1_3.c` policy tests plus CLI/debugger negative fixtures. |
| `TC-V13-DOC-001` — v1.3 test plan exists and is linked | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-DOC-002` — v1.3 lesson exists | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-DOC-003` — Device memory map is documented | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-DOC-004` — README current status is not stale | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-DOC-005` — Example README explains how to run fixtures | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-DOC-006` — Docs avoid overclaiming hardware accuracy | `tests/v1_3/test_docs_devices.sh`. |
| `TC-V13-REL-001` — Release check includes v1.3 | Makefile release targets (`release-docs-check`, `release-hygiene-check`, `release-clean-check`, `release-archive-check`, `release-check`). |
| `TC-V13-REL-002` — Fresh archive includes device source but not generated junk | Makefile release targets (`release-docs-check`, `release-hygiene-check`, `release-clean-check`, `release-archive-check`, `release-check`). |
| `TC-V13-REL-003` — Fresh archive test passes after extraction | Makefile release targets (`release-docs-check`, `release-hygiene-check`, `release-clean-check`, `release-archive-check`, `release-check`). |
| `TC-V13-REL-004` — Sanitizer checks pass or skip clearly | Manual release targets: `make test-asan`, `make test-ubsan`, and `make test-cc-matrix`; they skip clearly when a toolchain is unavailable. |
| `TC-V13-REL-005` — Compiler matrix remains healthy | Manual release target: `make test-cc-matrix`; skips clearly when alternate compilers are unavailable. |
| `TC-V13-REL-006` — Scope discipline is preserved | Makefile release targets (`release-docs-check`, `release-hygiene-check`, `release-clean-check`, `release-archive-check`, `release-check`). |

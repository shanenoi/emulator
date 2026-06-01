# v1.12 Test Plan — Practical ARM64 Instruction Coverage for Freestanding Guest Demos

## Goal

Expand the emulator's targeted ARM64 subset so small freestanding C guest programs compiled against `include/emulator_guest.h` can use more normal compiler-emitted integer, branch, addressing, and memory patterns.

This milestone is intentionally not a full ARM64 ISA implementation. It focuses on high-value instructions that show up in tiny no-libc demos, teaching programs, and deterministic interactive examples.

## Scope

- Keep instruction decode and execution inside the CPU layer.
- Preserve the existing device, keyboard, terminal, frame, random, UART, and guest-helper behavior.
- Improve unsupported-instruction diagnostics with PC and raw opcode context.
- Add focused coverage for compiler-common ARM64 instruction families:
  - `TBZ` and `TBNZ` test-bit branches.
  - `BR` and `BLR` indirect branch/call instructions.
  - Logical immediate `AND`, `ORR`, `EOR`, and `ANDS`/`TST` forms.
  - Conditional select family: `CSEL`, `CSINC`, `CSINV`, and `CSNEG`.
  - Extended-register `ADD` and `SUB` with unsigned/signed byte, halfword, word, and doubleword extensions.
  - Literal loads including `LDR` and `LDRSW` literal.
  - Register-offset `LDR` and `STR` forms.
  - Sign-extending loads: `LDRSB`, `LDRSH`, and `LDRSW`.
  - Multiply-add/subtract families: `MADD`, `MSUB`, `SMADDL`, `SMSUBL`, `UMADDL`, and `UMSUBL`, including practical aliases such as `MUL`, `SMULL`, and `UMULL`.
  - Useful bitfield/sign-extension aliases such as `UBFX`, `SBFX`, `BFI`, `BFXIL`, `UXTB`, `UXTH`, and `SXT[B/H/W]` where they naturally fit the implemented bitfield decoder.

## Out of Scope

- Full ARM64 ISA coverage.
- Floating-point, SIMD, or NEON.
- Atomics or exclusive memory instructions.
- Exception unwind ABI support.
- libc, hosted startup, `printf`, `malloc`, argv/envp, or dynamic linking.
- OS process model, MMU, interrupts, or platform devices unrelated to instruction execution.
- Demo/game-specific CPU behavior.

## Required Tests

1. Unit/decode/execute tests cover each implemented instruction group.
2. `TBZ` and `TBNZ` include taken and not-taken execution cases.
3. `BR` and `BLR` update `pc` and `lr` correctly and reject invalid targets through existing runtime-error paths.
4. Logical immediates produce expected values, and flag-setting forms update NZCV consistently with existing logical-flag behavior.
5. Conditional select chooses the correct source or transformed value based on NZCV conditions.
6. Extended-register arithmetic covers unsigned and signed extensions.
7. Register-offset load/store addressing computes expected addresses and uses existing memory access helpers.
8. Sign-extending loads produce correct signed 32-bit and 64-bit values.
9. Multiply-add/subtract forms produce expected results.
10. Bitfield/sign-extension aliases produce expected extracted, inserted, zero-extended, and sign-extended values.
11. Freestanding guest C integration tests compile small programs against `include/emulator_guest.h` when the local AArch64 toolchain is available.
12. Integration tests exercise arrays, structs, signed char/short loads, conditionals, switch-like branching, screen drawing, keyboard polling, and deterministic frame observation with observable UART or screen output.
13. Existing keyboard, terminal, frame, interactive validation, and guest-helper tests remain deterministic and do not require a real TTY.

## Freestanding Compiler Profile

The deterministic guest C integration tests use this profile when `clang` and `ld.lld` are available:

```sh
clang --target=aarch64-none-elf \
  -Iinclude \
  -ffreestanding \
  -nostdlib \
  -fno-builtin \
  -fno-stack-protector \
  -fno-pic \
  -fno-pie \
  -mgeneral-regs-only \
  -O2
```

`-mgeneral-regs-only` is part of the supported profile because floating-point/SIMD remains out of scope.

## Determinism Policy

Normal `emulator run` remains deterministic and instruction-limited as before. Scripted input, final screen dumps, and `--frames` remain the deterministic test path for guest programs. Interactive mode stays host-paced for humans and is not required for v1.12 automated tests.

Unsupported instructions should continue to fail clearly and include at least the instruction PC and raw opcode so the next coverage pass can be driven by observable compiler output.

# v1.4 Examples — Exceptions, Traps, and Interrupt Skeleton

v1.4 examples will demonstrate the opt-in teaching exception model introduced by
`lessons/v1.4-exceptions-and-interrupts.md`.

Planned fixtures:

- handled `BRK #imm` trap,
- handled invalid instruction,
- handled read/write/fetch fault,
- handled device fault,
- simplified `ERET` return,
- unhandled exception when no vector is configured,
- deterministic queued timer interrupt,
- masked timer interrupt,
- double-fault/nested-exception rejection.

Tests and generated binaries are intentionally not added yet in this development
slice. The current code exposes the C API and decoder/runtime hooks needed to
build those fixtures next.

# v1.10 Test Plan — Deterministic Frame Pacing

Goal: add a runner-paced guest-visible frame tick for interactive demos and teaching programs without making normal runs depend on wall-clock time.

Scope:

- Add a deterministic frame MMIO device at `0x09060000`.
- Expose frame status, 64-bit frame counter, and ready-clear control registers.
- Add a runner helper that advances the frame counter exactly once and sets ready status.
- Add deterministic non-interactive `run --frames <N> --instructions-per-frame <N>` mode.
- Integrate the same frame advancement helper into `--interactive` once per host frame.
- Add freestanding guest helper constants/functions in `include/emulator_guest.h`.

Register behavior:

- `FRAME_STATUS` bit 0 reports frame-ready status.
- `FRAME_COUNTER_LO` and `FRAME_COUNTER_HI` expose the monotonic unsigned 64-bit counter.
- `FRAME_CONTROL` bit 0 clears ready status without changing the counter.
- The counter starts at 0 and increments once per runner-advanced frame.

CLI behavior:

- `--frames <N>` is valid only for non-interactive `run`.
- `--frames 0` and non-numeric values are rejected.
- `--frames` cannot be combined with `--interactive`.
- `--fps` remains interactive-only.
- `--instructions-per-frame` is valid for both deterministic frame mode and interactive mode.

Required checks:

- Initial counter/status state.
- Single and repeated frame advancement.
- Control clear-ready behavior.
- Guest helper constants compile against public constants.
- CLI frame mode lets a guest observe frame readiness/counter through MMIO.
- CLI validation rejects invalid frame/timing combinations.
- Keyboard, terminal, and interactive validation regressions continue to pass.

# v1.9 Test Plan — Optional Interactive Host Runner

v1.9 adds an optional host-interactive runner for `emulator run`. The CPU still only observes guest-visible devices: host keyboard bytes are normalized and queued into the existing keyboard FIFO, and live output is rendered from the existing terminal/screen MMIO buffer. Normal non-interactive runs, scripted `--input`, and `--screen-dump` remain deterministic test paths.

The interactive CLI options are:

- `--interactive` enables the host event loop for `run` only.
- `--fps <N>` configures redraw/frame pacing. The default is 30 and zero is rejected.
- `--instructions-per-frame <N>` configures how many guest instructions execute per interactive frame. The default is 1000 and zero is rejected.
- `--quit-key <ctrl-c|esc>` selects a host-only quit key. The default is `ctrl-c`; this key is not injected into the guest.

Interactive mode requires both stdin and stdout to be TTYs. When enabled, the host terminal is switched to noncanonical/no-echo input with nonblocking polling and restored before returning from the interactive runner. If stdin/stdout are not TTYs, the CLI fails clearly instead of silently treating test input as guest keyboard input.

Arrow-key escape sequences are normalized by the host before queueing guest input:

| Host key | Queued byte |
| --- | ---: |
| Up | `0x80` |
| Down | `0x81` |
| Left | `0x82` |
| Right | `0x83` |

Guest programs continue to poll `KBD_STATUS` and read `KBD_DATA`. The guest helper header exposes `EMU_GUEST_KEY_ESC`, `EMU_GUEST_KEY_UP`, `EMU_GUEST_KEY_DOWN`, `EMU_GUEST_KEY_LEFT`, and `EMU_GUEST_KEY_RIGHT` constants.

## Coverage

- CLI tests reject invalid `--fps` values.
- CLI tests reject invalid `--instructions-per-frame` values.
- CLI tests reject invalid `--quit-key` values.
- CLI tests reject `--interactive` for non-`run` commands.
- CLI tests reject `--interactive` when stdin/stdout are not TTYs.
- Guest-helper constant tests verify stable arrow/escape key constants compile and keep their documented values.
- Existing scripted keyboard and terminal tests continue to cover the deterministic non-interactive paths.

# v1.8 Test Plan — Deterministic Terminal Screen Device

v1.8 adds a guest-visible terminal/screen MMIO device for structured text output. UART remains a byte stream; the terminal device maintains a deterministic screen buffer that can be inspected by tests or rendered by the CLI after guest execution.

The terminal MMIO range starts at `0x09050000` and uses the same fixed 4 KiB device range style as the UART, timer, random, exception-controller, and keyboard devices.

| Register | Offset | Access | Behavior |
| --- | ---: | --- | --- |
| `TERM_STATUS` | `0x00` | read 32-bit | bit 0 means the screen content or cursor changed |
| `TERM_WIDTH` | `0x04` | read 32-bit | current configured width |
| `TERM_HEIGHT` | `0x08` | read 32-bit | current configured height |
| `TERM_CURSOR_X` | `0x0c` | read/write 32-bit | cursor column; writes clamp to valid bounds |
| `TERM_CURSOR_Y` | `0x10` | read/write 32-bit | cursor row; writes clamp to valid bounds |
| `TERM_DATA` | `0x14` | write byte or 32-bit | writes the low byte at the cursor and advances |
| `TERM_CONTROL` | `0x18` | write 32-bit | bit 0 clears the screen and homes cursor; bit 1 homes cursor; bit 2 clears dirty status |
| `TERM_INDEX` | `0x20` | read/write 32-bit | linear cell index, `y * width + x` |
| `TERM_CELL` | `0x24` | read/write byte or 32-bit | reads/writes the byte at `TERM_INDEX`; out-of-range reads return `0` and writes are ignored |

Screen cells initialize to spaces. Newline moves the cursor to column 0 of the next row, carriage return moves to column 0, writing past the right edge wraps to the next row, and moving past the bottom scrolls up by one row.

The CLI exposes:

- `--screen-size <WIDTH>x<HEIGHT>` to configure the terminal dimensions before execution. The default is 80x25. Valid bounds are width 1..160 and height 1..100.
- `--screen-dump` to print the final terminal buffer after successful guest execution.
- `--screen-border <unicode|ascii|none>` to choose the host-only renderer border. The default is `unicode`.

## Coverage

- Unit tests verify default dimensions, space-initialized cells, and cursor origin.
- Unit tests verify `TERM_DATA` writes, cursor advance, and dirty status.
- Unit tests verify newline, carriage return, wrapping, and deterministic scrolling.
- Unit tests verify clear, home, and clear-dirty control bits.
- Unit tests verify direct cell access through `TERM_INDEX` and `TERM_CELL`.
- Unit tests verify cursor clamping and out-of-range cell access policy.
- CLI tests verify valid and invalid `--screen-size` parsing.
- CLI tests verify guest-visible width/height from CLI configuration.
- CLI tests verify exact final screen dumps for unicode, ASCII, and no-border rendering.

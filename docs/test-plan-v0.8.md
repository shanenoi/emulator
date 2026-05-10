# v0.8 Test Plan — ELF64 Loader

## Version Goal

v0.8 lets the emulator load a deliberately simple **AArch64 ELF64 executable** in addition to the raw `.bin` files supported by v0.1 through v0.7.

Before v0.8, the emulator only understands a flat byte stream loaded at `EMU_LOAD_ADDRESS`. That is useful for learning instructions, memory, debugging, and fake syscalls, but it is not how normal compiled programs are packaged.

v0.8 adds the first real executable-file milestone:

- detect ELF files by magic bytes,
- validate the ELF header,
- validate program headers,
- load `PT_LOAD` segments into emulator memory,
- zero-fill segment memory beyond file bytes,
- initialize `pc` from the ELF entry point,
- initialize `sp` to the emulator stack top,
- keep all existing raw-binary behavior working.

This version is still **not** a Linux process emulator. It does not run normal dynamically linked Linux programs. It should run small freestanding static AArch64 ELF examples that use only instructions and fake syscalls already supported by the emulator.

## Scope

### In Scope

- v0.1 through v0.7 regression tests must continue to pass.
- Existing CLI commands must accept both raw binaries and supported ELF files:

```sh
./emulator run <program>
./emulator trace <program>
./emulator regs <program>
./emulator dump <program> <address> <length>
./emulator debug <program>
```

- Keep raw `.bin` loading backward compatible.
- Add ELF detection using the first four bytes:

```text
7f 45 4c 46  # \x7f E L F
```

- Add ELF64 header parsing for little-endian AArch64 executable files.
- Validate at least:
  - ELF magic,
  - class is ELF64,
  - data encoding is little-endian,
  - ELF version is current,
  - type is supported executable type,
  - machine is AArch64,
  - header size is sane,
  - program header entry size is sane,
  - program header table range is inside the file,
  - entry point is inside a loaded executable segment or otherwise inside mapped memory.
- Support static `ET_EXEC` first.
- Either reject `ET_DYN`/PIE clearly or support it only with a documented fixed load bias. The recommended v0.8 decision is: reject `ET_DYN` with a clear unsupported-feature error.
- Load `PT_LOAD` segments into emulator memory.
- Enforce safe segment bounds:
  - `p_filesz <= p_memsz`,
  - file range is inside the input file,
  - memory range is inside emulator memory,
  - no unsigned arithmetic overflow,
  - no overlapping loaded memory ranges unless a documented policy exists. The recommended v0.8 decision is: reject overlaps.
- Zero-fill `.bss` bytes where `p_memsz > p_filesz`.
- Represent segment permissions internally, even if execute/write/read permissions are not enforced yet.
- Reject unsupported dynamic-linking features clearly, especially `PT_INTERP`.
- Add v0.8 examples and documentation.
- Add unit/integration/CLI tests for normal behavior, invalid inputs, edge cases, and regressions.

### Out of Scope

- Dynamic linker support.
- Running normal Linux distro binaries.
- `libc` startup support.
- `argv`, `argc`, environment variables, and auxiliary vector.
- File descriptor table beyond v0.7 fake stdout/stderr behavior.
- Real Linux syscall compatibility beyond the tiny documented fake subset.
- Relocations.
- Symbol resolution.
- DWARF debug information.
- Source-level debugging.
- Section-header based loading.
- Demand paging.
- MMU/page tables.
- Permission enforcement, unless kept very small and documented.
- ASLR.
- Shared libraries.
- Thread-local storage.
- Signals.
- Kernel emulation.
- New CPU instructions unless strictly needed by the chosen v0.8 examples and explicitly documented.

## Implementation Assumptions

These assumptions should become implementation decisions before tests are finalized.

1. The input loader should select ELF or raw mode automatically by file magic.
2. Non-ELF files continue through the existing raw loader path.
3. ELF files are little-endian ELF64 only.
4. `e_machine` must be AArch64.
5. The first supported ELF type is `ET_EXEC`.
6. `ET_DYN` is rejected in v0.8 unless the implementation explicitly introduces a stable load-bias policy.
7. The loader uses program headers, not section headers, to load memory.
8. Only `PT_LOAD` segments are copied into emulator memory.
9. `PT_INTERP` is rejected because it implies dynamic linking.
10. Unknown non-load program header types are ignored unless they imply unsupported runtime behavior.
11. `p_vaddr` is treated as the guest memory address in the emulator's flat memory model.
12. v0.8 examples should be linked so all loaded virtual addresses fit inside `EMU_MEMORY_SIZE`.
13. The initial `pc` becomes the ELF header's `e_entry`.
14. The initial `sp` remains the top of emulator memory unless a later version adds a real process stack layout.
15. `HLT` and v0.7 fake syscalls remain valid ways to stop an ELF program.
16. Segment permissions are recorded for inspection/future use but are not enforced in v0.8 unless explicitly documented.
17. Tests should not require a cross compiler to be installed. Unit/CLI tests may generate tiny ELF files directly from byte arrays.
18. Documentation may show an optional `aarch64-linux-gnu-as`/`ld` or `gcc -nostdlib -static` command, but automated tests should have a fallback that constructs minimal ELF fixtures directly.

Recommended constants:

```c
#define EMU_ELF_MAGIC0 0x7f
#define EMU_ELF_MAGIC1 'E'
#define EMU_ELF_MAGIC2 'L'
#define EMU_ELF_MAGIC3 'F'
#define EMU_ELF_CLASS_64 2
#define EMU_ELF_DATA_LSB 1
#define EMU_ELF_VERSION_CURRENT 1
#define EMU_ELF_ET_EXEC 2
#define EMU_ELF_ET_DYN 3
#define EMU_ELF_EM_AARCH64 183
#define EMU_ELF_PT_LOAD 1
#define EMU_ELF_PT_INTERP 3
```

Recommended public loader shape:

```c
typedef enum {
    EMU_PROGRAM_RAW = 0,
    EMU_PROGRAM_ELF64,
} EmuProgramFormat;

typedef struct {
    EmuProgramFormat format;
    uint64_t entry;
    uint64_t stack_pointer;
    size_t segment_count;
} EmuLoadedProgram;

bool load_program(Emulator *emu, const char *path, EmuLoadedProgram *program,
                  char *error, size_t error_size);
```

Acceptable alternative: keep separate raw/ELF functions internally, as long as CLI/debugger behavior exercises the same public program-loading behavior.

## Required Test Artifacts

Suggested new files:

```text
docs/test-plan-v0.8.md
examples/v0_8/hello_elf.s
examples/v0_8/exit_status_elf.s
examples/v0_8/bss_elf.s
examples/v0_8/linker.ld
tests/v0_8/test_v0_8.c
tests/v0_8/test_cli_elf.sh
```

Suggested implementation files, if useful:

```text
src/elf_loader.c
src/program_loader.c
include/emulator.h
src/main.c
src/debugger.c
Makefile
README.md
examples/README.md
lessons/v0.8-elf-loader.md
```

## Test Data Strategy

Tests should create small ELF files programmatically instead of depending on a system cross toolchain.

Recommended fixture helper:

```c
write_elf64_fixture(path, options)
```

The helper should be able to vary:

- `e_ident` fields,
- `e_type`,
- `e_machine`,
- `e_entry`,
- `e_phoff`,
- `e_phentsize`,
- `e_phnum`,
- program header type,
- `p_offset`,
- `p_vaddr`,
- `p_filesz`,
- `p_memsz`,
- `p_flags`,
- segment file bytes,
- extra padding/truncation.

Recommended happy-path fixture:

```text
ELF header at file offset 0
Program header table immediately after ELF header
One PT_LOAD segment at file offset 0x100
p_vaddr = 0x1000
p_filesz = code_size
p_memsz = code_size
p_flags = R | X
e_entry = 0x1000
segment bytes = supported raw instructions
```

For syscall examples, the fixture can contain two `PT_LOAD` segments:

```text
text segment: 0x1000, R|X, contains instructions
data segment: 0x2000, R|W, contains message bytes and optional bss
entry: 0x1000
```

## Test Case Index

- Header/detection tests: `TC-V08-HDR-*`
- Program-header tests: `TC-V08-PH-*`
- Segment-loading tests: `TC-V08-SEG-*`
- Entry/stack tests: `TC-V08-ENTRY-*`
- Execution/syscall tests: `TC-V08-EXEC-*`
- CLI tests: `TC-V08-CLI-*`
- Debugger tests: `TC-V08-DBG-*`
- Error-message tests: `TC-V08-ERR-*`
- Regression tests: `TC-V08-REG-*`
- Documentation/tests examples: `TC-V08-DOC-*`
- Acceptance tests: `TC-V08-ACC-*`

---

# Header and Format Detection Tests

## TC-V08-HDR-001 — Non-ELF input still loads as raw binary

**Purpose:** preserve v0.1 through v0.7 raw-binary workflow.

**Setup:** create a normal raw binary containing `MOVZ X0, #1; HLT`.

**Steps:**

1. Load through the new program loader or CLI.
2. Run to halt.

**Expected:**

- Loader selects raw format.
- `pc` starts at `EMU_LOAD_ADDRESS`.
- Program executes exactly as before.
- `x0 == 1`.
- No ELF validation error appears.

## TC-V08-HDR-002 — Valid ELF magic selects ELF loader

**Purpose:** ensure format detection is magic-based.

**Setup:** create a minimal valid ELF64 AArch64 executable.

**Steps:** load the file.

**Expected:**

- Loader selects ELF64 format.
- Raw loader is not used.
- `pc` is initialized from `e_entry`, not forced to `EMU_LOAD_ADDRESS` unless entry happens to equal it.

## TC-V08-HDR-003 — Truncated file shorter than ELF magic

**Purpose:** reject or raw-load tiny files consistently.

**Setup:** create files of length 1, 2, and 3 bytes.

**Expected:**

- If the bytes do not equal the full ELF magic, files follow raw loader rules.
- Existing raw loader behavior applies.
- No out-of-bounds header read occurs.

## TC-V08-HDR-004 — ELF magic but truncated ELF header

**Purpose:** reject malformed ELF without reading beyond file.

**Setup:** create a file starting with `\x7fELF` but shorter than `sizeof(Elf64_Ehdr)`.

**Expected:**

- Loader fails.
- CLI exits non-zero.
- Error mentions ELF header is truncated or incomplete.
- No partial segment data is loaded.

## TC-V08-HDR-005 — Wrong ELF class is rejected

**Purpose:** v0.8 only supports ELF64.

**Setup:** valid magic but `EI_CLASS = ELFCLASS32`.

**Expected:**

- Loader fails.
- Error mentions unsupported ELF class or expected ELF64.

## TC-V08-HDR-006 — Big-endian ELF is rejected

**Purpose:** v0.8 only supports little-endian files.

**Setup:** `EI_DATA = ELFDATA2MSB`.

**Expected:**

- Loader fails.
- Error mentions unsupported endian/data encoding.

## TC-V08-HDR-007 — Invalid ELF version is rejected

**Purpose:** reject unknown ELF versions.

**Setup:** set `EI_VERSION` or `e_version` to a value other than current.

**Expected:**

- Loader fails.
- Error mentions unsupported ELF version.

## TC-V08-HDR-008 — Wrong machine is rejected

**Purpose:** prevent loading x86, x86_64, ARM32, or arbitrary ELF files.

**Setup:** valid ELF64 little-endian header with `e_machine != EM_AARCH64`.

**Expected:**

- Loader fails.
- Error mentions expected AArch64 or unsupported machine.

## TC-V08-HDR-009 — Unsupported ELF type is rejected

**Purpose:** support only documented file types.

**Setup:** create ELF files with `ET_REL`, `ET_CORE`, and unknown type.

**Expected:**

- Loader fails.
- Error mentions unsupported ELF type.

## TC-V08-HDR-010 — ET_DYN/PIE is rejected clearly

**Purpose:** avoid accidental unsupported load-bias behavior.

**Setup:** `e_type = ET_DYN` with otherwise plausible program headers.

**Expected:**

- Loader fails.
- Error mentions PIE, shared object, or ET_DYN unsupported in v0.8.

## TC-V08-HDR-011 — Header size too small is rejected

**Purpose:** validate `e_ehsize`.

**Setup:** set `e_ehsize` less than `sizeof(Elf64_Ehdr)`.

**Expected:**

- Loader fails.
- Error mentions ELF header size.

## TC-V08-HDR-012 — Header size larger than file is rejected

**Purpose:** avoid trusting impossible header metadata.

**Setup:** `e_ehsize` larger than file size.

**Expected:** loader fails with a header-size/range error.

## TC-V08-HDR-013 — Program header offset zero with nonzero count

**Purpose:** catch invalid program-header table location.

**Setup:** `e_phoff = 0`, `e_phnum > 0`.

**Expected:** loader fails with program-header table error.

## TC-V08-HDR-014 — Program header count zero

**Purpose:** an executable with no loadable segments cannot run.

**Setup:** `e_phnum = 0`.

**Expected:**

- Loader fails.
- Error mentions missing program headers or no loadable segments.

## TC-V08-HDR-015 — Program header entry size mismatch

**Purpose:** validate `e_phentsize`.

**Setup:** set `e_phentsize` smaller or larger than `sizeof(Elf64_Phdr)`.

**Expected:** loader fails with program-header entry-size error.

## TC-V08-HDR-016 — Program header table outside file

**Purpose:** range-check table offset and size.

**Setup:** choose `e_phoff` and `e_phnum` so `e_phoff + e_phnum * e_phentsize` exceeds file length.

**Expected:** loader fails without reading past file end.

## TC-V08-HDR-017 — Program header table range integer overflow

**Purpose:** catch unsigned overflow.

**Setup:** set `e_phoff` near `UINT64_MAX` and nonzero `e_phnum`.

**Expected:** loader fails with program-header range/overflow error.

---

# Program Header Tests

## TC-V08-PH-001 — Single PT_LOAD program header loads

**Purpose:** minimal successful program-header path.

**Setup:** one `PT_LOAD` segment containing `MOVZ X0, #7; HLT`.

**Expected:**

- Segment bytes appear at `p_vaddr`.
- Program starts at `e_entry`.
- Program halts with `x0 == 7`.

## TC-V08-PH-002 — Multiple PT_LOAD segments load

**Purpose:** support separate text/data layout.

**Setup:** two `PT_LOAD` segments:

- text at `0x1000`, R|X,
- data at `0x2000`, R|W.

**Expected:**

- Both memory ranges are loaded.
- Text can read/write data using supported instructions or syscall arguments.
- Segment metadata records both ranges.

## TC-V08-PH-003 — Non-PT_LOAD headers are ignored when harmless

**Purpose:** tolerate common metadata headers.

**Setup:** include a harmless `PT_NOTE` or unknown non-load header plus a valid `PT_LOAD`.

**Expected:**

- Loader ignores the harmless header.
- Program runs normally.

## TC-V08-PH-004 — PT_INTERP is rejected

**Purpose:** dynamic linking is out of scope.

**Setup:** add `PT_INTERP` pointing to `/lib/ld-linux-aarch64.so.1`.

**Expected:**

- Loader fails.
- Error mentions dynamic linker, interpreter, or `PT_INTERP` unsupported.

## TC-V08-PH-005 — p_filesz greater than p_memsz is rejected

**Purpose:** invalid ELF segment shape.

**Setup:** `p_filesz = 16`, `p_memsz = 8`.

**Expected:** loader fails with segment size error.

## TC-V08-PH-006 — Segment file offset outside file is rejected

**Purpose:** validate `p_offset`.

**Setup:** `p_offset` beyond file length.

**Expected:** loader fails with segment file range error.

## TC-V08-PH-007 — Segment file range outside file is rejected

**Purpose:** validate `p_offset + p_filesz`.

**Setup:** `p_offset` inside file but `p_offset + p_filesz` exceeds file length.

**Expected:** loader fails with segment file range error.

## TC-V08-PH-008 — Segment file range integer overflow is rejected

**Purpose:** catch unsigned wraparound.

**Setup:** `p_offset = UINT64_MAX - 4`, `p_filesz = 16`.

**Expected:** loader fails with segment range/overflow error.

## TC-V08-PH-009 — Segment memory range outside emulator memory is rejected

**Purpose:** keep flat memory safe.

**Setup:** `p_vaddr + p_memsz > EMU_MEMORY_SIZE`.

**Expected:** loader fails with memory range error.

## TC-V08-PH-010 — Segment memory range integer overflow is rejected

**Purpose:** catch `p_vaddr + p_memsz` wraparound.

**Setup:** `p_vaddr = UINT64_MAX - 8`, `p_memsz = 32`.

**Expected:** loader fails with segment memory overflow error.

## TC-V08-PH-011 — Zero-size PT_LOAD segment behavior is documented

**Purpose:** avoid ambiguous edge behavior.

**Setup:** `PT_LOAD` with `p_filesz = 0` and `p_memsz = 0`.

**Expected:**

- Preferred behavior: ignore zero-size load segment.
- Loader still requires at least one non-empty loadable segment containing the entry point.
- Behavior is documented and tested.

## TC-V08-PH-012 — File-empty but memory-nonempty segment zero-fills

**Purpose:** support pure `.bss` segment.

**Setup:** `p_filesz = 0`, `p_memsz = 32`, valid memory range.

**Expected:**

- Loader writes zero bytes into that memory range.
- Segment is represented in metadata.
- If entry is not in this segment, program may still run from another text segment.

## TC-V08-PH-013 — Overlapping PT_LOAD segments are rejected

**Purpose:** deterministic memory mapping.

**Setup:** two `PT_LOAD` segments whose guest memory ranges overlap.

**Expected:**

- Loader fails.
- Error mentions overlapping segments.
- Later segment does not silently overwrite earlier segment.

## TC-V08-PH-014 — Adjacent PT_LOAD segments are accepted

**Purpose:** distinguish overlap from touching boundaries.

**Setup:** segment A range `[0x1000, 0x1100)`, segment B range `[0x1100, 0x1200)`.

**Expected:** loader accepts both.

## TC-V08-PH-015 — Misaligned segment address policy is documented

**Purpose:** avoid hidden assumptions about `p_align`.

**Setup:** `p_vaddr` not aligned to `p_align` or not instruction-aligned for text.

**Expected:**

- Recommended behavior: reject text entry if `e_entry` is not 4-byte aligned.
- Segment alignment itself may be ignored in v0.8 if documented.
- Tests assert the chosen policy.

## TC-V08-PH-016 — p_align zero and one are accepted

**Purpose:** ELF allows no alignment constraint.

**Setup:** valid segment with `p_align = 0` and another with `p_align = 1`.

**Expected:** loader accepts if all other ranges are valid.

## TC-V08-PH-017 — Segment permission flags are recorded

**Purpose:** satisfy v0.8 definition of done without enforcing permissions.

**Setup:** load text R|X and data R|W segments.

**Expected:**

- Internal segment metadata records readable/writable/executable flags or equivalent.
- Tests can inspect metadata directly or through a helper.
- Execution still succeeds even if permissions are not enforced.

---

# Segment Loading and Memory Tests

## TC-V08-SEG-001 — Text bytes copied exactly

**Purpose:** verify no byte corruption or endian confusion.

**Setup:** text segment contains known instruction words and padding bytes.

**Expected:** memory at `p_vaddr` exactly matches file bytes.

## TC-V08-SEG-002 — Data bytes copied exactly

**Purpose:** validate non-executable segment loading.

**Setup:** data segment contains ASCII message bytes.

**Expected:** memory at `p_vaddr` contains the same message bytes.

## TC-V08-SEG-003 — BSS bytes are zero-filled

**Purpose:** implement `p_memsz > p_filesz`.

**Setup:** data segment file bytes contain `abc`, `p_memsz` extends 16 bytes.

**Expected:**

- First three bytes are `abc`.
- Remaining thirteen bytes are zero.

## TC-V08-SEG-004 — BSS zero-fill overwrites prior memory contents

**Purpose:** avoid relying on initially zeroed memory only.

**Setup:** pre-fill emulator memory at target range with nonzero pattern, then load ELF with bss range.

**Expected:** bss region becomes zero.

## TC-V08-SEG-005 — Segment at low memory address loads

**Purpose:** support valid low guest addresses.

**Setup:** `p_vaddr = 0x100` and entry there.

**Expected:** loader accepts if the address is within memory and instruction-aligned.

## TC-V08-SEG-006 — Segment ending exactly at memory size loads

**Purpose:** boundary success case.

**Setup:** `p_vaddr + p_memsz == EMU_MEMORY_SIZE`.

**Expected:** loader accepts and writes exactly through the final byte.

## TC-V08-SEG-007 — Segment one byte past memory size fails

**Purpose:** boundary failure case.

**Setup:** `p_vaddr + p_memsz == EMU_MEMORY_SIZE + 1`.

**Expected:** loader fails with memory range error.

## TC-V08-SEG-008 — Segment with filesz zero and memsz zero does not dirty memory

**Purpose:** confirm zero-size policy.

**Setup:** zero-size `PT_LOAD` over address that contains nonzero pattern.

**Expected:** memory remains unchanged for that segment.

## TC-V08-SEG-009 — Large valid segment near maximum memory loads

**Purpose:** stress loader with near-limit file/memory sizes.

**Setup:** segment of size close to available memory but still valid.

**Expected:** loader succeeds within memory limits.

## TC-V08-SEG-010 — Large invalid segment fails without partial execution

**Purpose:** ensure loader failure prevents running partially loaded input.

**Setup:** one valid segment followed by one invalid out-of-range segment.

**Expected:**

- Loader fails.
- CLI does not enter execution.
- Error is loader-specific.

---

# Entry Point and Stack Tests

## TC-V08-ENTRY-001 — PC initialized from e_entry

**Purpose:** core ELF behavior.

**Setup:** ELF text segment at `0x4000`, entry `0x4010`, first useful instruction at `0x4010`.

**Expected:**

- `pc == 0x4010` after load.
- Execution starts at `0x4010`, not `EMU_LOAD_ADDRESS`.

## TC-V08-ENTRY-002 — Entry at beginning of text segment

**Purpose:** common case.

**Setup:** `e_entry == p_vaddr`.

**Expected:** program runs normally.

## TC-V08-ENTRY-003 — Entry inside text segment but not first byte

**Purpose:** support ELF entry that skips headers/stubs inside segment.

**Setup:** `p_vaddr = 0x1000`, `e_entry = 0x1010`, with valid instructions at `0x1010`.

**Expected:** program begins at offset `0x10` inside the segment.

## TC-V08-ENTRY-004 — Entry outside loaded segments is rejected

**Purpose:** catch impossible executable entry.

**Setup:** valid segment at `0x1000`, `e_entry = 0x8000` outside all loaded memory ranges.

**Expected:** loader fails with entry-point error.

## TC-V08-ENTRY-005 — Entry in non-executable segment behavior is documented

**Purpose:** permissions are represented, not necessarily enforced.

**Setup:** entry points into data segment with no execute flag.

**Expected:**

- Recommended behavior: reject entry not in an executable segment.
- If permissions are not enforced, at least warn/error policy must be documented and tested.

## TC-V08-ENTRY-006 — Misaligned entry is rejected

**Purpose:** ARM64 instructions are 4-byte aligned.

**Setup:** `e_entry = 0x1002`.

**Expected:** loader fails with misaligned entry error before execution.

## TC-V08-ENTRY-007 — Entry at final valid instruction boundary succeeds

**Purpose:** boundary success for instruction fetch.

**Setup:** segment ends exactly after one instruction at `e_entry`.

**Expected:** fetch succeeds if four instruction bytes are present.

## TC-V08-ENTRY-008 — Stack pointer remains top of memory

**Purpose:** v0.8 creates initial stack without process-stack layout.

**Setup:** load any valid ELF.

**Expected:**

- `sp == EMU_MEMORY_SIZE` or the documented stack top.
- Existing stack examples still use valid downward-growing stack behavior.

## TC-V08-ENTRY-009 — ELF does not clobber stack top region unless segment overlaps it

**Purpose:** preserve stack area.

**Setup:** load small ELF text/data well below stack top.

**Expected:** memory near stack top remains zero/unmodified before execution.

## TC-V08-ENTRY-010 — Segment overlapping reserved stack area policy is documented

**Purpose:** avoid future stack collisions.

**Setup:** segment mapped near top of memory where stack starts.

**Expected:**

- Preferred v0.8 behavior: reject segments that overlap a documented reserved stack range.
- Acceptable alternative: allow any in-bounds segment and document that stack collision is program responsibility.
- Tests assert the chosen policy.

---

# Execution and Syscall Tests

## TC-V08-EXEC-001 — ELF program halts with HLT

**Purpose:** minimal execution from ELF.

**Setup:** ELF text contains `MOVZ X0, #42; HLT`.

**Expected:**

- CLI run prints normal halted behavior.
- `regs` shows `x0 = 42`.

## TC-V08-EXEC-002 — ELF program exits through fake syscall

**Purpose:** v0.7 runtime works from ELF-loaded code.

**Setup:** ELF text sets `x0 = 7`, `x8 = 93`, executes `SVC #0`.

**Expected:**

- Emulator stops through guest exit.
- CLI process exit status is `7`.

## TC-V08-EXEC-003 — ELF program writes to stdout

**Purpose:** hello-world acceptance path.

**Setup:** text segment loads arguments for `write(1, message, len)`, data segment contains message.

**Expected:**

- stdout contains exact message bytes.
- stderr is empty.
- If program then exits zero, CLI returns `0`.

## TC-V08-EXEC-004 — ELF program writes to stderr

**Purpose:** fd 2 path remains valid.

**Setup:** same as stdout but `fd = 2`.

**Expected:**

- stderr contains exact message bytes.
- stdout does not contain message unless trace/debug output is enabled.

## TC-V08-EXEC-005 — ELF program writes from BSS after storing data

**Purpose:** verify loaded zero bss can be used as guest memory.

**Setup:** ELF has data/bss segment. Program stores bytes into bss using supported store instructions, then calls `write` from that address.

**Expected:** written output equals stored bytes.

## TC-V08-EXEC-006 — ELF program reads initialized data

**Purpose:** verify data segment participates in normal load/store execution.

**Setup:** data segment contains a known 64-bit value. Text loads it with `LDR` and halts.

**Expected:** register contains expected value.

## TC-V08-EXEC-007 — ELF program uses stack call pattern

**Purpose:** v0.4 function/stack behavior works under ELF entry.

**Setup:** ELF text contains a small `BL`/`RET` function and `STP`/`LDP` stack frame if needed.

**Expected:** program returns to caller and halts/exits with expected register value.

## TC-V08-EXEC-008 — ELF branch target uses ELF addresses

**Purpose:** branch calculations use actual `pc` from ELF entry.

**Setup:** text at `0x4000` uses forward/backward branches.

**Expected:** target addresses are based on ELF `pc`, not raw load address assumptions.

## TC-V08-EXEC-009 — Instruction limit still applies

**Purpose:** infinite ELF programs cannot hang tests.

**Setup:** ELF text contains infinite branch to itself.

**Expected:**

- Emulator stops with instruction-limit error.
- Error includes `pc` and opcode context.

## TC-V08-EXEC-010 — Unsupported instruction in ELF reports context

**Purpose:** v0.6 error quality applies to ELF.

**Setup:** ELF text contains unsupported opcode at entry.

**Expected:**

- Execution fails.
- Error includes `pc`, raw opcode, and reason.

## TC-V08-EXEC-011 — Non-zero SVC immediate remains runtime error

**Purpose:** v0.7 syscall trap policy applies to ELF.

**Setup:** ELF text contains `SVC #1`.

**Expected:** runtime error mentions unsupported/nonzero SVC immediate.

## TC-V08-EXEC-012 — Unknown syscall returns -ENOSYS in ELF

**Purpose:** v0.7 recoverable fake syscall behavior applies to ELF.

**Setup:** ELF text sets `x8` to unknown syscall, executes `SVC #0`, then exits with low byte of `x0` or stores it.

**Expected:** `x0 == -ENOSYS` after syscall, and program continues until its next stop.

---

# CLI Tests

## TC-V08-CLI-001 — run accepts valid ELF

**Purpose:** primary user workflow.

**Command:**

```sh
./emulator run tests/v0_8/tmp/hello.elf
```

**Expected:**

- Exit status follows guest exit or `0` on normal halt.
- Output matches program behavior.
- No raw-loader-only error appears.

## TC-V08-CLI-002 — regs accepts valid ELF

**Purpose:** final register inspection works for ELF.

**Command:**

```sh
./emulator regs tests/v0_8/tmp/mov_hlt.elf
```

**Expected:**

- Registers are printed.
- `pc` reflects ELF execution path.
- Expected `xN` values appear.

## TC-V08-CLI-003 — trace accepts valid ELF

**Purpose:** readable traces work with ELF entry addresses.

**Command:**

```sh
./emulator trace tests/v0_8/tmp/hello.elf
```

**Expected:**

- Trace starts at `e_entry`.
- Lines include address, opcode, decoded instruction text.
- `svc #0x0` appears for syscall examples.

## TC-V08-CLI-004 — dump accepts valid ELF

**Purpose:** memory inspection works for loaded segments.

**Command:**

```sh
./emulator dump tests/v0_8/tmp/data.elf 0x2000 16
```

**Expected:**

- Dump shows loaded data segment bytes.
- BSS bytes appear as zero where expected.

## TC-V08-CLI-005 — debug accepts valid ELF

**Purpose:** debugger workflow works with ELF input.

**Setup:** script with `regs`, `break <entry>`, `run`, `step`, `continue`, `quit`.

**Expected:**

- Debugger loads ELF successfully.
- Breakpoints use ELF addresses.
- Stepping begins at `e_entry`.

## TC-V08-CLI-006 — invalid ELF exits non-zero

**Purpose:** stable CLI failure behavior.

**Command:** run with malformed ELF magic/header.

**Expected:**

- CLI returns non-zero.
- stderr begins with or includes `error:`.
- Error mentions ELF loader reason.
- stdout is empty unless command intentionally prints usage.

## TC-V08-CLI-007 — unsupported dynamic ELF exits non-zero

**Purpose:** clear ET_DYN/PT_INTERP failure.

**Expected:**

- CLI returns non-zero.
- Error mentions unsupported dynamic/PIE/interpreter.

## TC-V08-CLI-008 — raw binaries still work through every command

**Purpose:** no CLI regression.

**Commands:** run existing v0.1–v0.7 raw examples through representative `run`, `trace`, `regs`, `dump`, and `debug` commands.

**Expected:** all old behaviors remain stable.

## TC-V08-CLI-009 — usage text mentions ELF support

**Purpose:** docs in CLI match behavior.

**Command:** invalid usage or no args.

**Expected:** usage text says input can be a raw binary or supported ELF64 executable.

## TC-V08-CLI-010 — missing file error remains clear

**Purpose:** loader refactor must not break file-open diagnostics.

**Command:**

```sh
./emulator run does-not-exist.elf
```

**Expected:** error mentions failed open or loader error, not malformed ELF.

---

# Debugger Tests

## TC-V08-DBG-001 — debugger reset reloads ELF and entry PC

**Purpose:** debugger reset should use ELF entry, not raw load address.

**Steps:**

1. Initialize debugger with ELF entry at `0x4000`.
2. Step one instruction.
3. Run `run`/reset in debugger.

**Expected:** `pc` returns to `0x4000`.

## TC-V08-DBG-002 — breakpoint at ELF entry stops before first instruction

**Purpose:** breakpoint semantics match raw binaries.

**Setup:** add breakpoint at `e_entry`, continue.

**Expected:** debugger reports stopped at breakpoint before executing first instruction.

## TC-V08-DBG-003 — breakpoint inside ELF text segment stops

**Purpose:** breakpoints use loaded virtual addresses.

**Setup:** text has several instructions; breakpoint at second instruction address.

**Expected:** continue stops at that exact address.

## TC-V08-DBG-004 — breakpoint outside loaded ELF memory is allowed or rejected consistently

**Purpose:** define debugger validation policy.

**Setup:** attempt breakpoint at unmapped address.

**Expected:**

- If debugger only validates memory bounds, breakpoint may be accepted.
- If debugger validates loaded segments, it should reject.
- Chosen behavior is documented and tested.

## TC-V08-DBG-005 — debugger memory command reads ELF data segment

**Purpose:** inspect loaded data.

**Steps:** run debug script `x 0x2000 16`.

**Expected:** output shows data segment bytes.

## TC-V08-DBG-006 — debugger memory command reads BSS zeros

**Purpose:** inspect zero-filled segment region.

**Expected:** dump shows zero bytes in bss range.

## TC-V08-DBG-007 — debugger trace on uses ELF addresses

**Purpose:** trace formatting consistency.

**Steps:** enable trace and step/continue.

**Expected:** trace lines show `e_entry`-based addresses.

## TC-V08-DBG-008 — debugger handles ELF syscall output

**Purpose:** v0.7 debugger/syscall path works with ELF.

**Setup:** debug hello ELF, break before `svc`, step.

**Expected:** syscall output appears exactly once and execution state advances or exits as documented.

## TC-V08-DBG-009 — debugger does not enter REPL for malformed ELF

**Purpose:** loader errors happen before interactive execution.

**Setup:** run `debug malformed.elf`.

**Expected:**

- CLI exits non-zero.
- Error is printed.
- No debugger prompt appears.

---

# Error Message Tests

## TC-V08-ERR-001 — Errors identify ELF loader phase

**Purpose:** user-friendly diagnostics.

**Setup:** malformed header.

**Expected:** error includes `ELF` or `elf` and a clear reason.

## TC-V08-ERR-002 — Segment range errors include useful values

**Purpose:** debugging bad fixtures/linker scripts.

**Expected:** error includes at least address/size or file offset/size involved.

## TC-V08-ERR-003 — Unsupported machine error includes actual machine value

**Purpose:** make wrong-architecture files understandable.

**Expected:** error includes actual `e_machine` number or name and expected AArch64.

## TC-V08-ERR-004 — Dynamic linker error points to unsupported PT_INTERP

**Purpose:** common user mistake.

**Expected:** error says dynamic linking or `PT_INTERP` is unsupported, not generic invalid ELF.

## TC-V08-ERR-005 — Entry-point error includes e_entry

**Purpose:** help users fix linker scripts.

**Expected:** error includes the invalid entry address.

## TC-V08-ERR-006 — Raw loader errors unchanged

**Purpose:** refactor should not make old errors worse.

**Setup:** missing raw file, empty raw file, raw file too large.

**Expected:** messages remain loader-specific and clear.

---

# Regression Tests

## TC-V08-REG-001 — v0.1 tests still pass

**Purpose:** base CPU/raw loader regressions.

**Expected:** full v0.1 suite passes unchanged.

## TC-V08-REG-002 — v0.2 tests still pass

**Purpose:** branches and flags unaffected.

**Expected:** full v0.2 suite passes unchanged.

## TC-V08-REG-003 — v0.3 tests still pass

**Purpose:** memory and stack unaffected.

**Expected:** full v0.3 suite passes unchanged.

## TC-V08-REG-004 — v0.4 tests still pass

**Purpose:** calls/returns unaffected.

**Expected:** full v0.4 suite passes unchanged.

## TC-V08-REG-005 — v0.5 tests still pass

**Purpose:** debugger unaffected.

**Expected:** full v0.5 suite passes unchanged.

## TC-V08-REG-006 — v0.6 tests still pass

**Purpose:** trace/disassembly/error context unaffected.

**Expected:** full v0.6 suite passes unchanged.

## TC-V08-REG-007 — v0.7 tests still pass

**Purpose:** fake syscalls unaffected.

**Expected:** full v0.7 suite passes unchanged.

## TC-V08-REG-008 — make test builds and runs v0.8 tests

**Purpose:** top-level test workflow includes new version.

**Expected:** `make test` includes v0.8 unit and CLI tests.

## TC-V08-REG-009 — make clean removes v0.8 generated artifacts

**Purpose:** clean workspace behavior.

**Expected:** generated v0.8 fixtures, binaries, and test runners are removed.

---

# Documentation and Example Tests

## TC-V08-DOC-001 — README links v0.8 test plan

**Purpose:** navigation consistency.

**Expected:** README Test Plans section includes `docs/test-plan-v0.8.md`.

## TC-V08-DOC-002 — README current status updates to v0.8 after implementation

**Purpose:** avoid stale status.

**Expected:** after implementation, README says repository contains v0.8 ELF loader implementation.

## TC-V08-DOC-003 — README documents input formats

**Purpose:** users know raw and ELF both work.

**Expected:** docs mention raw binaries and supported ELF64 `ET_EXEC` files.

## TC-V08-DOC-004 — README documents limitations

**Purpose:** prevent expectations of full Linux compatibility.

**Expected:** docs clearly say no dynamic linker, no libc, no relocations, no normal Linux executable support.

## TC-V08-DOC-005 — examples README explains how to build v0.8 examples

**Purpose:** example workflow remains one-command friendly.

**Expected:** `examples/README.md` includes v0.8 ELF examples and optional toolchain notes.

## TC-V08-DOC-006 — v0.8 lesson exists

**Purpose:** continue lesson series.

**Expected:** `lessons/v0.8-elf-loader.md` explains:

- raw `.bin` vs ELF,
- ELF header vs program headers,
- `PT_LOAD`,
- entry point,
- bss zero-fill,
- why dynamic linking is out of scope.

## TC-V08-DOC-007 — v0.8 examples are versioned

**Purpose:** keep examples organized.

**Expected:** v0.8 examples live under `examples/v0_8/` and do not replace older examples.

## TC-V08-DOC-008 — Docs include exact supported ELF subset

**Purpose:** testable contract.

**Expected:** docs list:

```text
ELF64
little-endian
AArch64
ET_EXEC
PT_LOAD
no PT_INTERP
no ET_DYN/PIE
no relocations
```

---

# Acceptance Tests

## TC-V08-ACC-001 — Minimal ELF runs

**Purpose:** core acceptance.

**Program:** ELF text contains `MOVZ X0, #123; HLT`.

**Expected:**

- `./emulator regs minimal.elf` shows `x0 = 123`.
- Program starts at ELF entry.

## TC-V08-ACC-002 — ELF hello world prints

**Purpose:** main user-facing demo.

**Program:** static freestanding ELF writes `hello from elf\n` to stdout through fake syscall and exits zero.

**Expected:**

```text
hello from elf
```

- CLI exit status is `0`.

## TC-V08-ACC-003 — ELF exit status propagates

**Purpose:** process-status behavior works from ELF.

**Program:** ELF calls fake `exit(9)`.

**Expected:** CLI exit status is `9`.

## TC-V08-ACC-004 — ELF bss is zero-filled

**Purpose:** validate segment memory-size behavior.

**Program:** ELF reads from bss and exits with `0` only if the bytes are zero.

**Expected:** CLI exit status is `0`.

## TC-V08-ACC-005 — ELF trace is readable

**Purpose:** v0.6 readable traces apply to ELF.

**Command:**

```sh
./emulator trace hello.elf
```

**Expected:** trace begins at ELF entry and includes decoded instruction text.

## TC-V08-ACC-006 — ELF debug workflow works

**Purpose:** v0.5 debugger applies to ELF.

**Script:** break at entry, run, step, regs, continue.

**Expected:** debugger stops, steps, prints registers, and completes normally.

## TC-V08-ACC-007 — Dynamic ELF fails clearly

**Purpose:** common unsupported case.

**Program:** ELF with `PT_INTERP` or `ET_DYN`.

**Expected:** CLI exits non-zero and explains dynamic linking/PIE is unsupported.

## TC-V08-ACC-008 — Raw examples still work

**Purpose:** backward compatibility acceptance.

**Programs:** representative existing raw examples from v0.1 through v0.7.

**Expected:** all still behave as before.

---

# Suggested Implementation Checklist

Implementation is not required to follow this exact order, but this order keeps risk low:

1. Add ELF type/constants and little-endian read helpers.
2. Add file-read helper that loads input file into a temporary host buffer.
3. Add format detection:
   - full ELF magic -> ELF loader,
   - otherwise -> raw loader.
4. Preserve existing `load_raw_binary` behavior.
5. Parse and validate ELF header.
6. Parse and validate program header table.
7. Reject unsupported ELF type/machine/endian/class/version.
8. Reject `PT_INTERP`.
9. Validate every `PT_LOAD` file and memory range before mutating emulator memory, or ensure rollback/clear behavior on failure.
10. Load all valid `PT_LOAD` segments.
11. Zero-fill `p_memsz - p_filesz`.
12. Record segment metadata and flags.
13. Validate `e_entry`.
14. Initialize `pc` and `sp`.
15. Route CLI and debugger through the unified program loader.
16. Add v0.8 examples.
17. Add unit tests.
18. Add CLI/debugger tests.
19. Update README, examples docs, and lesson.
20. Run `make clean && make test`.

# Final Definition of Done

v0.8 is complete when:

- `make test` passes v0.1 through v0.8.
- Existing raw examples still work.
- A minimal static AArch64 ELF64 `ET_EXEC` fixture runs from its ELF entry point.
- `PT_LOAD` text/data/bss segments are loaded correctly.
- Invalid ELF files fail with clear loader errors.
- Dynamic/PIE/interpreter-based ELF files fail clearly as unsupported.
- `run`, `trace`, `regs`, `dump`, and `debug` work with supported ELF input.
- v0.7 fake syscalls work from ELF-loaded programs.
- README and examples docs describe the exact supported ELF subset and limitations.
- A v0.8 lesson explains ELF loading at the same learner-friendly level as prior lessons.

# v1.2 Virtual Memory Examples

v1.2 introduces the teaching virtual-memory model used by loaded programs.
The examples in earlier directories still run the same programs, but the loader
now installs a visible page map before execution:

```text
program pages -> mapped readable/executable or readable/writable
stack pages   -> mapped readable/writable near the top of memory
guard page    -> intentionally left unmapped below the stack
other pages   -> unmapped until a loader maps them
```

This directory also has a deterministic fixture generator for small raw programs
that demonstrate the v1.2 permission model without requiring clang, ld.lld, or
Apple tooling.

Generate the examples:

```sh
make examples/v1_2/simple_raw.bin
```

That command runs:

```sh
python3 examples/v1_2/generate_vm_fixtures.py --output-dir examples/v1_2
```

Generated files:

```text
simple_raw.bin          exits successfully
write_code_page.bin     tries to write into its own r-x code mapping
execute_unmapped.bin    branches outside its raw program mapping
mapping_inspection.txt  short manual-inspection checklist
```

The deterministic v1.2 test suite also uses `tests/fixtures/vm_fixture_writer.py`
to generate extra raw and ELF fixtures under `tests/v1_2/tmp/`. Those temporary
files are intentionally not tracked and are removed by `make clean`.

Try these inspection commands:

```sh
make examples/v1_2/simple_raw.bin
./emulator info examples/v1_2/simple_raw.bin
./emulator run examples/v1_2/simple_raw.bin
./emulator run examples/v1_2/write_code_page.bin
./emulator run examples/v1_2/execute_unmapped.bin
printf 'maps\nmap 0x1000\nmap 0xef000\nquit\n' | ./emulator debug examples/v1_2/simple_raw.bin
```

Expected ideas to notice:

- `info` now prints a `mappings:` section.
- program mappings use readable/writeable/executable labels such as `r-x` and
  `rw-`.
- the stack is mapped near `0x100000`.
- the page below the stack appears as a `stack-guard` line with `---`
  permissions.
- `write_code_page.bin` fails with a write-permission fault.
- `execute_unmapped.bin` fails with an unmapped execute fault.
- `tests/v1_2/test_cli_virtual_memory.sh` covers the CLI-facing version of
  those same permission faults.
- `tests/v1_2/test_debugger_virtual_memory.sh` covers `maps`, `map <address>`,
  debugger memory inspection, and breakpoint validation against unmapped or
  non-executable addresses.

Policy details:

- zero-length checks are treated as successful no-ops after ordinary bounds
  validation.
- empty input files are rejected by the loader before mapping.
- true overlapping mapping ranges are rejected.
- adjacent byte ranges are allowed, preserving the earlier ELF/Mach-O lesson
  fixtures.
- loaders keep the entry segment executable for compatibility with earlier
  generated fixtures; future stricter fixtures can turn that compatibility
  policy into a deliberate negative test.

This is still a teaching VM, not a real ARMv8 MMU. There are no page tables,
TLBs, processes, signals, demand paging, `mmap`, copy-on-write, or OS kernel
fault delivery yet.

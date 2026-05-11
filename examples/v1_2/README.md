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

For now this directory documents the v1.2 development slice. Automated v1.2
fixtures and tests will be added in the v1.2 test phase.

Try these inspection commands after building the v1.1 examples:

```sh
make examples/v1_1/hello.macho
./emulator info examples/v1_1/hello.macho
printf 'maps\nmap 0x1000\nmap 0xe0000\nquit\n' | ./emulator debug examples/v1_1/hello.macho
```

Expected ideas to notice:

- `info` now prints a `mappings:` section.
- program mappings use readable/writeable/executable labels such as `r-x` and
  `rw-`.
- the stack is mapped near `0x100000`.
- addresses below the stack mapping are intentionally unmapped, which prepares
  the project for stack-guard tests.

This is still a teaching VM, not a real ARMv8 MMU. There are no page tables,
TLBs, processes, signals, demand paging, `mmap`, copy-on-write, or OS kernel
fault delivery yet.

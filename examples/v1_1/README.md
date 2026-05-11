# v1.1 Mach-O examples

v1.1 includes deterministic Mach-O fixture generation for tiny teaching executables. The generated files are intentionally simple and are produced by `generate_macho_fixtures.py`, not by Xcode, `ld64`, or Apple platform SDKs.

The supported runtime profile is narrow:

```text
64-bit little-endian arm64 Mach-O
MH_EXECUTE
LC_SEGMENT_64 mapped into the existing 1 MiB flat memory
LC_MAIN resolved through mapped file ranges
optional LC_SYMTAB range validation and symbol-count inspection
toy SVC #0 syscalls only
no dyld, shared libraries, rebasing, binding, code signing, or Darwin process setup
```

Generate all v1.1 examples:

```sh
make examples/v1_1/minimal_exit.macho
make examples/v1_1/hello.macho
make examples/v1_1/zero_fill.macho
```

Or generate them directly:

```sh
python3 examples/v1_1/generate_macho_fixtures.py --output-dir examples/v1_1
```

Try them:

```sh
./emulator info examples/v1_1/hello.macho
./emulator run examples/v1_1/hello.macho
./emulator info examples/v1_1/zero_fill.macho
```

The generated fixtures are ignored by git and removed by `make clean`; the generator script is the checked-in source of truth.

Dedicated v1.1 automated tests live in `tests/v1_1/`, with reusable malformed and valid fixture generation in `tests/fixtures/macho_fixture_writer.py`.

These fixtures are intentionally tiny. More complex Apple runtime features remain unsupported here, including `dyld`, shared libraries, code signing, platform process setup, and real Darwin syscalls.

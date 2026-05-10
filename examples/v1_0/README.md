# v1.0 Smoke Manifest

v1.0 is a stability release, so it does not introduce a new program format or a new CPU subsystem. Instead, it points learners at a small set of representative examples that should keep working together:

```sh
make examples
./emulator run examples/v0_1/add.bin
./emulator trace examples/v0_2/trace_loop.bin
./emulator dump examples/v0_3/memory_store_load.bin 0xffff8 8
./emulator debug examples/v0_1/add.bin < examples/v0_5/debug_add_script.txt
./emulator run examples/v0_7/hello.bin
./emulator run examples/v0_8/hello_elf.elf
./emulator run examples/v0_9/return_42.elf
```

The v0.9 freestanding C examples are optional. If the build prints a skip message for a v0.9 `.elf`, that `.elf` was not produced; install or expose `clang` and `ld.lld` before running that specific example.

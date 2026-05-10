CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

TARGET := emulator
SRC := \
	src/main.c \
	src/debugger.c \
	src/emulator.c \
	src/disasm.c \
	src/cpu.c \
	src/memory.c \
	src/loader.c
OBJ := $(SRC:.c=.o)

CORE_SRC := \
	src/emulator.c \
	src/debugger.c \
	src/disasm.c \
	src/cpu.c \
	src/memory.c \
	src/loader.c
CORE_OBJ := $(CORE_SRC:.c=.o)

.PHONY: all clean examples run-demo test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c include/emulator.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

V0_1_EXAMPLES := examples/v0_1/add.bin examples/v0_1/nop_hlt.bin examples/v0_1/sub.bin
V0_2_EXAMPLES := \
	examples/v0_2/branch_forward.bin \
	examples/v0_2/branch_backward_loop.bin \
	examples/v0_2/cbnz_countdown.bin \
	examples/v0_2/cbz_skip.bin \
	examples/v0_2/cmp_beq.bin \
	examples/v0_2/cmp_bcond_loop.bin \
	examples/v0_2/cmp_bne.bin \
	examples/v0_2/signed_compare_lt_ge.bin \
	examples/v0_2/infinite_branch.bin \
	examples/v0_2/trace_loop.bin
V0_3_EXAMPLES := \
	examples/v0_3/memory_store_load.bin \
	examples/v0_3/stack_push_pop.bin \
	examples/v0_3/stp_ldp_stack.bin \
	examples/v0_3/w_register_load_store.bin \
	examples/v0_3/invalid_memory_access.bin
V0_4_EXAMPLES := \
	examples/v0_4/simple_call.bin \
	examples/v0_4/sequential_calls.bin \
	examples/v0_4/nested_calls.bin \
	examples/v0_4/call_with_stack_frame.bin \
	examples/v0_4/ret_x30.bin \
	examples/v0_4/ret_custom_register.bin \
	examples/v0_4/invalid_return.bin \
	examples/v0_4/unsaved_nested_call.bin
V0_7_EXAMPLES := \
	examples/v0_7/hello.bin \
	examples/v0_7/stderr.bin \
	examples/v0_7/exit_status.bin \
	examples/v0_7/bad_fd.bin

examples: $(V0_1_EXAMPLES) $(V0_2_EXAMPLES) $(V0_3_EXAMPLES) $(V0_4_EXAMPLES) $(V0_7_EXAMPLES)

examples/v0_1/%.o: examples/v0_1/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_1/%.bin: examples/v0_1/%.o
	llvm-objcopy -O binary -j .text $< $@

examples/v0_2/%.o: examples/v0_2/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_2/%.bin: examples/v0_2/%.o
	llvm-objcopy -O binary -j .text $< $@

examples/v0_3/%.o: examples/v0_3/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_3/%.bin: examples/v0_3/%.o
	llvm-objcopy -O binary -j .text $< $@

examples/v0_4/%.o: examples/v0_4/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_4/%.bin: examples/v0_4/%.o
	llvm-objcopy -O binary -j .text $< $@

examples/v0_7/%.o: examples/v0_7/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_7/%.bin: examples/v0_7/%.o
	llvm-objcopy -O binary -j .text $< $@

run-demo: all examples/v0_1/add.bin
	./$(TARGET) run examples/v0_1/add.bin

clean:
	rm -f $(TARGET) $(OBJ) tests/v0_1/*.o tests/v0_1/test_v0_1 tests/v0_2/*.o tests/v0_2/test_v0_2 \
		tests/v0_3/*.o tests/v0_3/test_v0_3 tests/v0_4/*.o tests/v0_4/test_v0_4 \
		tests/v0_5/*.o tests/v0_5/test_v0_5 tests/v0_6/*.o tests/v0_6/test_v0_6 \
		examples/v0_1/*.o examples/v0_1/*.bin examples/v0_2/*.o examples/v0_2/*.bin \
		examples/v0_3/*.o examples/v0_3/*.bin examples/v0_4/*.o examples/v0_4/*.bin \
		examples/v0_7/*.o examples/v0_7/*.bin \
		tests/v0_1/tmp/* tests/v0_2/tmp/* tests/v0_3/tmp/* tests/v0_4/tmp/* tests/v0_5/tmp/* \
		tests/v0_6/tmp/*

tests/v0_1/test_v0_1: tests/v0_1/test_v0_1.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_2/test_v0_2: tests/v0_2/test_v0_2.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_3/test_v0_3: tests/v0_3/test_v0_3.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_4/test_v0_4: tests/v0_4/test_v0_4.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_5/test_v0_5: tests/v0_5/test_v0_5.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_6/test_v0_6: tests/v0_6/test_v0_6.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: all examples tests/v0_1/test_v0_1 tests/v0_2/test_v0_2 tests/v0_3/test_v0_3 tests/v0_4/test_v0_4 tests/v0_5/test_v0_5 tests/v0_6/test_v0_6
	./tests/v0_1/test_v0_1
	./tests/v0_1/test_cli.sh
	./tests/v0_2/test_v0_2
	./tests/v0_2/test_cli_trace.sh
	./tests/v0_3/test_v0_3
	./tests/v0_3/test_cli_memory.sh
	./tests/v0_4/test_v0_4
	./tests/v0_4/test_cli_functions.sh
	./tests/v0_5/test_v0_5
	./tests/v0_5/test_cli_debugger.sh
	./tests/v0_6/test_v0_6
	./tests/v0_6/test_cli_runtime.sh

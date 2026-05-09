CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

TARGET := emulator
SRC := \
	src/main.c \
	src/emulator.c \
	src/cpu.c \
	src/memory.c \
	src/loader.c
OBJ := $(SRC:.c=.o)

CORE_SRC := \
	src/emulator.c \
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

examples: examples/v0_1/add.bin examples/v0_1/nop_hlt.bin examples/v0_1/sub.bin

examples/v0_1/%.o: examples/v0_1/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_1/%.bin: examples/v0_1/%.o
	llvm-objcopy -O binary -j .text $< $@

run-demo: all examples/v0_1/add.bin
	./$(TARGET) run examples/v0_1/add.bin

clean:
	rm -f $(TARGET) $(OBJ) tests/v0_1/*.o tests/v0_1/test_v0_1 \
		examples/v0_1/*.o examples/v0_1/*.bin tests/v0_1/tmp/*

tests/v0_1/test_v0_1: tests/v0_1/test_v0_1.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: all tests/v0_1/test_v0_1
	./tests/v0_1/test_v0_1
	./tests/v0_1/test_cli.sh

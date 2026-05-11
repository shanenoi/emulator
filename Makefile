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

.PHONY: all clean examples regression-examples run-demo test release-docs-check release-hygiene-check release-clean-check release-archive-check release-check release-archive test-asan test-ubsan test-cc-matrix

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
V0_8_EXAMPLES := \
	examples/v0_8/hello_elf.elf \
	examples/v0_8/exit_status_elf.elf \
	examples/v0_8/bss_elf.elf
V0_9_EXAMPLES := \
	examples/v0_9/return_42.elf \
	examples/v0_9/fib.elf \
	examples/v0_9/sum_array.elf \
	examples/v0_9/string_len.elf \
	examples/v0_9/hello_c.elf \
	examples/v0_9/nested_calls.elf \
	examples/v0_9/stack_locals.elf \
	examples/v0_9/byte_copy.elf \
	examples/v0_9/static_local.elf \
	examples/v0_9/stderr_c.elf \
	examples/v0_9/bad_fd_c.elf \
	examples/v0_9/unknown_syscall_c.elf \
	examples/v0_9/invalid_write_c.elf
V1_1_EXAMPLES := \
	examples/v1_1/minimal_exit.macho \
	examples/v1_1/hello.macho \
	examples/v1_1/zero_fill.macho

examples: $(V0_1_EXAMPLES) $(V0_2_EXAMPLES) $(V0_3_EXAMPLES) $(V0_4_EXAMPLES) $(V0_7_EXAMPLES) $(V0_8_EXAMPLES) $(V0_9_EXAMPLES) $(V1_1_EXAMPLES)

TEST_EXAMPLES := $(V0_1_EXAMPLES) $(V0_2_EXAMPLES) $(V0_3_EXAMPLES) $(V0_4_EXAMPLES) $(V0_7_EXAMPLES) $(V0_8_EXAMPLES)

regression-examples: $(TEST_EXAMPLES)

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

examples/v0_8/%.o: examples/v0_8/%.s
	clang --target=aarch64-none-elf -c $< -o $@

examples/v0_8/%.elf: examples/v0_8/%.o examples/v0_8/linker.ld
	ld.lld -static -nostdlib -T examples/v0_8/linker.ld $< -o $@

examples/v0_9/start.o: examples/v0_9/start.s
	@if command -v clang >/dev/null 2>&1; then \
		clang --target=aarch64-none-elf -c $< -o $@; \
	else \
		echo "skipping v0.9 example build: clang is not available"; \
	fi

examples/v0_9/%.o: examples/v0_9/%.c
	@if command -v clang >/dev/null 2>&1; then \
		clang --target=aarch64-none-elf -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie -O0 -c $< -o $@; \
	else \
		echo "skipping v0.9 example build: clang is not available"; \
	fi

examples/v0_9/%.elf: examples/v0_9/start.o examples/v0_9/%.o examples/v0_9/linker.ld
	@if command -v ld.lld >/dev/null 2>&1 && [ -f examples/v0_9/start.o ] && [ -f examples/v0_9/$*.o ]; then \
		ld.lld -static -nostdlib -T examples/v0_9/linker.ld examples/v0_9/start.o examples/v0_9/$*.o -o $@; \
	else \
		echo "skipping v0.9 example link for $@: clang/ld.lld outputs are not available"; \
	fi

$(V1_1_EXAMPLES): examples/v1_1/generate_macho_fixtures.py
	python3 examples/v1_1/generate_macho_fixtures.py --output-dir examples/v1_1

run-demo: all examples/v0_1/add.bin
	./$(TARGET) run examples/v0_1/add.bin


V1_0_RELEASE_TESTS := \
	tests/v1_0/test_cli_release.sh \
	tests/v1_0/test_docs_release.sh \
	tests/v1_0/test_optional_release_examples.sh

V1_1_TEST_FIXTURE_MARKER := tests/v1_1/tmp/.fixtures.stamp

clean:
	rm -f $(TARGET) $(OBJ) tests/v0_1/*.o tests/v0_1/test_v0_1 tests/v0_2/*.o tests/v0_2/test_v0_2 \
		tests/v0_3/*.o tests/v0_3/test_v0_3 tests/v0_4/*.o tests/v0_4/test_v0_4 \
		tests/v0_5/*.o tests/v0_5/test_v0_5 tests/v0_6/*.o tests/v0_6/test_v0_6 \
		tests/v0_7/*.o tests/v0_7/test_v0_7 tests/v0_8/*.o tests/v0_8/test_v0_8 \
		tests/v0_9/*.o tests/v0_9/test_v0_9 tests/v1_1/*.o tests/v1_1/test_v1_1 \
		examples/v0_1/*.o examples/v0_1/*.bin examples/v0_2/*.o examples/v0_2/*.bin \
		examples/v0_3/*.o examples/v0_3/*.bin examples/v0_4/*.o examples/v0_4/*.bin \
		examples/v0_7/*.o examples/v0_7/*.bin examples/v0_8/*.o examples/v0_8/*.elf \
		examples/v0_9/*.o examples/v0_9/*.elf examples/v1_1/*.macho \
		tests/v0_1/tmp/* tests/v0_2/tmp/* tests/v0_3/tmp/* tests/v0_4/tmp/* tests/v0_5/tmp/* \
		tests/v0_6/tmp/* tests/v0_7/tmp/* tests/v0_8/tmp/* tests/v0_9/tmp/* tests/v1_0/tmp/*
	rm -rf tests/v1_1/tmp/* tests/v1_1/tmp/.fixtures.stamp

$(V1_1_TEST_FIXTURE_MARKER): tests/fixtures/macho_fixture_writer.py
	mkdir -p tests/v1_1/tmp
	python3 tests/fixtures/macho_fixture_writer.py --output-dir tests/v1_1/tmp
	touch $@

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

tests/v0_7/test_v0_7: tests/v0_7/test_v0_7.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_8/test_v0_8: tests/v0_8/test_v0_8.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v0_9/test_v0_9: tests/v0_9/test_v0_9.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

tests/v1_1/test_v1_1: tests/v1_1/test_v1_1.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test: all $(TEST_EXAMPLES) $(V1_1_TEST_FIXTURE_MARKER) tests/v0_1/test_v0_1 tests/v0_2/test_v0_2 tests/v0_3/test_v0_3 tests/v0_4/test_v0_4 tests/v0_5/test_v0_5 tests/v0_6/test_v0_6 tests/v0_7/test_v0_7 tests/v0_8/test_v0_8 tests/v0_9/test_v0_9 tests/v1_1/test_v1_1
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
	mkdir -p tests/v0_7/tmp
	./tests/v0_7/test_v0_7
	./tests/v0_7/test_cli_syscalls.sh
	mkdir -p tests/v0_8/tmp
	./tests/v0_8/test_v0_8
	./tests/v0_8/test_cli_elf.sh
	mkdir -p tests/v0_9/tmp
	./tests/v0_9/test_v0_9
	./tests/v0_9/test_cli_c_programs.sh
	./tests/v0_9/test_optional_c_examples.sh
	mkdir -p tests/v1_0/tmp
	./tests/v1_0/test_cli_release.sh
	./tests/v1_0/test_docs_release.sh
	./tests/v1_0/test_optional_release_examples.sh
	mkdir -p tests/v1_1/tmp
	./tests/v1_1/test_v1_1
	./tests/v1_1/test_cli_macho.sh
	./tests/v1_1/test_docs_macho.sh
	./tests/v1_1/test_optional_macho_examples.sh

release-docs-check:
	@set -eu; \
		fail() { echo "release docs check failed: $$*" >&2; exit 1; }; \
		need_file() { [ -f "$$1" ] || fail "missing required file: $$1"; }; \
		for version in v0.1 v0.2 v0.3 v0.4 v0.5 v0.6 v0.7 v0.8 v0.9 v1.0 v1.1 v1.2; do \
			need_file "docs/test-plan-$$version.md"; \
		done; \
		for lesson in \
			lessons/v0.1-instruction-sandbox.md \
			lessons/v0.2-branches-and-loops.md \
			lessons/v0.3-memory-and-stack.md \
			lessons/v0.4-functions-and-returns.md \
			lessons/v0.5-debugger-repl.md \
			lessons/v0.6-assembler-friendly-runtime.md \
			lessons/v0.7-toy-syscalls.md \
			lessons/v0.8-elf-loader.md \
			lessons/v0.9-tiny-c-programs.md \
			lessons/v1.0-stable-learning-emulator.md \
			lessons/v1.1-mach-o-loader.md \
			lessons/v1.2-virtual-memory.md; do \
			need_file "$$lesson"; \
		done; \
		need_file examples/README.md; \
		need_file examples/v1_0/README.md; \
		need_file examples/v1_0/smoke_manifest.txt; \
		need_file examples/v1_1/README.md; \
		need_file examples/v1_1/generate_macho_fixtures.py; \
		need_file examples/v1_2/README.md; \
		need_file tests/v1_0/test_cli_release.sh; \
		need_file tests/v1_0/test_docs_release.sh; \
		need_file tests/v1_0/test_optional_release_examples.sh; \
		need_file tests/fixtures/macho_fixture_writer.py; \
		need_file tests/v1_1/test_v1_1.c; \
		need_file tests/v1_1/test_cli_macho.sh; \
		need_file tests/v1_1/test_docs_macho.sh; \
		need_file tests/v1_1/test_optional_macho_examples.sh; \
		need_file README.md; \
		grep -q "v1.1 Test Plan" README.md || fail "README does not link the v1.1 test plan"; \
		grep -q "v1.2 Test Plan" README.md || fail "README does not link the v1.2 test plan"; \
		grep -q "v1.1 Lesson" README.md || fail "README does not link the v1.1 lesson"; \
		grep -q "v1.2 Lesson" README.md || fail "README does not link the v1.2 lesson"; \
		grep -q "v0.1 through v1.1" README.md || fail "README does not describe current deterministic tests"; \
		grep -q "make release-check" README.md || fail "README does not document make release-check"; \
		grep -q "make test-asan" README.md || fail "README does not document optional sanitizer checks"; \
		grep -q "fresh archive.*full deterministic test suite\|fresh-archive full deterministic-suite" README.md || fail "README does not document full deterministic-suite archive validation"; \
		grep -q "raw.*ELF64" README.md || fail "README does not describe raw and ELF64 program support"; \
		grep -q "Mach-O" README.md || fail "README does not describe Mach-O program support"; \
		grep -q "Virtual Memory" README.md || fail "README does not describe v1.2 virtual memory"; \
		grep -q "page" README.md || fail "README does not describe page mappings"; \
		grep -q "dynamic linking" README.md || fail "README does not list stable limitations"; \
		if grep -qi "no ELF loader yet" README.md; then fail "README still says there is no ELF loader"; fi; \
		if grep -qi "v0.8 tests are missing\|v0.9 tests are missing" README.md; then fail "README contains stale missing-test wording for implemented versions"; fi; \
		if grep -qi "not added yet\|will be added in the v1.0 test phase\|tests/v1_0/.*planned\|tests are still pending" README.md; then fail "README contains stale wording about missing implemented tests"; fi; \
		printf '%s\n' "v1.2 release docs check passed"

release-hygiene-check:
	@set -eu; \
		fail() { echo "release hygiene check failed: $$*" >&2; exit 1; }; \
		[ -d .git ] || fail "not running from a git checkout"; \
		tracked_generated=$$(git ls-files | grep -E '(^emulator$$|\.(o|bin|elf|macho)$$|^tests/v[0-9_]+/test_v[0-9_]+$$)' || true); \
		if [ -n "$$tracked_generated" ]; then echo "$$tracked_generated" >&2; fail "generated build outputs are tracked"; fi; \
		for pattern in 'emulator' '*.o' '*.bin' '*.elf' '*.macho' 'tests/v0_9/tmp/' 'tests/v1_0/tmp/' 'tests/v1_1/tmp/'; do \
			grep -Fq "$$pattern" .gitignore || fail ".gitignore does not cover $$pattern"; \
		done; \
		if [ -d scripts ]; then fail "./scripts must not be shipped in source"; fi; \
		printf '%s\n' "v1.2 release hygiene check passed"

release-clean-check:
	@set -eu; \
		fail() { echo "release clean check failed: $$*" >&2; exit 1; }; \
		make all regression-examples >/dev/null; \
		mkdir -p tests/v0_9/tmp tests/v1_0/tmp tests/v1_1/tmp; \
		: > examples/v0_9/clean_probe.o; \
		: > examples/v0_9/clean_probe.elf; \
		: > tests/v0_9/tmp/clean_probe.tmp; \
		: > tests/v1_0/tmp/clean_probe.tmp; \
		: > tests/v1_1/tmp/clean_probe.tmp; \
		: > tests/v0_9/test_v0_9; \
		if [ "$${RELEASE_CLEAN_FULL:-0}" = "1" ]; then make examples >/dev/null; make test >/dev/null; fi; \
		make clean >/dev/null; \
		leftovers=$$(find . \
			\( -path './.git' -o -path './.git/*' \) -prune -o \
			\( -name emulator -o -name '*.o' -o -name '*.bin' -o -name '*.elf' -o -name '*.macho' \
			-o -path './tests/v0_1/test_v0_1' -o -path './tests/v0_2/test_v0_2' \
			-o -path './tests/v0_3/test_v0_3' -o -path './tests/v0_4/test_v0_4' \
			-o -path './tests/v0_5/test_v0_5' -o -path './tests/v0_6/test_v0_6' \
			-o -path './tests/v0_7/test_v0_7' -o -path './tests/v0_8/test_v0_8' \
			-o -path './tests/v0_9/test_v0_9' -o -path './tests/v1_1/test_v1_1' \
			\) -print | sort); \
		if [ -n "$$leftovers" ]; then echo "$$leftovers" >&2; fail "make clean left generated artifacts behind"; fi; \
		printf '%s\n' "v1.2 release clean-artifact check passed"

release-archive-check:
	@set -eu; \
		fail() { echo "release archive check failed: $$*" >&2; exit 1; }; \
		command -v git >/dev/null 2>&1 || fail "git is required"; \
		command -v zip >/dev/null 2>&1 || fail "zip is required"; \
		command -v unzip >/dev/null 2>&1 || fail "unzip is required"; \
		tmp_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/emulator-release-check.XXXXXX"); \
		trap 'rm -rf "'"$$tmp_dir"'"' EXIT INT TERM; \
		archive="$$tmp_dir/emulator_release_check.zip"; \
		extract_dir="$$tmp_dir/extract"; \
		mkdir -p "$$extract_dir"; \
		git archive --format=zip HEAD -o "$$archive"; \
		zip -qr "$$archive" .git; \
		[ -s "$$archive" ] || fail "archive was not produced"; \
		unzip -q "$$archive" -d "$$extract_dir"; \
		[ -f "$$extract_dir/Makefile" ] || fail "archive does not contain Makefile"; \
		[ -d "$$extract_dir/.git" ] || fail "archive does not contain .git"; \
		[ ! -d "$$extract_dir/scripts" ] || fail "archive contains ./scripts"; \
		make -C "$$extract_dir" clean >/dev/null; \
		EMULATOR_SKIP_OPTIONAL_REAL_TOOLCHAIN=1 make -C "$$extract_dir" -j$${RELEASE_ARCHIVE_JOBS:-4} test; \
		[ -x "$$extract_dir/emulator" ] || fail "fresh archive did not build emulator through make test"; \
		"$$extract_dir/emulator" help >/dev/null; \
		printf '%s\n' "v1.2 release archive check passed: fresh archive ran the full deterministic test suite"

release-check: release-docs-check release-hygiene-check release-clean-check release-archive-check
	@echo "v1.2 release gate passed: tests, docs, hygiene, clean-artifact, and fresh-archive full-suite checks completed successfully"

test-asan:
	@set -eu; \
		cc_bin=$${CC:-cc}; \
		command -v "$$cc_bin" >/dev/null 2>&1 || { echo "skipping AddressSanitizer check: compiler '$$cc_bin' is not available"; exit 0; }; \
		probe_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/emulator-asan-probe.XXXXXX"); \
		trap 'rm -rf "'"$$probe_dir"'"' EXIT INT TERM; \
		printf '%s\n' 'int main(void) { return 0; }' > "$$probe_dir/probe.c"; \
		flags='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer'; \
		if ! "$$cc_bin" $$flags "$$probe_dir/probe.c" -o "$$probe_dir/probe" >/dev/null 2>&1; then echo "skipping AddressSanitizer check: compiler '$$cc_bin' does not support required flags"; exit 0; fi; \
		make clean >/dev/null; \
		make CC="$$cc_bin" CFLAGS="$$flags" test; \
		printf '%s\n' "AddressSanitizer release check passed"

test-ubsan:
	@set -eu; \
		cc_bin=$${CC:-cc}; \
		command -v "$$cc_bin" >/dev/null 2>&1 || { echo "skipping UndefinedBehaviorSanitizer check: compiler '$$cc_bin' is not available"; exit 0; }; \
		probe_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/emulator-ubsan-probe.XXXXXX"); \
		trap 'rm -rf "'"$$probe_dir"'"' EXIT INT TERM; \
		printf '%s\n' 'int main(void) { return 0; }' > "$$probe_dir/probe.c"; \
		flags='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined'; \
		if ! "$$cc_bin" $$flags "$$probe_dir/probe.c" -o "$$probe_dir/probe" >/dev/null 2>&1; then echo "skipping UndefinedBehaviorSanitizer check: compiler '$$cc_bin' does not support required flags"; exit 0; fi; \
		make clean >/dev/null; \
		make CC="$$cc_bin" CFLAGS="$$flags" test; \
		printf '%s\n' "UndefinedBehaviorSanitizer release check passed"

test-cc-matrix:
	@set -eu; \
		ran=0; \
		for cc_bin in cc clang gcc; do \
			if command -v "$$cc_bin" >/dev/null 2>&1; then \
				ran=$$((ran + 1)); \
				echo "running v1.0 compiler-matrix check with CC=$$cc_bin"; \
				make clean >/dev/null; \
				make CC="$$cc_bin" test; \
			else \
				echo "skipping compiler-matrix entry: $$cc_bin is not available"; \
			fi; \
		done; \
		if [ "$$ran" -eq 0 ]; then echo "skipping compiler-matrix check: no supported host compiler was found"; exit 0; fi; \
		printf '%s\n' "v1.0 compiler-matrix check passed"

release-archive:
	@set -eu; \
		timestamp=$${SOURCE_DATE_EPOCH:-$$(date +%s)}; \
		archive="emulator_$${timestamp}.zip"; \
		git archive --format=zip HEAD -o "$$archive"; \
		zip -qr "$$archive" .git; \
		echo "created $$archive"

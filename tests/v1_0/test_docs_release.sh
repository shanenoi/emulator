#!/bin/sh
set -eu

TMP_DIR="tests/v1_0/tmp"
mkdir -p "$TMP_DIR"

fail() {
    echo "v1.0 docs release test failed: $*" >&2
    exit 1
}

require_file() {
    [ -f "$1" ] || fail "missing file: $1"
}

require_contains() {
    file="$1"
    needle="$2"
    grep -Fq -- "$needle" "$file" || fail "$file does not contain: $needle"
}

require_not_contains_ci() {
    file="$1"
    needle="$2"
    if grep -qi -- "$needle" "$file"; then
        fail "$file contains stale text matching: $needle"
    fi
}

for version in v0.1 v0.2 v0.3 v0.4 v0.5 v0.6 v0.7 v0.8 v0.9 v1.0; do
    require_file "docs/test-plan-$version.md"
    require_contains README.md "$version Test Plan"
done

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
    lessons/v1.0-stable-learning-emulator.md; do
    require_file "$lesson"
done

for text in \
    "raw binary" \
    "ELF64" \
    "debugger" \
    "readable trace" \
    "write = 64" \
    "exit = 93" \
    "freestanding C" \
    "v0.1 through v1.0" \
    "make release-check" \
    "make release-archive"; do
    require_contains README.md "$text"
done

for text in \
    "dynamic linking" \
    "PIE" \
    "printf" \
    "malloc" \
    "argv" \
    "envp" \
    "MMU" \
    "floating point"; do
    require_contains README.md "$text"
done

require_contains examples/README.md "v0.9 freestanding C"
require_contains examples/README.md "not** produced"
require_contains examples/v1_0/README.md "Smoke"
require_contains examples/v1_0/smoke_manifest.txt "debug"
require_contains lessons/v1.0-stable-learning-emulator.md "One-sentence summary"
require_contains docs/test-plan-v1.0.md "tests/v1_0/test_cli_release.sh"

require_not_contains_ci README.md "no ELF loader yet"
require_not_contains_ci README.md "v0.8 tests are missing"
require_not_contains_ci README.md "v0.9 tests are missing"
require_not_contains_ci README.md "Dedicated tests/v1_0/ release tests are still planned"
require_not_contains_ci README.md "will be added in the v1.0 test phase"
require_not_contains_ci README.md "not added yet"
require_not_contains_ci README.md "normal hosted C works"

# Ensure representative commands from docs are copy-pasteable enough to resolve paths.
for path in \
    examples/v0_1/add.bin \
    examples/v0_2/cbnz_countdown.bin \
    examples/v0_3/memory_store_load.bin \
    examples/v0_4/simple_call.bin \
    examples/v0_7/hello.bin \
    examples/v0_8/hello_elf.elf; do
    case "$path" in
        *.bin|*.elf) : ;;
        *) fail "unexpected command path: $path" ;;
    esac
done

sh scripts/release_docs_check.sh >/dev/null

echo "v1.0 docs release tests passed"

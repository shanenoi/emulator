#!/bin/sh
set -eu

fail() {
    echo "release clean check failed: $*" >&2
    exit 1
}

# This check intentionally runs in the current checkout instead of a temp copy.
# Here we create representative generated artifacts, clean the tree, and verify
# that known generated files are gone.  By default we avoid rebuilding every
# optional real-toolchain C example because release_archive_check already runs
# the full deterministic suite from a fresh archive.  Maintainers who want to run
# the literal TC-V10-BUILD-004 sequence in isolation can set
# RELEASE_CLEAN_FULL=1.
make all regression-examples >/dev/null
mkdir -p tests/v0_9/tmp tests/v1_0/tmp
: > examples/v0_9/clean_probe.o
: > examples/v0_9/clean_probe.elf
: > tests/v0_9/tmp/clean_probe.tmp
: > tests/v1_0/tmp/clean_probe.tmp
: > tests/v0_9/test_v0_9
if [ "${RELEASE_CLEAN_FULL:-0}" = "1" ]; then
    make examples >/dev/null
    make test >/dev/null
fi
make clean >/dev/null

# Avoid requiring a clean git working tree here: contributors may run this check
# before committing source/doc changes.  Instead, look only for known generated
# artifacts that should have been removed.
leftovers=$(find . \
    \( -path './.git' -o -path './.git/*' \) -prune -o \
    \( \
        -name emulator -o \
        -name '*.o' -o \
        -name '*.bin' -o \
        -name '*.elf' -o \
        -path './tests/v0_1/test_v0_1' -o \
        -path './tests/v0_2/test_v0_2' -o \
        -path './tests/v0_3/test_v0_3' -o \
        -path './tests/v0_4/test_v0_4' -o \
        -path './tests/v0_5/test_v0_5' -o \
        -path './tests/v0_6/test_v0_6' -o \
        -path './tests/v0_7/test_v0_7' -o \
        -path './tests/v0_8/test_v0_8' -o \
        -path './tests/v0_9/test_v0_9' \
    \) -print | sort)

if [ -n "$leftovers" ]; then
    echo "$leftovers" >&2
    fail "make clean left generated artifacts behind"
fi

printf '%s\n' "v1.0 release clean-artifact check passed"

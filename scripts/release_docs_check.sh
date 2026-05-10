#!/bin/sh
set -eu

fail() {
    echo "release docs check failed: $*" >&2
    exit 1
}

need_file() {
    [ -f "$1" ] || fail "missing required file: $1"
}

for version in v0.1 v0.2 v0.3 v0.4 v0.5 v0.6 v0.7 v0.8 v0.9 v1.0; do
    need_file "docs/test-plan-$version.md"
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
    need_file "$lesson"
done

need_file examples/README.md
need_file examples/v1_0/README.md
need_file examples/v1_0/smoke_manifest.txt
need_file tests/v1_0/test_cli_release.sh
need_file tests/v1_0/test_docs_release.sh
need_file tests/v1_0/test_optional_release_examples.sh
need_file scripts/release_clean_check.sh
need_file scripts/release_archive_check.sh
need_file scripts/optional_sanitizer_check.sh
need_file scripts/optional_cc_matrix_check.sh

README=README.md
need_file "$README"

grep -q "v1.0 Test Plan" "$README" || fail "README does not link the v1.0 test plan"
grep -q "v1.0 Lesson" "$README" || fail "README does not link the v1.0 lesson"
grep -q "v0.1 through v1.0" "$README" || fail "README does not describe v0.1 through v1.0 tests"
grep -q "make release-check" "$README" || fail "README does not document make release-check"
grep -q "make test-asan" "$README" || fail "README does not document optional sanitizer checks"
grep -q "fresh archive.*full deterministic test suite\|fresh-archive full deterministic-suite" "$README" || fail "README does not document full deterministic-suite archive validation"
grep -q "raw.*ELF64" "$README" || fail "README does not describe raw and ELF64 program support"
grep -q "dynamic linking" "$README" || fail "README does not list stable limitations"

if grep -qi "no ELF loader yet" "$README"; then
    fail "README still says there is no ELF loader"
fi
if grep -qi "v0.8 tests are missing\|v0.9 tests are missing" "$README"; then
    fail "README contains stale missing-test wording for implemented versions"
fi
if grep -qi "not added yet\|will be added in the v1.0 test phase\|tests/v1_0/.*planned" "$README"; then
    fail "README contains stale wording about missing v1.0 release tests"
fi

printf '%s\n' "v1.0 release docs check passed"

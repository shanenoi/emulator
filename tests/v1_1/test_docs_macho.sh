#!/bin/sh
set -eu

fail() {
    echo "v1.1 docs test failed: $*" >&2
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

for file in \
    docs/test-plan-v1.1.md \
    lessons/v1.1-mach-o-loader.md \
    examples/v1_1/README.md \
    examples/v1_1/generate_macho_fixtures.py \
    tests/fixtures/macho_fixture_writer.py \
    tests/v1_1/test_v1_1.c \
    tests/v1_1/test_cli_macho.sh \
    tests/v1_1/test_docs_macho.sh \
    tests/v1_1/test_optional_macho_examples.sh; do
    require_file "$file"
done

for text in \
    "v1.1 Test Plan" \
    "v1.1 Lesson" \
    "Mach-O" \
    "info" \
    "LC_SEGMENT_64" \
    "LC_MAIN" \
    "arm64" \
    "MH_EXECUTE" \
    "dyld" \
    "normal dynamically linked"; do
    require_contains README.md "$text"
done

for file in README.md lessons/v1.1-mach-o-loader.md examples/v1_1/README.md; do
    require_contains "$file" "LC_SEGMENT_64"
    require_contains "$file" "LC_MAIN"
    require_contains "$file" "little-endian"
    require_contains "$file" "arm64"
    require_contains "$file" "MH_EXECUTE"
    require_contains "$file" "dyld"
    require_contains "$file" "unsupported"
done

require_contains README.md "symbol names/addresses"
require_contains lessons/v1.1-mach-o-loader.md "symbol names/addresses"
require_contains examples/v1_1/README.md "symbol-name/address"

require_contains docs/test-plan-v1.1.md "TC-V11-BUILD-002"
require_contains docs/test-plan-v1.1.md "tests/v1_1/test_v1_1.c"
require_contains docs/test-plan-v1.1.md "tests/v1_1/test_cli_macho.sh"
require_contains docs/test-plan-v1.1.md "tests/v1_1/test_optional_macho_examples.sh"
require_contains Makefile "tests/v1_1/test_v1_1"
require_contains Makefile "tests/v1_1/test_cli_macho.sh"
require_contains Makefile "tests/v1_1/test_docs_macho.sh"
require_contains Makefile "tests/v1_1/test_optional_macho_examples.sh"
require_contains Makefile "examples/v1_1/*.macho"
require_contains .gitignore "examples/v1_1/*.macho"
require_contains .gitignore "tests/v1_1/tmp/"

require_not_contains_ci README.md "v1.1 tests have not been added"
require_not_contains_ci README.md "tests are still pending"
require_not_contains_ci lessons/v1.1-mach-o-loader.md "loader groundwork rather than a finished release"
require_not_contains_ci examples/v1_1/README.md "tests are still pending"

./emulator help >tests/v1_1/tmp/help.out
require_contains tests/v1_1/tmp/help.out "supported little-endian arm64 Mach-O MH_EXECUTE"

make release-docs-check >/dev/null

echo "v1.1 docs tests passed"

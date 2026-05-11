#!/bin/sh
set -eu

fail() {
    echo "release hygiene check failed: $*" >&2
    exit 1
}

[ -d .git ] || fail "not running from a git checkout"

# Generated artifacts should not be tracked as source.
tracked_generated=$(git ls-files | grep -E '(^emulator$|\.(o|bin|elf|macho)$|^tests/v[0-9_]+/test_v[0-9_]+$)' || true)
if [ -n "$tracked_generated" ]; then
    echo "$tracked_generated" >&2
    fail "generated build outputs are tracked"
fi

for pattern in 'emulator' '*.o' '*.bin' '*.elf' '*.macho' 'tests/v0_9/tmp/' 'tests/v1_0/tmp/' 'tests/v1_1/tmp/'; do
    grep -Fq "$pattern" .gitignore || fail ".gitignore does not cover $pattern"
done

# Helper scripts should be portable through explicit sh invocation from Makefile.
for script in \
    scripts/release_docs_check.sh \
    scripts/release_hygiene_check.sh \
    scripts/release_clean_check.sh \
    scripts/release_archive_check.sh \
    scripts/optional_sanitizer_check.sh \
    scripts/optional_cc_matrix_check.sh \
    scripts/optional_macho_toolchain_check.sh; do
    [ -f "$script" ] || fail "missing helper script: $script"
    head -n 1 "$script" | grep -q '^#!/bin/sh' || fail "$script is not a POSIX sh script"
    grep -q 'set -eu' "$script" || fail "$script does not use strict shell mode"
done

printf '%s\n' "v1.0 release hygiene check passed"

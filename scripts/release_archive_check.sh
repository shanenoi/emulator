#!/bin/sh
set -eu

fail() {
    echo "release archive check failed: $*" >&2
    exit 1
}

command -v git >/dev/null 2>&1 || fail "git is required"
command -v zip >/dev/null 2>&1 || fail "zip is required"
command -v unzip >/dev/null 2>&1 || fail "unzip is required"

root=$(pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/emulator-release-check.XXXXXX")
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

archive="$tmp_dir/emulator_release_check.zip"
extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir"

git archive --format=zip HEAD -o "$archive"
zip -qr "$archive" .git
[ -s "$archive" ] || fail "archive was not produced"

unzip -q "$archive" -d "$extract_dir"
[ -f "$extract_dir/Makefile" ] || fail "archive does not contain Makefile"
[ -d "$extract_dir/.git" ] || fail "archive does not contain .git"

# Keep this development-phase release check fast: prove a fresh archive builds.
# Dedicated v1.0 tests will later extend this to a full fresh-archive test run.
make -C "$extract_dir" clean >/dev/null
make -C "$extract_dir" >/dev/null
[ -x "$extract_dir/emulator" ] || fail "fresh archive did not build emulator"

printf '%s\n' "v1.0 release archive check passed"

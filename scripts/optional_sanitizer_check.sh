#!/bin/sh
set -eu

kind=${1:-}
case "$kind" in
    asan)
        flags='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer'
        label='AddressSanitizer'
        ;;
    ubsan)
        flags='-std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=undefined'
        label='UndefinedBehaviorSanitizer'
        ;;
    *)
        echo "usage: $0 asan|ubsan" >&2
        exit 2
        ;;
esac

cc_bin=${CC:-cc}
if ! command -v "$cc_bin" >/dev/null 2>&1; then
    echo "skipping $label check: compiler '$cc_bin' is not available"
    exit 0
fi

probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/emulator-${kind}-probe.XXXXXX")
cleanup() {
    rm -rf "$probe_dir"
}
trap cleanup EXIT INT TERM

cat > "$probe_dir/probe.c" <<'C'
int main(void) { return 0; }
C

if ! "$cc_bin" $flags "$probe_dir/probe.c" -o "$probe_dir/probe" >/dev/null 2>&1; then
    echo "skipping $label check: compiler '$cc_bin' does not support required flags"
    exit 0
fi

make clean >/dev/null
make CC="$cc_bin" CFLAGS="$flags" test
printf '%s\n' "$label release check passed"

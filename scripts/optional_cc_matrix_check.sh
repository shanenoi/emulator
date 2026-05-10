#!/bin/sh
set -eu

ran=0
for cc_bin in cc clang gcc; do
    if command -v "$cc_bin" >/dev/null 2>&1; then
        ran=$((ran + 1))
        echo "running v1.0 compiler-matrix check with CC=$cc_bin"
        make clean >/dev/null
        make CC="$cc_bin" test
    else
        echo "skipping compiler-matrix entry: $cc_bin is not available"
    fi
done

if [ "$ran" -eq 0 ]; then
    echo "skipping compiler-matrix check: no supported host compiler was found"
    exit 0
fi

printf '%s\n' "v1.0 compiler-matrix check passed"

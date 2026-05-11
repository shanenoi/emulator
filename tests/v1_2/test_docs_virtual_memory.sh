#!/bin/sh
set -eu

require_file() {
    [ -f "$1" ] || { echo "FAIL: missing $1" >&2; exit 1; }
}

require_contains() {
    file="$1"
    needle="$2"
    if ! grep -Fq -- "$needle" "$file"; then
        echo "FAIL: expected $file to contain: $needle" >&2
        exit 1
    fi
}

require_not_contains() {
    file="$1"
    needle="$2"
    if grep -Fq -- "$needle" "$file"; then
        echo "FAIL: expected stale wording to be gone from $file: $needle" >&2
        exit 1
    fi
}

# TC-V12-DOC-001 through TC-V12-DOC-008 and TC-V12-REL-005.
require_file docs/test-plan-v1.2.md
require_file lessons/v1.2-virtual-memory.md
require_file examples/v1_2/README.md
require_file examples/v1_2/generate_vm_fixtures.py
require_file tests/fixtures/vm_fixture_writer.py
require_file tests/v1_2/test_v1_2.c
require_file tests/v1_2/test_cli_virtual_memory.sh
require_file tests/v1_2/test_debugger_virtual_memory.sh

require_contains README.md "v1.2 Test Plan"
require_contains README.md "v1.2 Lesson"
require_contains README.md "Virtual Memory"
require_contains README.md "maps"
require_contains README.md "map <address>"
require_contains README.md "stack-guard"
require_contains lessons/v1.2-virtual-memory.md "page"
require_contains lessons/v1.2-virtual-memory.md "zero-fill"
require_contains lessons/v1.2-virtual-memory.md "unmapped access"
require_contains lessons/v1.2-virtual-memory.md "read permission"
require_contains lessons/v1.2-virtual-memory.md "write permission"
require_contains lessons/v1.2-virtual-memory.md "execute permission"
require_contains lessons/v1.2-virtual-memory.md "Out of scope"
require_contains examples/v1_2/README.md "generate_vm_fixtures.py"
require_contains examples/v1_2/README.md "write_code_page.bin"
require_contains examples/v1_2/README.md "execute_unmapped.bin"
require_contains Makefile "tests/v1_2/test_v1_2"
require_contains Makefile "test_cli_virtual_memory.sh"
require_contains Makefile "test_debugger_virtual_memory.sh"
require_contains Makefile "test_docs_virtual_memory.sh"
require_contains .gitignore "examples/v1_2/*.bin"
require_not_contains README.md "v1.2 tests are intentionally deferred"
require_not_contains README.md "fixtures/tests are deferred"
require_not_contains examples/v1_2/README.md "deferred"

printf '%s\n' "v1.2 docs virtual-memory tests passed"

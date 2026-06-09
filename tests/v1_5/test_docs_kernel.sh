#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

fail() { echo "test_docs_kernel.sh failed: $*" >&2; exit 1; }
need_file() { [ -f "$1" ] || fail "missing file: $1"; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains_i() { if grep -Eiq "$2" "$1"; then fail "stale/overclaim text in $1 matching: $2"; fi; }

need_file docs/test-plan-v1.5.md
need_file lessons/v1.5-toy-kernel-and-cooperative-tasks.md
need_file examples/v1_5/README.md
need_file examples/v1_5/generate_kernel_fixtures.py
need_file tests/fixtures/kernel_fixture_writer.py
need_file tests/v1_5/test_v1_5.c
need_file tests/v1_5/test_cli_kernel.sh
need_file tests/v1_5/test_debugger_kernel.sh
need_file tests/v1_5/test_docs_kernel.sh
need_file tests/v1_5/test_optional_kernel_examples.sh

contains README.md "--kernel"
not_contains_i README.md "v1\.5.*tests are still pending|dedicated v1\.5 automated tests are still to be added|tests are intentionally deferred|yet to add tests"

for f in docs/test-plan-v1.5.md lessons/v1.5-toy-kernel-and-cooperative-tasks.md examples/v1_5/README.md; do
    contains "$f" "BRK #0x150"
    contains "$f" "BRK #0x151"
    contains "$f" "BRK #0x154"
    contains "$f" "deterministic"
    contains "$f" "toy-kernel"
    contains "$f" "not"
    not_contains_i "$f" "supports real GIC|implements full EL1|uses host wall-clock interrupt|production kernel ready"
done

contains lessons/v1.5-toy-kernel-and-cooperative-tasks.md "boot-info"
contains lessons/v1.5-toy-kernel-and-cooperative-tasks.md "cooperative"
contains lessons/v1.5-toy-kernel-and-cooperative-tasks.md "FAULTED"
contains examples/v1_5/README.md "single_task_exit.bin"
contains examples/v1_5/README.md "two_task_yield.bin"
contains examples/v1_5/README.md "sleep_then_exit.bin"
contains examples/v1_5/README.md "task_fault_then_exit.bin"
contains examples/v1_5/README.md "eret_task_fault_then_exit.bin"
contains examples/v1_5/README.md "three_task_round_robin.bin"
contains examples/v1_5/README.md "kernel_panic.bin"
contains examples/v1_5/README.md "console_write.bin"
contains examples/v1_5/README.md "sleep_deadlock.bin"
contains examples/v1_5/README.md "infinite_task.bin"
contains examples/v1_5/README.md "--output-dir"

python3 - <<'PY'
from pathlib import Path
makefile = Path('Makefile').read_text()
required = [
    'V1_5_TEST_FIXTURE_MARKER',
    'tests/v1_5/test_v1_5',
    './tests/v1_5/test_v1_5',
    './tests/v1_5/test_cli_kernel.sh',
    './tests/v1_5/test_debugger_kernel.sh',
    './tests/v1_5/test_docs_kernel.sh',
    './tests/v1_5/test_optional_kernel_examples.sh',
]
missing = [item for item in required if item not in makefile]
if missing:
    raise SystemExit('Makefile missing v1.5 test hooks: ' + ', '.join(missing))
plan = Path('docs/test-plan-v1.5.md').read_text()
for needle in ['Implemented v1.5 Contract', '`ERET` outside an active exception inside a task', 'guest-visible task descriptors']:
    if needle not in plan:
        raise SystemExit(f'test plan missing current-scope clarification: {needle}')
PY

printf '%s\n' "v1.5 docs kernel tests passed"

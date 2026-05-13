#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

fail() { echo "test_docs_exceptions.sh failed: $*" >&2; exit 1; }
need_file() { [ -f "$1" ] || fail "missing file: $1"; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains_i() { if grep -Eiq "$2" "$1"; then fail "stale/overclaim text in $1 matching: $2"; fi; }

need_file docs/test-plan-v1.4.md
need_file lessons/v1.4-exceptions-and-interrupts.md
need_file examples/v1_4/README.md
need_file examples/v1_4/generate_exception_fixtures.py
need_file tests/fixtures/exception_fixture_writer.py
need_file tests/v1_4/test_v1_4.c
need_file tests/v1_4/test_cli_exceptions.sh
need_file tests/v1_4/test_debugger_exceptions.sh
need_file tests/v1_4/test_docs_exceptions.sh

contains README.md "v1.4 Test Plan"
contains README.md "v1.4 Lesson"
contains README.md "v1.4 — Exceptions, Traps, and Interrupt Skeleton"
contains README.md "implemented/tested teaching profile for **v1.4"
contains README.md "v0.1 through v1.4 deterministic test suite"
contains README.md "0x09030000"
contains README.md "--exception-vector"
contains README.md "--timer-interrupt"
contains README.md "v1.5 — Toy Kernel Mode"
contains README.md "v1.6 — Tiny OS Lab"
not_contains_i README.md "v1\.4.*initial development slice|current v1\.4 test pass is pending|tests are intentionally deferred|v1\.4 tests are still pending|yet to add tests"

for f in docs/test-plan-v1.4.md lessons/v1.4-exceptions-and-interrupts.md examples/v1_4/README.md; do
    contains "$f" "0x09030000"
    contains "$f" "BRK"
    contains "$f" "ERET"
    contains "$f" "SVC_TRAP"
    contains "$f" "TIMER_INTERRUPT"
    contains "$f" "deterministic"
    contains "$f" "not" 
    not_contains_i "$f" "supports real GIC|implements full EL1|full ARM exception fidelity|production kernel ready|uses host wall-clock interrupt|uses real asynchronous"
done

contains examples/v1_4/README.md "make examples/v1_4/cli_handled_brk.bin"
contains examples/v1_4/README.md "./emulator trace examples/v1_4/cli_handled_brk.bin --exception-vector 0x1080"
contains examples/v1_4/README.md "mmio_timer_once.bin"
contains lessons/v1.4-exceptions-and-interrupts.md "x0"
contains lessons/v1.4-exceptions-and-interrupts.md "x3"
contains docs/test-plan-v1.4.md "make test"
contains docs/test-plan-v1.4.md "tests/v1_4/test_v1_4.c"
contains docs/test-plan-v1.4.md "tests/v1_4/test_cli_exceptions.sh"

python3 - <<'PY'
from pathlib import Path
readme = Path('README.md').read_text()
makefile = Path('Makefile').read_text()
required = [
    'tests/v1_4/test_v1_4',
    './tests/v1_4/test_v1_4',
    './tests/v1_4/test_cli_exceptions.sh',
    './tests/v1_4/test_debugger_exceptions.sh',
    './tests/v1_4/test_docs_exceptions.sh',
    'V1_4_TEST_FIXTURE_MARKER',
]
missing = [item for item in required if item not in makefile]
if missing:
    raise SystemExit('Makefile missing v1.4 test hooks: ' + ', '.join(missing))
if readme.find('v1.4 — Exceptions') > readme.find('v1.5 — Toy Kernel Mode'):
    raise SystemExit('README roadmap order puts v1.4 after v1.5')
PY

printf '%s\n' "v1.4 docs exception tests passed"

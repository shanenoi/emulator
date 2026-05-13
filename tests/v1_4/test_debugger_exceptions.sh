#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_4/tmp
mkdir -p "$TMP"
python3 tests/fixtures/exception_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_debugger_exceptions.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq -- "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq -- "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

cat >"$TMP/debug_exception.in" <<'EOF'
exception
s
exception
s
exception
s
exception
q
EOF
./emulator debug "$TMP/cli_handled_brk.bin" --exception-vector 0x1080 <"$TMP/debug_exception.in" >"$TMP/debug_exception.out" 2>"$TMP/debug_exception.err"
[ ! -s "$TMP/debug_exception.err" ] || fail "debug exception stderr was not empty"
contains "$TMP/debug_exception.out" "exception_active = no"
contains "$TMP/debug_exception.out" "vector_configured = yes"
contains "$TMP/debug_exception.out" "pc=0x0000000000001080"
contains "$TMP/debug_exception.out" "exception_active = yes"
contains "$TMP/debug_exception.out" "cause = 0x02"
contains "$TMP/debug_exception.out" "fault_address = 0x0000000000000014"
contains "$TMP/debug_exception.out" "resume_pc = 0x0000000000001004"
contains "$TMP/debug_exception.out" "pc=0x0000000000001004"

cat >"$TMP/debug_continue.in" <<'EOF'
continue
exception
q
EOF
./emulator debug "$TMP/mmio_skip_device_fault.bin" <"$TMP/debug_continue.in" >"$TMP/debug_continue.out" 2>"$TMP/debug_continue.err"
[ ! -s "$TMP/debug_continue.err" ] || fail "debug continue stderr was not empty"
contains "$TMP/debug_continue.out" "halted"
contains "$TMP/debug_continue.out" "exception_active = no"
contains "$TMP/debug_continue.out" "cause = 0x00"

cat >"$TMP/debug_trace.in" <<'EOF'
trace on
s
s
s
q
EOF
./emulator debug "$TMP/cli_handled_brk.bin" --exception-vector 0x1080 <"$TMP/debug_trace.in" >"$TMP/debug_trace.out" 2>"$TMP/debug_trace.err"
[ ! -s "$TMP/debug_trace.err" ] || fail "debug trace stderr was not empty"
contains "$TMP/debug_trace.out" "trace enabled"
contains "$TMP/debug_trace.out" "trace exception-enter cause=breakpoint-or-trap(0x02)"
contains "$TMP/debug_trace.out" "trace exception-return cause=breakpoint-or-trap(0x02)"

cat >"$TMP/debug_break.in" <<'EOF'
break 0x1000
continue
exception
s
exception
q
EOF
./emulator debug "$TMP/cli_handled_brk.bin" --exception-vector 0x1080 <"$TMP/debug_break.in" >"$TMP/debug_break.out" 2>"$TMP/debug_break.err"
[ ! -s "$TMP/debug_break.err" ] || fail "debug breakpoint stderr was not empty"
contains "$TMP/debug_break.out" "breakpoint 1 set at 0x0000000000001000"
contains "$TMP/debug_break.out" "breakpoint hit at 0x0000000000001000"
contains "$TMP/debug_break.out" "exception_active = no"
contains "$TMP/debug_break.out" "pc=0x0000000000001080"
contains "$TMP/debug_break.out" "exception_active = yes"

printf '%s\n' "v1.4 debugger exception tests passed"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TMP=tests/v1_3/tmp
mkdir -p "$TMP"
python3 tests/fixtures/device_fixture_writer.py --output-dir "$TMP"

fail() { echo "test_debugger_devices.sh failed: $*" >&2; exit 1; }
contains() { grep -Fq "$2" "$1" || fail "expected $1 to contain: $2"; }
not_contains() { if grep -Fq "$2" "$1"; then fail "did not expect $1 to contain: $2"; fi; }

cat >"$TMP/debug_maps.in" <<'EOF'
maps
map 0x09000000
map 150994944
mem 0x09000000 4
break 0x09000000
quit
EOF
./emulator debug "$TMP/uart_hi.bin" <"$TMP/debug_maps.in" >"$TMP/debug_maps.out" 2>"$TMP/debug_maps.err"
contains "$TMP/debug_maps.out" "devices: 5"
contains "$TMP/debug_maps.out" "name=uart"
contains "$TMP/debug_maps.out" "0x0000000009000000-0x0000000009001000"
contains "$TMP/debug_maps.out" "name=keyboard"
contains "$TMP/debug_maps.out" "name=terminal"
contains "$TMP/debug_maps.out" "address 0x0000000009000000 is in rw- device"
contains "$TMP/debug_maps.err" "dump range out of bounds"
contains "$TMP/debug_maps.err" "breakpoint address outside memory"
not_contains "$TMP/debug_maps.out" "Hi"

cat >"$TMP/debug_step.in" <<'EOF'
s
s
s
s
q
EOF
./emulator debug "$TMP/uart_no_svc.bin" <"$TMP/debug_step.in" >"$TMP/debug_step.out" 2>"$TMP/debug_step.err"
[ ! -s "$TMP/debug_step.err" ] || fail "debug step stderr was not empty"
contains "$TMP/debug_step.out" "pc=0x0000000000001010"
contains "$TMP/debug_step.out" "Mpc=0x0000000000001010"

cat >"$TMP/debug_continue.in" <<'EOF'
break 0x1010
continue
q
EOF
./emulator debug "$TMP/uart_no_svc.bin" <"$TMP/debug_continue.in" >"$TMP/debug_continue.out" 2>"$TMP/debug_continue.err"
[ ! -s "$TMP/debug_continue.err" ] || fail "debug continue stderr was not empty"
contains "$TMP/debug_continue.out" "breakpoint 1 set at 0x0000000000001010"
contains "$TMP/debug_continue.out" "Mbreakpoint hit at 0x0000000000001010"

cat >"$TMP/debug_fault.in" <<'EOF'
s
s
s
s
q
EOF
./emulator debug "$TMP/uart_bad_offset.bin" <"$TMP/debug_fault.in" >"$TMP/debug_fault.out" 2>"$TMP/debug_fault.err"
contains "$TMP/debug_fault.err" "invalid write register"
contains "$TMP/debug_fault.err" "pc=0x000000000000100c"
contains "$TMP/debug_fault.out" "pc=0x000000000000100c"

cat >"$TMP/debug_trace.in" <<'EOF'
trace on
s
s
s
s
q
EOF
./emulator debug "$TMP/uart_no_svc.bin" <"$TMP/debug_trace.in" >"$TMP/debug_trace.out" 2>"$TMP/debug_trace.err"
[ ! -s "$TMP/debug_trace.err" ] || fail "debug trace stderr was not empty"
contains "$TMP/debug_trace.out" "trace enabled"
contains "$TMP/debug_trace.out" "trace pc=0x000000000000100c"
contains "$TMP/debug_trace.out" "strb w1, [x0]"
contains "$TMP/debug_trace.out" "Mpc=0x0000000000001010"

printf '%s\n' "v1.3 debugger device tests passed"

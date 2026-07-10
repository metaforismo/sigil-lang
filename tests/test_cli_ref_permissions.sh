#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

smt_dir="$work_dir/smt"
output="$("$sigil_bin" check "$example_file" --no-z3 --solver-timeout-ms 250 --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 5" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.expose_write_permission.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.2.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.3.memory_write" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.ensures.1.exact" >/dev/null

expose="$smt_dir/fn.expose_write_permission.ensures.1.exact.smt2"
write_gate="$smt_dir/fn.store_preserves_write_permission.safety.3.memory_write.smt2"
preserve="$smt_dir/fn.store_preserves_write_permission.ensures.1.exact.smt2"

test -f "$expose"
test -f "$write_gate"
test -f "$preserve"

grep "(set-option :timeout 250)" "$expose" >/dev/null
grep "(declare-const ptr_write Bool)" "$expose" >/dev/null
grep "(assert (= result ptr_write))" "$expose" >/dev/null
grep "(assert (not (= result ptr_write)))" "$expose" >/dev/null

grep "(declare-const ptr_write Bool)" "$write_gate" >/dev/null
grep "(assert ptr_write)" "$write_gate" >/dev/null
grep "(assert (not ptr_write))" "$write_gate" >/dev/null

grep "(declare-const updated_write Bool)" "$preserve" >/dev/null
grep "(assert (= updated_write ptr_write))" "$preserve" >/dev/null
grep "(assert (= result updated_write))" "$preserve" >/dev/null
grep "(assert (not (= result ptr_write)))" "$preserve" >/dev/null

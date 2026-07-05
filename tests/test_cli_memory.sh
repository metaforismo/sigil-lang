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

printf '%s\n' "$output" | grep "  functions: 4" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 8" >/dev/null
printf '%s\n' "$output" | grep "fn.read_ref.safety.1.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "fn.read_flag.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.refs_are_disjoint.ensures.1.exact" >/dev/null

ref_valid="$smt_dir/fn.read_ref.safety.1.memory_valid.smt2"
bool_load="$smt_dir/fn.read_flag.ensures.1.exact.smt2"
disjoint="$smt_dir/fn.refs_are_disjoint.ensures.1.exact.smt2"
same_ref="$smt_dir/fn.refs_are_same.ensures.1.exact.smt2"

test -f "$ref_valid"
test -f "$bool_load"
test -f "$disjoint"
test -f "$same_ref"

grep "(set-option :timeout 250)" "$ref_valid" >/dev/null
grep "(declare-const ptr_valid Bool)" "$ref_valid" >/dev/null
grep "(declare-const ptr_value Int)" "$ref_valid" >/dev/null
grep "(assert (not ptr_valid))" "$ref_valid" >/dev/null
grep "(declare-const ptr_value Bool)" "$bool_load" >/dev/null
grep "(assert (= result ptr_value))" "$bool_load" >/dev/null
grep "(assert (= result (distinct left_addr right_addr)))" "$disjoint" >/dev/null
grep "(assert (= result (= left_addr right_addr)))" "$same_ref" >/dev/null

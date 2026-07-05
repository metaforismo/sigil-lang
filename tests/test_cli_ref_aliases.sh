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
printf '%s\n' "$output" | grep "  proof obligations: 6" >/dev/null
printf '%s\n' "$output" | grep "fn.same_ref_loads_match.safety.1.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "fn.same_ref_loads_match.safety.2.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.same_ref_loads_match.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.same_bool_ref_loads_match.ensures.1.exact" >/dev/null

i64_exact="$smt_dir/fn.same_ref_loads_match.ensures.1.exact.smt2"
bool_exact="$smt_dir/fn.same_bool_ref_loads_match.ensures.1.exact.smt2"

test -f "$i64_exact"
test -f "$bool_exact"

grep "(set-option :timeout 250)" "$i64_exact" >/dev/null
grep "(declare-const left_value Int)" "$i64_exact" >/dev/null
grep "(declare-const right_value Int)" "$i64_exact" >/dev/null
grep "(assert (or (or (not left_valid) (not right_valid)) (or (distinct left_addr right_addr) (= left_value right_value))))" "$i64_exact" >/dev/null
grep "(assert (= result left_value))" "$i64_exact" >/dev/null
grep "(assert (not (= result right_value)))" "$i64_exact" >/dev/null

grep "(declare-const left_value Bool)" "$bool_exact" >/dev/null
grep "(declare-const right_value Bool)" "$bool_exact" >/dev/null
grep "(assert (or (or (not left_valid) (not right_valid)) (or (distinct left_addr right_addr) (= left_value right_value))))" "$bool_exact" >/dev/null
grep "(assert (= result left_value))" "$bool_exact" >/dev/null
grep "(assert (not (= result right_value)))" "$bool_exact" >/dev/null

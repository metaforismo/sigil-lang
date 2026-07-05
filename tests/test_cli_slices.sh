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

printf '%s\n' "$output" | grep "  functions: 3" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 7" >/dev/null
printf '%s\n' "$output" | grep "fn.read_slice.safety.1.index_in_bounds" >/dev/null
printf '%s\n' "$output" | grep "fn.read_flags.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.length_is_non_negative.ensures.1.non_negative" >/dev/null

slice_bounds="$smt_dir/fn.read_slice.safety.1.index_in_bounds.smt2"
array_ensure="$smt_dir/fn.read_flags.ensures.1.exact.smt2"
length_ensure="$smt_dir/fn.length_is_non_negative.ensures.1.non_negative.smt2"

test -f "$slice_bounds"
test -f "$array_ensure"
test -f "$length_ensure"

grep "(set-option :timeout 250)" "$slice_bounds" >/dev/null
grep "(declare-const xs_len Int)" "$slice_bounds" >/dev/null
grep "(declare-const xs_data (Array Int Int))" "$slice_bounds" >/dev/null
grep "(assert (not (and (>= index 0) (< index xs_len))))" "$slice_bounds" >/dev/null
grep "(declare-const flags_data (Array Int Bool))" "$array_ensure" >/dev/null
grep "(assert (= result (select flags_data index)))" "$array_ensure" >/dev/null
grep "(assert (= result xs_len))" "$length_ensure" >/dev/null

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

printf '%s\n' "$output" | grep "  structs: 3" >/dev/null
printf '%s\n' "$output" | grep "  theorems: 1" >/dev/null
printf '%s\n' "$output" | grep "  functions: 4" >/dev/null
printf '%s\n' "$output" | grep "fn.unwrap_bool.assert.1.field_visible" >/dev/null
printf '%s\n' "$output" | grep "fn.make_non_negative.call.1.requires.1.known" >/dev/null
printf '%s\n' "$output" | grep "fn.make_non_negative.struct.box.invariant.1.value_non_negative" >/dev/null

bool_assert="$smt_dir/fn.unwrap_bool.assert.1.field_visible.smt2"
call_requires="$smt_dir/fn.make_non_negative.call.1.requires.1.known.smt2"
generic_invariant="$smt_dir/fn.make_non_negative.struct.box.invariant.1.value_non_negative.smt2"

test -f "$bool_assert"
test -f "$call_requires"
test -f "$generic_invariant"

grep "(set-option :timeout 250)" "$bool_assert" >/dev/null
grep "(declare-const box_value Bool)" "$bool_assert" >/dev/null
grep "(declare-const box_value Int)" "$call_requires" >/dev/null
grep "(assert (not (>= box_value 0)))" "$call_requires" >/dev/null
grep "(assert (= non_negative_i64_call_1_" "$generic_invariant" >/dev/null
grep "(assert (not non_negative_i64_call_1_" "$generic_invariant" >/dev/null

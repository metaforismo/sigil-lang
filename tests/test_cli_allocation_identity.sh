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

printf '%s\n' "$output" | grep "  functions: 5" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 8" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.expose_allocation.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.classify_allocations.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.alias_preserves_allocation.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.model_store_preserves_allocation.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.ref_store_preserves_allocation.ensures.1.preserved" >/dev/null

expose="$smt_dir/fn.expose_allocation.ensures.1.exact.smt2"
classify="$smt_dir/fn.classify_allocations.ensures.1.exact.smt2"
alias="$smt_dir/fn.alias_preserves_allocation.ensures.1.preserved.smt2"
model_store="$smt_dir/fn.model_store_preserves_allocation.ensures.1.preserved.smt2"
ref_store="$smt_dir/fn.ref_store_preserves_allocation.ensures.1.preserved.smt2"

test -f "$expose"
test -f "$classify"
test -f "$alias"
test -f "$model_store"
test -f "$ref_store"

grep "(set-option :timeout 250)" "$expose" >/dev/null
grep "(declare-const xs_alloc Int)" "$expose" >/dev/null
grep "(assert (= result xs_alloc))" "$expose" >/dev/null
grep "(declare-const ptr_alloc Int)" "$classify" >/dev/null
grep "(assert (= result (distinct xs_alloc ptr_alloc)))" "$classify" >/dev/null
grep "(assert (= alias_alloc xs_alloc))" "$alias" >/dev/null
grep "(assert (= updated_alloc xs_alloc))" "$model_store" >/dev/null
grep "(assert (= updated_alloc ptr_alloc))" "$ref_store" >/dev/null

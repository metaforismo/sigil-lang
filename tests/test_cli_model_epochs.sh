#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

smt_dir="$work_dir/smt"
output="$($sigil_bin check "$example_file" --no-z3 --solver-timeout-ms 250 \
  --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 6" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.array_store_advances.ensures.1.advanced" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.view_preserves_epoch.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.deallocation_advances.ensures.1.advanced" >/dev/null

store_smt="$smt_dir/fn.array_store_advances.ensures.1.advanced.smt2"
view_smt="$smt_dir/fn.view_preserves_epoch.ensures.1.preserved.smt2"
borrow_smt="$smt_dir/fn.borrow_preserves_epoch.ensures.1.preserved.smt2"
allocation_smt="$smt_dir/fn.allocations_start_distinct.ensures.1.exact.smt2"
deallocation_smt="$smt_dir/fn.deallocation_advances.ensures.1.advanced.smt2"

test -f "$store_smt"
test -f "$view_smt"
test -f "$borrow_smt"
test -f "$allocation_smt"
test -f "$deallocation_smt"

grep -F "(assert (= updated_epoch (+ values_epoch 1)))" "$store_smt" >/dev/null
grep -F "(assert (= result updated_epoch))" "$store_smt" >/dev/null
grep -F "(assert (= view_epoch values_epoch))" "$view_smt" >/dev/null
grep -F "(assert (= borrowed_epoch values_epoch))" "$borrow_smt" >/dev/null
grep -F "(assert (= first_epoch 0))" "$allocation_smt" >/dev/null
grep -F "(assert (= second_epoch 0))" "$allocation_smt" >/dev/null
grep -F "(assert (distinct second_alloc first_alloc))" "$allocation_smt" >/dev/null
grep -F "(assert (= dead_epoch (+ values_epoch 1)))" "$deallocation_smt" >/dev/null

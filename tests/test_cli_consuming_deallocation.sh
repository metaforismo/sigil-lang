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

printf '%s\n' "$output" | grep "  functions: 3" >/dev/null
printf '%s\n' "$output" | grep "fn.retire_slice.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "fn.retire_slice.safety.3.borrow_free" >/dev/null
printf '%s\n' "$output" | grep "fn.retire_slice.safety.4.allocation_unique" >/dev/null
printf '%s\n' "$output" | grep "fn.retire_slice.safety.8.allocation_unique" >/dev/null
printf '%s\n' "$output" | grep "fn.retire_ref.assert.2.reference_invalid" >/dev/null

slice_dead="$smt_dir/fn.retire_slice.assert.3.allocation_dead.smt2"
slice_moved="$smt_dir/fn.retire_slice.assert.1.move_live.smt2"
ref_epoch="$smt_dir/fn.retire_ref.assert.3.permission_revoked.smt2"
disjoint_unique="$smt_dir/fn.retire_one.safety.4.allocation_unique.smt2"

test -f "$slice_dead"
test -f "$slice_moved"
test -f "$ref_epoch"
test -f "$disjoint_unique"

grep "(assert (= moved_alloc xs_alloc))" "$slice_moved" >/dev/null
grep "(assert (= moved_live xs_live))" "$slice_moved" >/dev/null
grep "(assert (= moved_len xs_len))" "$slice_moved" >/dev/null
grep "(assert (= moved_offset xs_offset))" "$slice_moved" >/dev/null
grep "(assert (= moved_data xs_data))" "$slice_moved" >/dev/null
grep "(assert (= moved_owner xs_owner))" "$slice_moved" >/dev/null
grep "(assert (= moved_has_owner xs_has_owner))" "$slice_moved" >/dev/null
grep "(assert (= moved_shared xs_shared))" "$slice_moved" >/dev/null
grep "(assert (= moved_mut_borrow xs_mut_borrow))" "$slice_moved" >/dev/null
grep "(assert (= dead_alloc moved_alloc))" "$slice_dead" >/dev/null
grep "(assert (= dead_live false))" "$slice_dead" >/dev/null
grep "(assert (= dead_owner 0))" "$slice_dead" >/dev/null
grep "(assert (= dead_has_owner false))" "$slice_dead" >/dev/null
grep "(assert (= dead_shared 0))" "$slice_dead" >/dev/null
grep "(assert (= dead_mut_borrow false))" "$slice_dead" >/dev/null
grep "(assert (= dead_valid false))" "$ref_epoch" >/dev/null
grep "(assert (= dead_write false))" "$ref_epoch" >/dev/null
grep "(assert (= dead_epoch (+ ptr_epoch 1)))" "$ref_epoch" >/dev/null
grep "(assert (distinct left_alloc right_alloc))" "$disjoint_unique" >/dev/null

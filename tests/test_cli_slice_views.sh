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
printf '%s\n' "$output" | grep "fn.read_subview.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "fn.read_subview.safety.2.view_in_bounds" >/dev/null
printf '%s\n' "$output" | grep "fn.read_subview.assert.1.allocation_preserved" >/dev/null
printf '%s\n' "$output" | grep "fn.write_subview.assert.1.offset_preserved" >/dev/null

view_bounds="$smt_dir/fn.read_subview.safety.2.view_in_bounds.smt2"
read_exact="$smt_dir/fn.read_subview.ensures.1.exact.smt2"
write_exact="$smt_dir/fn.write_subview.ensures.1.exact.smt2"
adjacent="$smt_dir/fn.adjacent_views_do_not_overlap.ensures.1.separate.smt2"
identical="$smt_dir/fn.identical_views_match.ensures.1.identical.smt2"
overlapping="$smt_dir/fn.identical_nonempty_views_overlap.ensures.1.overlapping.smt2"
empty="$smt_dir/fn.empty_view_never_overlaps.ensures.1.separate.smt2"

test -f "$view_bounds"
test -f "$read_exact"
test -f "$write_exact"
test -f "$adjacent"
test -f "$identical"
test -f "$overlapping"
test -f "$empty"

grep "(declare-const xs_offset Int)" "$view_bounds" >/dev/null
grep "(assert (>= xs_offset 0))" "$view_bounds" >/dev/null
grep "(assert (not (and (>= start 0) (and (>= count 0) (<= (+ start count) xs_len)))))" \
  "$view_bounds" >/dev/null
grep "(assert (= sub_offset (+ xs_offset start)))" "$read_exact" >/dev/null
grep "(assert (= sub_data xs_data))" "$read_exact" >/dev/null
grep "(assert (= result (select sub_data (+ sub_offset index))))" "$read_exact" >/dev/null
grep "(assert (not (= result (select xs_data (+ xs_offset (+ start index))))))" \
  "$read_exact" >/dev/null
grep "(assert (= updated_offset sub_offset))" "$write_exact" >/dev/null
grep "(assert (= updated_data (store sub_data (+ sub_offset index) value)))" \
  "$write_exact" >/dev/null
grep "(assert (= result (not (and (= left_alloc right_alloc)" "$adjacent" >/dev/null
grep "(< left_offset (+ right_offset right_len))" "$adjacent" >/dev/null
grep "(< right_offset (+ left_offset left_len))" "$adjacent" >/dev/null
grep "(assert (= result (and (= left_alloc right_alloc)" "$identical" >/dev/null
grep "(= left_offset right_offset)" "$identical" >/dev/null
grep "(= left_len right_len)" "$identical" >/dev/null
grep "(assert (= result (and (= left_alloc right_alloc)" "$overlapping" >/dev/null
grep "(assert (> left_len 0))" "$overlapping" >/dev/null
grep "(assert (= empty_len 0))" "$empty" >/dev/null
grep "(> empty_len 0)" "$empty" >/dev/null
grep "(assert (= result (not (and (= empty_alloc other_alloc)" "$empty" >/dev/null

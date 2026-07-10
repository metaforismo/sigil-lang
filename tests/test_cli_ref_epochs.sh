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
printf '%s\n' "$output" | grep "  proof obligations: 7" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.entry_epochs_match.ensures.1.same_entry_epoch" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.2.ownership_present" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.3.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.4.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.5.memory_write" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.ensures.1.next_epoch" >/dev/null

entry_epoch="$smt_dir/fn.entry_epochs_match.ensures.1.same_entry_epoch.smt2"
store_epoch="$smt_dir/fn.store_advances_epoch.ensures.1.next_epoch.smt2"

test -f "$entry_epoch"
test -f "$store_epoch"

grep "(set-option :timeout 250)" "$entry_epoch" >/dev/null
grep "(declare-const __sigil_entry_epoch Int)" "$entry_epoch" >/dev/null
grep "(declare-const left_epoch Int)" "$entry_epoch" >/dev/null
grep "(declare-const right_epoch Int)" "$entry_epoch" >/dev/null
grep "(assert (= left_epoch __sigil_entry_epoch))" "$entry_epoch" >/dev/null
grep "(assert (= right_epoch __sigil_entry_epoch))" "$entry_epoch" >/dev/null
grep "(assert (not (= left_epoch right_epoch)))" "$entry_epoch" >/dev/null

grep "(declare-const ptr_epoch Int)" "$store_epoch" >/dev/null
grep "(declare-const updated_epoch Int)" "$store_epoch" >/dev/null
grep "(assert (= ptr_epoch __sigil_entry_epoch))" "$store_epoch" >/dev/null
grep "(assert (= updated_epoch (+ ptr_epoch 1)))" "$store_epoch" >/dev/null
grep "(assert (= result updated_epoch))" "$store_epoch" >/dev/null
grep "(assert (not (= result (+ ptr_epoch 1))))" "$store_epoch" >/dev/null

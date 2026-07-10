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
printf '%s\n' "$output" | grep \
  "fn.initialize_slot.safety.11.memory_initialized" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.initialization_follows_views.assert.2.view_uninitialized" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.initialized_constructor.assert.2.second_initialized" >/dev/null

raw_smt="$smt_dir/fn.initialize_slot.assert.1.starts_uninitialized.smt2"
initialized_smt="$smt_dir/fn.initialize_slot.assert.2.marked_initialized.smt2"
view_smt="$smt_dir/fn.initialization_follows_views.assert.2.view_uninitialized.smt2"
full_smt="$smt_dir/fn.initialized_constructor.assert.1.first_initialized.smt2"

test -f "$raw_smt"
test -f "$initialized_smt"
test -f "$view_smt"
test -f "$full_smt"

grep "(assert (= raw_init ((as const (Array Int Bool)) false)))" "$raw_smt" >/dev/null
grep "(assert (= exclusive_init raw_init))" "$initialized_smt" >/dev/null
grep "(assert (= initialized_init (store exclusive_init (+ exclusive_offset index) true)))" \
  "$initialized_smt" >/dev/null
grep "(assert (= view_init raw_init))" "$view_smt" >/dev/null
grep "(assert (= values_init ((as const (Array Int Bool)) true)))" "$full_smt" >/dev/null

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

printf '%s\n' "$output" | grep "  structs: 0" >/dev/null
printf '%s\n' "$output" | grep "  containers: 1" >/dev/null
printf '%s\n' "$output" | grep "  functions: 1" >/dev/null
printf '%s\n' "$output" | grep "fn.read_window.container.window.invariant.1.index_non_negative" >/dev/null
printf '%s\n' "$output" | grep "fn.read_window.container.window.invariant.2.index_within_items" >/dev/null
printf '%s\n' "$output" | grep "fn.read_window.assert.1.len_visible" >/dev/null
printf '%s\n' "$output" | grep "fn.read_window.assert.2.index_visible" >/dev/null
printf '%s\n' "$output" | grep "fn.read_window.safety.1.memory_live" >/dev/null

len_assert="$smt_dir/fn.read_window.assert.1.len_visible.smt2"
index_assert="$smt_dir/fn.read_window.assert.2.index_visible.smt2"
container_invariant="$smt_dir/fn.read_window.container.window.invariant.2.index_within_items.smt2"
live_gate="$smt_dir/fn.read_window.safety.1.memory_live.smt2"

test -f "$len_assert"
test -f "$index_assert"
test -f "$container_invariant"
test -f "$live_gate"

grep "(set-option :timeout 250)" "$len_assert" >/dev/null
grep "(declare-const window_items_len Int)" "$len_assert" >/dev/null
grep "(declare-const window_items_data (Array Int Int))" "$len_assert" >/dev/null
grep "(assert (= window_items_len xs_len))" "$len_assert" >/dev/null
grep "(assert (= window_items_data xs_data))" "$len_assert" >/dev/null
grep "(assert (not (= window_items_len xs_len)))" "$len_assert" >/dev/null
grep "(assert (= window_index index))" "$index_assert" >/dev/null
grep "(assert (not (< window_index window_items_len)))" "$container_invariant" >/dev/null
grep "(assert xs_live)" "$live_gate" >/dev/null
grep "(assert (not xs_live))" "$live_gate" >/dev/null

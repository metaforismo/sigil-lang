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
  "fn.read_initialized.safety.3.memory_initialized" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.call_with_partial.call.1.requires.3.initialized" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.call_with_full.call.1.requires.3.initialized" >/dev/null

read_gate="$smt_dir/fn.read_initialized.safety.3.memory_initialized.smt2"
partial_call="$smt_dir/fn.call_with_partial.call.1.requires.3.initialized.smt2"
full_call="$smt_dir/fn.call_with_full.call.1.requires.3.initialized.smt2"

test -f "$read_gate"
test -f "$partial_call"
test -f "$full_call"

grep -F "(assert (select values_init (+ values_offset index)))" "$read_gate" >/dev/null
if grep -F "(assert (= values_init ((as const (Array Int Bool)) true)))" \
  "$read_gate" >/dev/null; then
  echo "function entry unexpectedly assumes a fully initialized container" >&2
  exit 1
fi
grep -F \
  "(assert (= initialized_init (store exclusive_init (+ exclusive_offset index) true)))" \
  "$partial_call" >/dev/null
grep -F "(assert (not (select initialized_init (+ initialized_offset index))))" \
  "$partial_call" >/dev/null
grep -F "(assert (= initialized_init ((as const (Array Int Bool)) true)))" \
  "$full_call" >/dev/null
grep -F "(assert (not (select initialized_init (+ initialized_offset 0))))" \
  "$full_call" >/dev/null

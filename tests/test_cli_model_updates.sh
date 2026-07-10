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
printf '%s\n' "$output" | grep "  proof obligations: 17" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.safety.2.ownership_present" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.safety.3.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.safety.4.index_in_bounds" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.safety.7.memory_initialized" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.assert.1.length_preserved" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_read.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.update_flag.ensures.1.exact" >/dev/null

write_store="$smt_dir/fn.write_then_read.ensures.1.exact.smt2"
write_len="$smt_dir/fn.write_then_read.assert.1.length_preserved.smt2"
flag_store="$smt_dir/fn.update_flag.ensures.1.exact.smt2"

test -f "$write_store"
test -f "$write_len"
test -f "$flag_store"

grep "(set-option :timeout 250)" "$write_store" >/dev/null
grep "(declare-const updated_len Int)" "$write_store" >/dev/null
grep "(declare-const updated_data (Array Int Int))" "$write_store" >/dev/null
grep "(declare-const updated_offset Int)" "$write_store" >/dev/null
grep "(assert (= updated_len xs_len))" "$write_store" >/dev/null
grep "(assert (= updated_live xs_live))" "$write_store" >/dev/null
grep "(assert (= updated_offset xs_offset))" "$write_store" >/dev/null
grep "(assert (= updated_data (store xs_data (+ xs_offset index) value)))" "$write_store" >/dev/null
grep "(assert (= updated_init (store xs_init (+ xs_offset index) true)))" "$write_store" >/dev/null
grep "(assert (= result (select updated_data (+ updated_offset index))))" "$write_store" >/dev/null
grep "(assert (not (= result value)))" "$write_store" >/dev/null
grep "(assert (= updated_len xs_len))" "$write_len" >/dev/null
grep "(assert (not (= updated_len xs_len)))" "$write_len" >/dev/null
grep "(declare-const updated_data (Array Int Bool))" "$flag_store" >/dev/null
grep "(assert (= updated_offset 0))" "$flag_store" >/dev/null
grep "(assert (= updated_data (store flags_data (+ flags_offset index) value)))" \
  "$flag_store" >/dev/null
grep "(assert (= updated_init (store flags_init (+ flags_offset index) true)))" \
  "$flag_store" >/dev/null

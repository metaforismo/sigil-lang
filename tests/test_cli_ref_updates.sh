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
printf '%s\n' "$output" | grep "  proof obligations: 11" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.safety.1.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.safety.2.memory_write" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.assert.1.still_valid" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.assert.2.still_writable" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.assert.3.same_address" >/dev/null
printf '%s\n' "$output" | grep "fn.write_then_load.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.update_flag_ref.ensures.1.exact" >/dev/null

write_store="$smt_dir/fn.write_then_load.ensures.1.exact.smt2"
write_valid="$smt_dir/fn.write_then_load.safety.1.memory_valid.smt2"
write_allowed="$smt_dir/fn.write_then_load.safety.2.memory_write.smt2"
flag_store="$smt_dir/fn.update_flag_ref.ensures.1.exact.smt2"

test -f "$write_store"
test -f "$write_valid"
test -f "$write_allowed"
test -f "$flag_store"

grep "(set-option :timeout 250)" "$write_store" >/dev/null
grep "(declare-const updated_addr Int)" "$write_store" >/dev/null
grep "(declare-const updated_valid Bool)" "$write_store" >/dev/null
grep "(declare-const updated_value Int)" "$write_store" >/dev/null
grep "(declare-const ptr_write Bool)" "$write_store" >/dev/null
grep "(declare-const updated_write Bool)" "$write_store" >/dev/null
grep "(declare-const ptr_epoch Int)" "$write_store" >/dev/null
grep "(declare-const updated_epoch Int)" "$write_store" >/dev/null
grep "(assert (= updated_addr ptr_addr))" "$write_store" >/dev/null
grep "(assert (= updated_valid ptr_valid))" "$write_store" >/dev/null
grep "(assert (= updated_write ptr_write))" "$write_store" >/dev/null
grep "(assert (= updated_value value))" "$write_store" >/dev/null
grep "(assert (= updated_epoch (+ ptr_epoch 1)))" "$write_store" >/dev/null
if grep "(assert (= ptr_value updated_value))" "$write_store" >/dev/null; then
  exit 1
fi
if grep "(assert (= updated_value ptr_value))" "$write_store" >/dev/null; then
  exit 1
fi
grep "(assert (= result updated_value))" "$write_store" >/dev/null
grep "(assert (not (= result value)))" "$write_store" >/dev/null
grep "(assert (not ptr_valid))" "$write_valid" >/dev/null
grep "(assert ptr_write)" "$write_allowed" >/dev/null
grep "(assert (not ptr_write))" "$write_allowed" >/dev/null
grep "(declare-const updated_value Bool)" "$flag_store" >/dev/null
grep "(assert (= updated_value value))" "$flag_store" >/dev/null

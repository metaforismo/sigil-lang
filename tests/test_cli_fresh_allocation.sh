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

printf '%s\n' "$output" | grep "  containers: 1" >/dev/null
printf '%s\n' "$output" | grep "  functions: 4" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.make_slice.safety.1.allocation_size_nonnegative" >/dev/null
printf '%s\n' "$output" | grep "fn.make_slice.assert.1.exact_length" >/dev/null
printf '%s\n' "$output" | grep "fn.make_models.assert.1.fresh_ref_allocation" >/dev/null
printf '%s\n' "$output" | grep "fn.no_identity_reuse.assert.1.fresh_lifetime" >/dev/null

slice_smt="$smt_dir/fn.make_slice.assert.1.exact_length.smt2"
models_smt="$smt_dir/fn.make_models.assert.3.distinct_models.smt2"
lifetime_smt="$smt_dir/fn.no_identity_reuse.assert.1.fresh_lifetime.smt2"

test -f "$slice_smt"
test -f "$models_smt"
test -f "$lifetime_smt"

grep "(assert (= values_len length))" "$slice_smt" >/dev/null
grep "(assert (= values_offset 0))" "$slice_smt" >/dev/null
grep "(assert (= values_live true))" "$slice_smt" >/dev/null
grep "(assert (= values_has_owner true))" "$slice_smt" >/dev/null
grep "(assert (= values_shared 0))" "$slice_smt" >/dev/null
grep "(assert (= values_mut_borrow false))" "$slice_smt" >/dev/null
grep "(assert (= values_data ((as const (Array Int Int)) initial)))" "$slice_smt" >/dev/null
grep "(assert (distinct flag_alloc existing_alloc))" "$models_smt" >/dev/null
grep "(assert (distinct bits_alloc existing_alloc))" "$models_smt" >/dev/null
grep "(assert (distinct bits_alloc flag_alloc))" "$models_smt" >/dev/null
grep "(assert (= flag_valid true))" "$models_smt" >/dev/null
grep "(assert (= flag_write true))" "$models_smt" >/dev/null
grep "(assert (= flag_value true))" "$models_smt" >/dev/null
grep "(assert (= flag_epoch 0))" "$models_smt" >/dev/null
grep "(assert (distinct number_addr existing_addr))" "$models_smt" >/dev/null
grep "(assert (distinct number_addr flag_addr))" "$models_smt" >/dev/null
grep "(assert (= bits_data ((as const (Array Int Bool)) false)))" "$models_smt" >/dev/null
grep "(assert (distinct replacement_alloc old_alloc))" "$lifetime_smt" >/dev/null
grep "(assert (distinct replacement_alloc dead_alloc))" "$lifetime_smt" >/dev/null

#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"
smt_dir="$work_dir/smt"
output="$("$sigil_bin" check "$example_file" --no-z3 --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 6" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 15" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.shared_count_nonnegative.ensures.1.nonnegative" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.mutable_excludes_shared.ensures.1.consistent" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.owner_has_identity.ensures.1.nonzero" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.alias_preserves_owner.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.model_store_preserves_borrows.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.ref_store_preserves_borrows.ensures.1.preserved" >/dev/null

alias_smt="$smt_dir/fn.alias_preserves_owner.ensures.1.preserved.smt2"
model_smt="$smt_dir/fn.model_store_preserves_borrows.ensures.1.preserved.smt2"
ref_smt="$smt_dir/fn.ref_store_preserves_borrows.ensures.1.preserved.smt2"
grep "(assert (= alias_owner xs_owner))" "$alias_smt" >/dev/null
grep "(assert (= alias_has_owner xs_has_owner))" "$alias_smt" >/dev/null
grep "(assert (= alias_shared xs_shared))" "$alias_smt" >/dev/null
grep "(assert (= alias_mut_borrow xs_mut_borrow))" "$alias_smt" >/dev/null
grep "(assert (= updated_shared xs_shared))" "$model_smt" >/dev/null
grep "(assert (= updated_mut_borrow xs_mut_borrow))" "$model_smt" >/dev/null
grep "(assert (= updated_owner ptr_owner))" "$ref_smt" >/dev/null
grep "(assert (= updated_has_owner ptr_has_owner))" "$ref_smt" >/dev/null

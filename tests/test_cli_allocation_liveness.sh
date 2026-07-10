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

printf '%s\n' "$output" | grep "  functions: 5" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 22" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.expose_liveness.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_live_slice.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_live_ref.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.model_store_preserves_liveness.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.ref_store_preserves_liveness.ensures.1.preserved" >/dev/null

expose="$smt_dir/fn.expose_liveness.ensures.1.exact.smt2"
slice_live="$smt_dir/fn.read_live_slice.safety.1.memory_live.smt2"
model_store="$smt_dir/fn.model_store_preserves_liveness.ensures.1.preserved.smt2"
ref_store="$smt_dir/fn.ref_store_preserves_liveness.ensures.1.preserved.smt2"

grep "(declare-const xs_live Bool)" "$expose" >/dev/null
grep "(assert (= result xs_live))" "$expose" >/dev/null
grep "(assert xs_live)" "$slice_live" >/dev/null
grep "(assert (not xs_live))" "$slice_live" >/dev/null
grep "(assert (= updated_live xs_live))" "$model_store" >/dev/null
grep "(assert (= updated_live ptr_live))" "$ref_store" >/dev/null

missing_live="$work_dir/missing_live.sigil"
printf '%s\n' \
  'module missing_live;' \
  '' \
  'fn unsafe_read(xs: Slice[i64], index: i64) -> i64' \
  'requires in_bounds: index >= 0 && index < len(xs);' \
  '{' \
  '  return at(xs, index);' \
  '}' >"$missing_live"

set +e
missing_output="$("$sigil_bin" check "$missing_live" --strict --no-z3 2>&1)"
missing_status=$?
set -e

test "$missing_status" -eq 2
printf '%s\n' "$missing_output" | grep "\[UNKNOWN\] fn.unsafe_read.safety.1.memory_live" >/dev/null
printf '%s\n' "$missing_output" | grep "\[PROVEN\] fn.unsafe_read.safety.2.index_in_bounds" >/dev/null

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
printf '%s\n' "$output" | grep "  proof obligations: 19" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.shared_round_trip.safety.3.shared_borrow_available" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.shared_round_trip.safety.6.shared_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.shared_round_trip.assert.2.decremented" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.mutable_round_trip.safety.3.mutable_borrow_available" >/dev/null
printf '%s\n' "$output" | grep "\[UNKNOWN\] fn.mutable_round_trip.assert.3.inactive" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.shared_round_trip.ensures.1.restored" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.mutable_round_trip.ensures.1.released" >/dev/null
shared="$smt_dir/fn.shared_round_trip.ensures.1.restored.smt2"
mutable="$smt_dir/fn.mutable_round_trip.ensures.1.released.smt2"
grep "(assert (= borrowed_shared (+ xs_shared 1)))" "$shared" >/dev/null
grep "(assert (= released_shared (- borrowed_shared 1)))" "$shared" >/dev/null
grep "(assert borrowed_mut_borrow)" "$mutable" >/dev/null
grep "(assert (not released_mut_borrow))" "$mutable" >/dev/null

missing="$work_dir/missing_guard.sigil"
printf '%s\n' \
  'module missing_guard;' \
  '' \
  'fn unsafe_shared_borrow(xs: Slice[i64]) -> i64' \
  'requires live: is_live(xs);' \
  'requires owned: has_owner(xs);' \
  '{' \
  '  let borrowed: Slice[i64] = borrow_shared(xs);' \
  '  return shared_borrows(borrowed);' \
  '}' >"$missing"
set +e
missing_output="$("$sigil_bin" check "$missing" --strict --no-z3 2>&1)"
missing_status=$?
set -e
test "$missing_status" -eq 2
printf '%s\n' "$missing_output" | grep "\[PROVEN\] fn.unsafe_shared_borrow.safety.1.memory_live" >/dev/null
printf '%s\n' "$missing_output" | grep "\[PROVEN\] fn.unsafe_shared_borrow.safety.2.ownership_present" >/dev/null
printf '%s\n' "$missing_output" | grep "\[UNKNOWN\] fn.unsafe_shared_borrow.safety.3.shared_borrow_available" >/dev/null

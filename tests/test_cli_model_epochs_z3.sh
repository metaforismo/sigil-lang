#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

output="$($sigil_bin check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"
test "$(printf '%s\n' "$output" | grep -c '^\[PROVEN\]')" -gt 0
test "$(printf '%s\n' "$output" | grep -c '^\[REFUTED\]\|^\[UNKNOWN\]\|^\[ERROR\]')" -eq 0

invalid_source="$work_dir/invalid-epochs.sigil"
cat >"$invalid_source" <<'SIGIL'
module invalid_epochs;

fn store_is_same(values: Array[i64], index: i64, value: i64) -> bool
requires live: is_live(values);
requires owned: has_owner(values);
requires exclusive: has_mut_borrow(values);
requires in_bounds: index >= 0 && index < len(values);
ensures invalid: result;
{
  let updated: Array[i64] = store(values, index, value);
  return same_snapshot(updated, values);
}

fn fresh_are_same() -> bool
{
  let first: Array[i64] = allocate_array(1, 0);
  let second: Slice[i64] = allocate_slice(1, 0);
  assert invalid: same_snapshot(first, second);
  return true;
}

fn deallocation_preserves(values: Slice[i64]) -> bool
requires live: is_live(values);
requires owned: has_owner(values);
requires no_shared: shared_borrows(values) == 0;
requires no_mutable: !has_mut_borrow(values);
{
  let before: i64 = epoch(values);
  let dead: Slice[i64] = deallocate(values);
  assert invalid: epoch(dead) == before;
  return true;
}
SIGIL

set +e
invalid_output="$($sigil_bin check "$invalid_source" --strict --solver-timeout-ms 250 2>&1)"
invalid_status=$?
set -e
printf '%s\n' "$invalid_output"
test "$invalid_status" -eq 2
printf '%s\n' "$invalid_output" | grep \
  "\[REFUTED\] fn.store_is_same.ensures.1.invalid" >/dev/null
printf '%s\n' "$invalid_output" | grep \
  "\[REFUTED\] fn.fresh_are_same.assert.1.invalid" >/dev/null
printf '%s\n' "$invalid_output" | grep \
  "\[REFUTED\] fn.deallocation_preserves.assert.1.invalid" >/dev/null

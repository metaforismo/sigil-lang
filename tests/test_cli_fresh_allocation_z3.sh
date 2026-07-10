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

negative_source="$work_dir/negative-size.sigil"
cat >"$negative_source" <<'SIGIL'
module negative_size;

fn invalid() -> bool
{
  let values: Slice[i64] = allocate_slice(-1, 0);
  return is_live(values);
}
SIGIL

set +e
negative_output="$($sigil_bin check "$negative_source" --strict --solver-timeout-ms 250 2>&1)"
negative_status=$?
set -e
printf '%s\n' "$negative_output"
test "$negative_status" -eq 2
printf '%s\n' "$negative_output" | grep \
  "\[REFUTED\] fn.invalid.safety.1.allocation_size_nonnegative" >/dev/null

false_claims="$work_dir/false-claims.sigil"
cat >"$false_claims" <<'SIGIL'
module false_claims;

fn not_live() -> bool
{
  let value: Ref[i64] = allocate_ref(4);
  assert false_live: !is_live(value);
  return true;
}

fn not_owned() -> bool
{
  let value: Slice[i64] = allocate_slice(1, 4);
  assert false_owner: !has_owner(value);
  return true;
}

fn wrong_initial() -> bool
{
  let value: Array[i64] = allocate_array(1, 4);
  assert false_initial: at(value, 0) == 5;
  return true;
}

fn not_fresh(existing: Ref[i64]) -> bool
{
  let value: Ref[i64] = allocate_ref(4);
  assert false_fresh: same_allocation(existing, value);
  return true;
}

fn same_address(existing: Ref[i64]) -> bool
{
  let value: Ref[i64] = allocate_ref(4);
  assert false_address: same_ref(existing, value);
  return true;
}

fn zero_owner() -> bool
{
  let value: Ref[i64] = allocate_ref(4);
  assert false_owner_id: owner_id(value) == 0;
  return true;
}

fn same_new_allocations() -> bool
{
  let first: Slice[i64] = allocate_slice(0, 1);
  let second: Slice[i64] = allocate_slice(0, 1);
  assert false_distinctness: same_allocation(first, second);
  return true;
}
SIGIL

set +e
false_output="$($sigil_bin check "$false_claims" --strict --solver-timeout-ms 250 2>&1)"
false_status=$?
set -e
printf '%s\n' "$false_output"
test "$false_status" -eq 2
printf '%s\n' "$false_output" | grep "\[REFUTED\] fn.not_live.assert.1.false_live" >/dev/null
printf '%s\n' "$false_output" | grep "\[REFUTED\] fn.not_owned.assert.1.false_owner" >/dev/null
printf '%s\n' "$false_output" | grep \
  "\[REFUTED\] fn.wrong_initial.assert.1.false_initial" >/dev/null
printf '%s\n' "$false_output" | grep "\[REFUTED\] fn.not_fresh.assert.1.false_fresh" >/dev/null
printf '%s\n' "$false_output" | grep \
  "\[REFUTED\] fn.same_address.assert.1.false_address" >/dev/null
printf '%s\n' "$false_output" | grep \
  "\[REFUTED\] fn.zero_owner.assert.1.false_owner_id" >/dev/null
printf '%s\n' "$false_output" | grep \
  "\[REFUTED\] fn.same_new_allocations.assert.1.false_distinctness" >/dev/null

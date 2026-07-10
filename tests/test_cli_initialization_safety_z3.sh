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

raw_read_source="$work_dir/raw-read.sigil"
cat >"$raw_read_source" <<'SIGIL'
module raw_read;

fn invalid() -> i64
{
  let raw: Slice[i64] = allocate_uninit_slice(1, 0);
  return at(raw, 0);
}
SIGIL

set +e
raw_output="$($sigil_bin check "$raw_read_source" --strict --solver-timeout-ms 250 2>&1)"
raw_status=$?
set -e
printf '%s\n' "$raw_output"
test "$raw_status" -eq 2
printf '%s\n' "$raw_output" | grep \
  "\[REFUTED\] fn.invalid.safety.4.memory_initialized" >/dev/null

partial_source="$work_dir/partial.sigil"
cat >"$partial_source" <<'SIGIL'
module partial;

fn wrong_slot() -> i64
{
  let raw: Array[i64] = allocate_uninit_array(2, 0);
  let exclusive: Array[i64] = borrow_mut(raw);
  let initialized: Array[i64] = store(exclusive, 0, 9);
  return at(initialized, 1);
}

fn false_initialization() -> bool
{
  let raw: Slice[bool] = allocate_uninit_slice(1, false);
  assert false_claim: is_initialized(raw, 0);
  return true;
}

fn out_of_bounds_store() -> bool
{
  let raw: Slice[i64] = allocate_uninit_slice(1, 0);
  let exclusive: Slice[i64] = borrow_mut(raw);
  let initialized: Slice[i64] = store(exclusive, 1, 9);
  return is_initialized(initialized, 1);
}

fn negative_raw_length() -> bool
{
  let raw: Array[i64] = allocate_uninit_array(-1, 0);
  return !is_initialized(raw, 0);
}
SIGIL

set +e
partial_output="$($sigil_bin check "$partial_source" --strict --solver-timeout-ms 250 2>&1)"
partial_status=$?
set -e
printf '%s\n' "$partial_output"
test "$partial_status" -eq 2
printf '%s\n' "$partial_output" | grep \
  "\[REFUTED\] fn.wrong_slot.safety.11.memory_initialized" >/dev/null
printf '%s\n' "$partial_output" | grep \
  "\[REFUTED\] fn.false_initialization.assert.1.false_claim" >/dev/null
printf '%s\n' "$partial_output" | grep \
  "\[REFUTED\] fn.out_of_bounds_store.safety.8.index_in_bounds" >/dev/null
printf '%s\n' "$partial_output" | grep \
  "\[REFUTED\] fn.negative_raw_length.safety.1.allocation_size_nonnegative" >/dev/null

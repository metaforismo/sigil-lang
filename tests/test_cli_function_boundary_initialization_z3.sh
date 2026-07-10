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

unsafe_source="$work_dir/unsafe-boundaries.sigil"
cat >"$unsafe_source" <<'SIGIL'
module unsafe_boundaries;

fn read_without_contract(values: Slice[i64], index: i64) -> i64
requires live: is_live(values);
requires in_bounds: index >= 0 && index < len(values);
{
  return at(values, index);
}

fn read_initialized(values: Slice[i64], index: i64) -> i64
requires live: is_live(values);
requires in_bounds: index >= 0 && index < len(values);
requires initialized: is_initialized(values, index);
{
  return at(values, index);
}

fn pass_raw() -> i64
{
  let raw: Slice[i64] = allocate_uninit_slice(1, 0);
  return read_initialized(raw, 0);
}

fn pass_wrong_slot() -> i64
{
  let raw: Slice[i64] = allocate_uninit_slice(2, 0);
  let exclusive: Slice[i64] = borrow_mut(raw);
  let initialized: Slice[i64] = store(exclusive, 0, 7);
  return read_initialized(initialized, 1);
}
SIGIL

set +e
unsafe_output="$($sigil_bin check "$unsafe_source" --strict --solver-timeout-ms 250 2>&1)"
unsafe_status=$?
set -e
printf '%s\n' "$unsafe_output"
test "$unsafe_status" -eq 2
printf '%s\n' "$unsafe_output" | grep \
  "\[REFUTED\] fn.read_without_contract.safety.3.memory_initialized" >/dev/null
printf '%s\n' "$unsafe_output" | grep \
  "\[REFUTED\] fn.pass_raw.call.1.requires.3.initialized" >/dev/null
printf '%s\n' "$unsafe_output" | grep \
  "\[REFUTED\] fn.pass_wrong_slot.call.1.requires.3.initialized" >/dev/null

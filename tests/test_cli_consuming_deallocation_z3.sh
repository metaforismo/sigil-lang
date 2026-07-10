#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

output="$($sigil_bin check "$example_file" --strict --solver-timeout-ms 250 \
  --save-smt "$work_dir/smt")"
printf '%s\n' "$output"
test "$(printf '%s\n' "$output" | grep -c '^\[PROVEN\]')" -gt 0
test "$(printf '%s\n' "$output" | grep -c '^\[REFUTED\]\|^\[UNKNOWN\]\|^\[ERROR\]')" -eq 0

alias_source="$work_dir/alias.sigil"
cat >"$alias_source" <<'SIGIL'
module aliased_deallocation;

fn retire_alias(xs: Slice[i64]) -> bool
requires live: is_live(xs);
requires owned: has_owner(xs);
requires no_shared: shared_borrows(xs) == 0;
requires no_mutable: !has_mut_borrow(xs);
{
  let alias: Slice[i64] = xs;
  let dead: Slice[i64] = deallocate(xs);
  return !is_live(dead);
}
SIGIL

set +e
alias_output="$($sigil_bin check "$alias_source" --strict --solver-timeout-ms 250 \
  --save-smt "$work_dir/alias-smt" 2>&1)"
alias_status=$?
set -e
printf '%s\n' "$alias_output"
test "$alias_status" -eq 2
printf '%s\n' "$alias_output" | grep \
  "\[REFUTED\] fn.retire_alias.safety.4.allocation_unique" >/dev/null

missing_source="$work_dir/missing-disjoint.sigil"
cat >"$missing_source" <<'SIGIL'
module missing_disjoint;

fn retire_left(left: Slice[i64], right: Slice[i64]) -> bool
requires live: is_live(left);
requires owned: has_owner(left);
requires no_shared: shared_borrows(left) == 0;
requires no_mutable: !has_mut_borrow(left);
{
  let dead: Slice[i64] = deallocate(left);
  return !is_live(dead);
}
SIGIL

set +e
missing_output="$($sigil_bin check "$missing_source" --strict --solver-timeout-ms 250 2>&1)"
missing_status=$?
set -e
printf '%s\n' "$missing_output"
test "$missing_status" -eq 2
printf '%s\n' "$missing_output" | grep \
  "\[REFUTED\] fn.retire_left.safety.4.allocation_unique" >/dev/null

guard_source="$work_dir/transition-guards.sigil"
cat >"$guard_source" <<'SIGIL'
module transition_guards;

fn reject_dead(xs: Slice[i64]) -> bool
requires dead: !is_live(xs);
requires owned: has_owner(xs);
requires no_shared: shared_borrows(xs) == 0;
requires no_mutable: !has_mut_borrow(xs);
{
  let retired: Slice[i64] = deallocate(xs);
  return !is_live(retired);
}

fn reject_unowned(xs: Slice[i64]) -> bool
requires live: is_live(xs);
requires unowned: !has_owner(xs);
requires no_shared: shared_borrows(xs) == 0;
requires no_mutable: !has_mut_borrow(xs);
{
  let retired: Slice[i64] = deallocate(xs);
  return !is_live(retired);
}

fn reject_shared(xs: Slice[i64]) -> bool
requires live: is_live(xs);
requires owned: has_owner(xs);
requires shared: shared_borrows(xs) > 0;
requires no_mutable: !has_mut_borrow(xs);
{
  let retired: Slice[i64] = deallocate(xs);
  return !is_live(retired);
}

fn reject_mutable(xs: Slice[i64]) -> bool
requires live: is_live(xs);
requires owned: has_owner(xs);
requires no_shared: shared_borrows(xs) == 0;
requires mutable: has_mut_borrow(xs);
{
  let retired: Slice[i64] = deallocate(xs);
  return !is_live(retired);
}
SIGIL

set +e
guard_output="$($sigil_bin check "$guard_source" --strict --solver-timeout-ms 250 2>&1)"
guard_status=$?
set -e
printf '%s\n' "$guard_output"
test "$guard_status" -eq 2
printf '%s\n' "$guard_output" | grep \
  "\[REFUTED\] fn.reject_dead.safety.1.memory_live" >/dev/null
printf '%s\n' "$guard_output" | grep \
  "\[REFUTED\] fn.reject_unowned.safety.2.ownership_present" >/dev/null
printf '%s\n' "$guard_output" | grep \
  "\[REFUTED\] fn.reject_shared.safety.3.borrow_free" >/dev/null
printf '%s\n' "$guard_output" | grep \
  "\[REFUTED\] fn.reject_mutable.safety.3.borrow_free" >/dev/null

tombstone_source="$work_dir/tombstone-access.sigil"
cat >"$tombstone_source" <<'SIGIL'
module tombstone_access;

fn read_dead(xs: Slice[i64]) -> i64
requires live: is_live(xs);
requires owned: has_owner(xs);
requires no_shared: shared_borrows(xs) == 0;
requires no_mutable: !has_mut_borrow(xs);
requires nonempty: len(xs) > 0;
{
  let dead: Slice[i64] = deallocate(xs);
  return at(dead, 0);
}
SIGIL

set +e
tombstone_output="$($sigil_bin check "$tombstone_source" --strict --solver-timeout-ms 250 2>&1)"
tombstone_status=$?
set -e
printf '%s\n' "$tombstone_output"
test "$tombstone_status" -eq 2
printf '%s\n' "$tombstone_output" | grep \
  "\[REFUTED\] fn.read_dead.safety.5.memory_live" >/dev/null

use_after_move="$work_dir/use-after-move.sigil"
cat >"$use_after_move" <<'SIGIL'
module use_after_move;

fn invalid(xs: Slice[i64]) -> i64
{
  let moved: Slice[i64] = move_owner(xs);
  return len(xs);
}
SIGIL

set +e
use_output="$($sigil_bin check "$use_after_move" --strict 2>&1)"
use_status=$?
set -e
printf '%s\n' "$use_output"
test "$use_status" -eq 1
printf '%s\n' "$use_output" | grep "use of consumed model value 'xs'" >/dev/null

nested_source="$work_dir/nested-consume.sigil"
cat >"$nested_source" <<'SIGIL'
module nested_consume;

fn invalid(xs: Slice[i64], flag: bool) -> bool
{
  if flag {
    let dead: Slice[i64] = deallocate(xs);
  } else {
  }
  return true;
}
SIGIL

set +e
nested_output="$($sigil_bin check "$nested_source" --strict 2>&1)"
nested_status=$?
set -e
printf '%s\n' "$nested_output"
test "$nested_status" -eq 1
printf '%s\n' "$nested_output" | grep \
  "consuming transition 'deallocate' is only supported in the function body root" >/dev/null

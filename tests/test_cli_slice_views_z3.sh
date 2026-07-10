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
printf '%s\n' "$output" | grep "fn.read_subview.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.write_subview.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "fn.adjacent_views_do_not_overlap.ensures.1.separate" >/dev/null
printf '%s\n' "$output" | grep "fn.identical_views_match.ensures.1.identical" >/dev/null
printf '%s\n' "$output" | grep \
  "fn.identical_nonempty_views_overlap.ensures.1.overlapping" >/dev/null
printf '%s\n' "$output" | grep "fn.empty_view_never_overlaps.ensures.1.separate" >/dev/null

invalid_source="$work_dir/invalid-view.sigil"
cat >"$invalid_source" <<'SIGIL'
module invalid_view;

fn oversized(xs: Slice[i64]) -> i64
requires live: is_live(xs);
{
  let invalid: Slice[i64] = slice_view(xs, 0, len(xs) + 1);
  return len(invalid);
}
SIGIL

set +e
invalid_output="$($sigil_bin check "$invalid_source" --strict --solver-timeout-ms 250 \
  --save-smt "$work_dir/invalid-smt" 2>&1)"
invalid_status=$?
set -e
printf '%s\n' "$invalid_output"

test "$invalid_status" -eq 2
printf '%s\n' "$invalid_output" | grep \
  "\[REFUTED\] fn.oversized.safety.2.view_in_bounds" >/dev/null
grep "(assert (not (and (>= 0 0)" \
  "$work_dir/invalid-smt/fn.oversized.safety.2.view_in_bounds.smt2" >/dev/null

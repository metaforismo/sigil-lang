#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

candidate="$work_dir/candidate_arithmetic.sigil"
smt_dir="$work_dir/smt"

cat >"$candidate" <<'SIGIL'
module candidate_arithmetic;

theorem add_one_gt for (x: i64)
ensures advanced: x + 1 > x;
{
  return x + 1 > x;
}
SIGIL

output="$("$sigil_bin" agent-check "$candidate" --strict --solver-timeout-ms 250 --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "agent-candidate candidate_arithmetic" >/dev/null
printf '%s\n' "$output" | grep "  theorems: 1" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 2" >/dev/null
printf '%s\n' "$output" | grep "  status: accepted" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] theorem.add_one_gt.ensures.1.advanced" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] theorem.add_one_gt.ensures.2.holds" >/dev/null

test -f "$smt_dir/theorem.add_one_gt.ensures.1.advanced.smt2"
test -f "$smt_dir/theorem.add_one_gt.ensures.2.holds.smt2"
grep "(set-option :timeout 250)" "$smt_dir/theorem.add_one_gt.ensures.1.advanced.smt2" >/dev/null
grep "(assert (not (> (+ x 1) x)))" "$smt_dir/theorem.add_one_gt.ensures.1.advanced.smt2" >/dev/null

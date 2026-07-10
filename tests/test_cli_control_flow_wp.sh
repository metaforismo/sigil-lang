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

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 6" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.choose_nonnegative.ensures.1.nonnegative - proved by control-flow weakest-precondition" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.summarize_counter.ensures.1.bounded - proved by control-flow weakest-precondition" >/dev/null

branch_smt="$smt_dir/fn.choose_nonnegative.ensures.1.nonnegative.smt2"
loop_smt="$smt_dir/fn.summarize_counter.ensures.1.bounded.smt2"
test -f "$branch_smt"
test -f "$loop_smt"
grep "(ite flag" "$branch_smt" >/dev/null
grep "i_loop_exit_" "$loop_smt" >/dev/null
grep "(assert (not (< i_loop_exit_" "$loop_smt" >/dev/null

unsafe_source="$work_dir/partial-branch.sigil"
cat >"$unsafe_source" <<'EOF'
module partial_branch;

fn choose(flag: bool, left: i64, right: i64) -> i64
requires left_nonnegative: left >= 0;
ensures nonnegative: result >= 0;
{
  let selected: i64 = 0;
  if flag {
    selected = left;
  } else {
    selected = right;
  }
  return selected;
}
EOF

unsafe_output="$("$sigil_bin" check "$unsafe_source" --no-z3)"
printf '%s\n' "$unsafe_output"
printf '%s\n' "$unsafe_output" | grep "\[UNKNOWN\] fn.choose.ensures.1.nonnegative" >/dev/null

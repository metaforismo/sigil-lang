#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

valid_candidate="$work_dir/candidate_valid.sigil"
invalid_candidate="$work_dir/candidate_invalid.sigil"
runtime_candidate="$work_dir/candidate_runtime.sigil"
valid_smt_dir="$work_dir/valid-smt"
invalid_smt_dir="$work_dir/invalid-smt"

cat >"$valid_candidate" <<'SIGIL'
module candidate_valid;

theorem truth for ()
ensures always: true;
{
  return true;
}
SIGIL

output="$("$sigil_bin" agent-check "$valid_candidate" --strict --no-z3 --save-smt "$valid_smt_dir" --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "agent-candidate candidate_valid" >/dev/null
printf '%s\n' "$output" | grep "  theorems: 1" >/dev/null
printf '%s\n' "$output" | grep "  functions: 0" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 2" >/dev/null
printf '%s\n' "$output" | grep "  status: accepted" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] theorem.truth.ensures.1.always" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] theorem.truth.ensures.2.holds" >/dev/null

test -f "$valid_smt_dir/theorem.truth.ensures.1.always.smt2"
test -f "$valid_smt_dir/theorem.truth.ensures.2.holds.smt2"
grep "(set-option :timeout 250)" "$valid_smt_dir/theorem.truth.ensures.1.always.smt2" >/dev/null

cat >"$invalid_candidate" <<'SIGIL'
module candidate_invalid;

theorem placeholder for ()
ensures impossible: false;
{
  return false;
}
SIGIL

set +e
bad_output="$("$sigil_bin" agent-check "$invalid_candidate" --strict --no-z3 --save-smt "$invalid_smt_dir" 2>&1)"
bad_status=$?
set -e
printf '%s\n' "$bad_output"

test "$bad_status" -eq 2
printf '%s\n' "$bad_output" | grep "agent-candidate candidate_invalid" >/dev/null
printf '%s\n' "$bad_output" | grep "  status: rejected" >/dev/null
printf '%s\n' "$bad_output" | grep "\[UNKNOWN\] theorem.placeholder.ensures.1.impossible" >/dev/null
printf '%s\n' "$bad_output" | grep "\[UNKNOWN\] theorem.placeholder.ensures.2.holds" >/dev/null
test -f "$invalid_smt_dir/theorem.placeholder.ensures.1.impossible.smt2"
test -f "$invalid_smt_dir/theorem.placeholder.ensures.2.holds.smt2"

cat >"$runtime_candidate" <<'SIGIL'
module candidate_runtime;

fn runtime_value() -> i64
{
  return 1;
}
SIGIL

set +e
runtime_output="$("$sigil_bin" agent-check "$runtime_candidate" --strict --no-z3 2>&1)"
runtime_status=$?
set -e
printf '%s\n' "$runtime_output"

test "$runtime_status" -eq 2
printf '%s\n' "$runtime_output" | grep "agent-candidate candidate_runtime" >/dev/null
printf '%s\n' "$runtime_output" | grep "  functions: 1" >/dev/null
printf '%s\n' "$runtime_output" | grep "  status: rejected" >/dev/null
printf '%s\n' "$runtime_output" | grep "runtime functions are outside the proof-only candidate surface" >/dev/null

#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

source_file="$work_dir/source.sigil"
cat >"$source_file" <<'SIGIL'
module refinement_target;

fn preserve(x: i64) -> i64
ensures exact: result == x;
{
  assert baseline: x == x;
  return x + 1;
}
SIGIL

proposer="$work_dir/proposer.sh"
cat >"$proposer" <<'SH'
#!/usr/bin/env sh
set -eu
source_file="$1"
request_file="$2"
candidate_file="$3"
attempt="$4"
test -f "$request_file"
if test "$attempt" -eq 1; then
  sed 's/return x + 1;/return x + 1;/' "$source_file" >"$candidate_file"
else
  cat >"$candidate_file" <<'SIGIL'
module refinement_target;

theorem identity_holds for (x: i64)
ensures reflexive: x == x;
{
  return true;
}

fn preserve(x: i64) -> i64
ensures exact: result == x;
{
  assert lemma_call: identity_holds(x);
  assert baseline: x == x;
  return x;
}
SIGIL
fi
SH
chmod +x "$proposer"

trace_dir="$work_dir/success"
output="$($sigil_bin agent-refine "$source_file" --agent-command "$proposer" \
  --max-attempts 2 --agent-timeout-ms 10000 --solver-timeout-ms 250 --no-z3 \
  --save-trace "$trace_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "agent-refinement refinement_target" >/dev/null
printf '%s\n' "$output" | grep "  status: accepted" >/dev/null
printf '%s\n' "$output" | grep "  attempts-used: 2" >/dev/null
test -f "$trace_dir/agent-refinement.trace.txt"
test -f "$trace_dir/attempt-1.request.txt"
test -f "$trace_dir/attempt-1.candidate.sigil"
test -f "$trace_dir/attempt-2.candidate.sigil"
test -f "$trace_dir/initial.proof.txt"
test -f "$trace_dir/attempt-1.proof.txt"
test -f "$trace_dir/attempt-2.proof.txt"
test -f "$trace_dir/attempt-2-smt/fn.preserve.ensures.1.exact.smt2"
grep "sigil-agent-refinement-trace-v1" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "source-path $source_file" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "agent-command $proposer" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "max-attempts 2" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt-input 2 $trace_dir/attempt-1.candidate.sigil" \
  "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt-request 2 attempt-2.request.txt" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt-candidate 2 attempt-2.candidate.sigil" \
  "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt-log 2 attempt-2.agent.log" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt 1 rejected proof-failure" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt 2 accepted" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "attempt-proof 2 attempt-2.proof.txt" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "final-status accepted" "$trace_dir/agent-refinement.trace.txt" >/dev/null
grep "status PROVEN" "$trace_dir/attempt-2.proof.txt" >/dev/null
grep "theorem.identity_holds.ensures" "$trace_dir/attempt-2.proof.txt" >/dev/null
grep "fn.preserve.assert.1.lemma_call" "$trace_dir/attempt-2.proof.txt" >/dev/null
grep "theorem identity_holds" "$trace_dir/attempt-2.candidate.sigil" >/dev/null
test ! -e "$trace_dir/agent-refinement.trace.txt.tmp"

proven_source="$work_dir/proven.sigil"
cat >"$proven_source" <<'SIGIL'
module already_proven;

fn identity(x: i64) -> i64
ensures exact: result == x;
{
  return x;
}
SIGIL

must_not_run="$work_dir/must-not-run.sh"
cat >"$must_not_run" <<'SH'
#!/usr/bin/env sh
touch "$3.invoked"
exit 99
SH
chmod +x "$must_not_run"

proven_trace="$work_dir/already-proven"
proven_output="$($sigil_bin agent-refine "$proven_source" --agent-command "$must_not_run" \
  --max-attempts 2 --agent-timeout-ms 10000 --no-z3 --save-trace "$proven_trace")"
printf '%s\n' "$proven_output"
printf '%s\n' "$proven_output" | grep "  status: accepted" >/dev/null
printf '%s\n' "$proven_output" | grep "  attempts-used: 0" >/dev/null
grep "final-status accepted" "$proven_trace/agent-refinement.trace.txt" >/dev/null
test ! -e "$proven_trace/attempt-1.candidate.sigil.invoked"

weakener="$work_dir/weakener.sh"
cat >"$weakener" <<'SH'
#!/usr/bin/env sh
set -eu
sed 's/ensures exact: result == x;/ensures exact: true;/' "$1" >"$3"
SH
chmod +x "$weakener"

set +e
weak_output="$($sigil_bin agent-refine "$source_file" --agent-command "$weakener" \
  --max-attempts 1 --agent-timeout-ms 10000 --no-z3 --save-trace "$work_dir/weaken" 2>&1)"
weak_status=$?
set -e
printf '%s\n' "$weak_output"
test "$weak_status" -eq 2
grep "attempt 1 rejected contract-changed" "$work_dir/weaken/agent-refinement.trace.txt" >/dev/null
grep "final-status exhausted" "$work_dir/weaken/agent-refinement.trace.txt" >/dev/null

assumer="$work_dir/assumer.sh"
cat >"$assumer" <<'SH'
#!/usr/bin/env sh
set -eu
sed 's/return x + 1;/assume shortcut: false;\n  return x + 1;/' "$1" >"$3"
SH
chmod +x "$assumer"

set +e
assume_output="$($sigil_bin agent-refine "$source_file" --agent-command "$assumer" \
  --max-attempts 1 --agent-timeout-ms 10000 --no-z3 --save-trace "$work_dir/assume" 2>&1)"
assume_status=$?
set -e
printf '%s\n' "$assume_output"
test "$assume_status" -eq 2
grep "attempt 1 rejected contract-changed" "$work_dir/assume/agent-refinement.trace.txt" >/dev/null

stripper="$work_dir/stripper.sh"
cat >"$stripper" <<'SH'
#!/usr/bin/env sh
set -eu
sed '/assert baseline:/d; s/return x + 1;/return x;/' "$1" >"$3"
SH
chmod +x "$stripper"

set +e
strip_output="$($sigil_bin agent-refine "$source_file" --agent-command "$stripper" \
  --max-attempts 1 --agent-timeout-ms 10000 --no-z3 --save-trace "$work_dir/strip" 2>&1)"
strip_status=$?
set -e
printf '%s\n' "$strip_output"
test "$strip_status" -eq 2
grep "attempt 1 rejected contract-changed" "$work_dir/strip/agent-refinement.trace.txt" >/dev/null

invalid="$work_dir/invalid.sh"
cat >"$invalid" <<'SH'
#!/usr/bin/env sh
printf 'this is not a Sigil module\n' >"$3"
SH
chmod +x "$invalid"

set +e
invalid_output="$($sigil_bin agent-refine "$source_file" --agent-command "$invalid" \
  --max-attempts 1 --agent-timeout-ms 10000 --no-z3 --save-trace "$work_dir/invalid" 2>&1)"
invalid_status=$?
set -e
printf '%s\n' "$invalid_output"
test "$invalid_status" -eq 2
grep "attempt 1 rejected invalid-candidate" \
  "$work_dir/invalid/agent-refinement.trace.txt" >/dev/null

nonwriter="$work_dir/nonwriter.sh"
cat >"$nonwriter" <<'SH'
#!/usr/bin/env sh
(sleep 1; touch "$3.survived") &
exit 0
SH
chmod +x "$nonwriter"

stale_trace="$work_dir/stale"
mkdir -p "$stale_trace"
sed 's/return x + 1;/return x;/' "$source_file" >"$stale_trace/attempt-1.candidate.sigil"
set +e
stale_output="$($sigil_bin agent-refine "$source_file" --agent-command "$nonwriter" \
  --max-attempts 1 --agent-timeout-ms 10000 --no-z3 --save-trace "$stale_trace" 2>&1)"
stale_status=$?
set -e
printf '%s\n' "$stale_output"
test "$stale_status" -eq 2
grep "attempt 1 rejected agent-exit-0" "$stale_trace/agent-refinement.trace.txt" >/dev/null
test ! -e "$stale_trace/attempt-1.candidate.sigil"
sleep 1
test ! -e "$stale_trace/attempt-1.candidate.sigil.survived"

sleeper="$work_dir/sleeper.sh"
cat >"$sleeper" <<'SH'
#!/usr/bin/env sh
(sleep 1; touch "$3.survived") &
wait
SH
chmod +x "$sleeper"

set +e
timeout_marker="$work_dir/timeout/attempt-1.candidate.sigil.survived"
timeout_output="$($sigil_bin agent-refine "$source_file" --agent-command "$sleeper" \
  --max-attempts 1 --agent-timeout-ms 50 --no-z3 --save-trace "$work_dir/timeout" 2>&1)"
timeout_status=$?
set -e
printf '%s\n' "$timeout_output"
test "$timeout_status" -eq 2
grep "attempt 1 timed-out" "$work_dir/timeout/agent-refinement.trace.txt" >/dev/null
grep "final-status exhausted" "$work_dir/timeout/agent-refinement.trace.txt" >/dev/null
sleep 1
test ! -e "$timeout_marker"

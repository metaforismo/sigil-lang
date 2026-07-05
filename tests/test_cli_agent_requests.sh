#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

source_file="$work_dir/hints.sigil"
agent_dir="$work_dir/agent"

cat >"$source_file" <<'SIGIL'
module hints;

fn add_one(x: i64) -> i64
ensures advanced: result > x;
{
  return x + 1;
}
SIGIL

output="$("$sigil_bin" check "$source_file" --no-z3 --save-agent-requests "$agent_dir")"
printf '%s\n' "$output"

request="$agent_dir/fn.add_one.ensures.1.advanced.agent-request.txt"
candidate="$agent_dir/fn.add_one.ensures.1.advanced.candidate.sigil"

test -f "$request"
test -f "$candidate"

printf '%s\n' "$output" | grep "agent-request: $request" >/dev/null
printf '%s\n' "$output" | grep "theorem-candidate: $candidate" >/dev/null
grep "sigil-agent-request-v1" "$request" >/dev/null
grep "candidate-file: fn.add_one.ensures.1.advanced.candidate.sigil" "$request" >/dev/null
grep "acceptance-gate:" "$request" >/dev/null
grep "sigil-theorem-candidate-v1" "$candidate" >/dev/null
grep "module candidate_fn_add_one_ensures_1_advanced;" "$candidate" >/dev/null
grep "return false;" "$candidate" >/dev/null

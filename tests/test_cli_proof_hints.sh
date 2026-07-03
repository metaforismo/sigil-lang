#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

source_file="$work_dir/hints.sigil"
hint_dir="$work_dir/hints"
smt_dir="$work_dir/smt"

cat >"$source_file" <<'SIGIL'
module hints;

fn add_one(x: i64) -> i64
ensures advanced: result > x;
{
  return x + 1;
}
SIGIL

output="$("$sigil_bin" check "$source_file" --no-z3 --save-smt "$smt_dir" --save-proof-hints "$hint_dir")"
printf '%s\n' "$output"

expected="$hint_dir/fn.add_one.ensures.1.advanced.proof-hint.txt"
test -f "$expected"
printf '%s\n' "$output" | grep "proof-hint: $expected" >/dev/null
grep "sigil-proof-hint-v1" "$expected" >/dev/null
grep "status: UNKNOWN" "$expected" >/dev/null
grep "goal:" "$expected" >/dev/null
grep "advanced: (result > x)" "$expected" >/dev/null
grep "agent-contract:" "$expected" >/dev/null
grep "smt-path: $smt_dir/fn.add_one.ensures.1.advanced.smt2" "$expected" >/dev/null
grep "(check-sat)" "$expected" >/dev/null

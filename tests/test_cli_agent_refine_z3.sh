#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"
rm -rf "$work_dir"
mkdir -p "$work_dir"

source_file="$work_dir/source.sigil"
cat >"$source_file" <<'SIGIL'
module z3_refinement;

fn advance(x: i64) -> i64
ensures advanced: result > x;
{
  return x;
}
SIGIL

proposer="$work_dir/proposer.sh"
cat >"$proposer" <<'SH'
#!/usr/bin/env sh
set -eu
sed 's/return x;/return x + 1;/' "$1" >"$3"
SH
chmod +x "$proposer"

output="$($sigil_bin agent-refine "$source_file" --agent-command "$proposer" \
  --max-attempts 1 --agent-timeout-ms 10000 --solver-timeout-ms 250 \
  --save-trace "$work_dir/trace")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  status: accepted" >/dev/null
printf '%s\n' "$output" | grep "  attempts-used: 1" >/dev/null
grep "status REFUTED" "$work_dir/trace/initial.proof.txt" >/dev/null
grep "status PROVEN" "$work_dir/trace/attempt-1.proof.txt" >/dev/null
grep "attempt 1 accepted" "$work_dir/trace/agent-refinement.trace.txt" >/dev/null
grep "(set-option :timeout 250)" \
  "$work_dir/trace/attempt-1-smt/fn.advance.ensures.1.advanced.smt2" >/dev/null

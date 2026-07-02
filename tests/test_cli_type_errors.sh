#!/bin/sh
set -eu

sigil="$1"
outdir="$2"

rm -rf "$outdir"
mkdir -p "$outdir"

source_file="$outdir/missing-return.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn missing_return(x: i64) -> i64
{
  let y: i64 = x + 1;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "function 'missing_return' must return a value on every path" >/dev/null
printf '%s\n' "$output" | grep "missing-return.sigil:3:1-6:1" >/dev/null

source_file="$outdir/unreachable-after-return.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn unreachable_after_return(x: i64) -> i64
{
  return x;
  assert never: true;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "unreachable statement after guaranteed return" >/dev/null
printf '%s\n' "$output" | grep "unreachable-after-return.sigil:6:3-21" >/dev/null

source_file="$outdir/reserved-result-parameter.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn reserved_result_parameter(result: i64) -> i64
{
  return result;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "parameter 'reserved_result_parameter.result' cannot use reserved name 'result'" >/dev/null
printf '%s\n' "$output" | grep "reserved-result-parameter.sigil:3:30-35" >/dev/null

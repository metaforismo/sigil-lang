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

source_file="$outdir/nonvoid-empty-return.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn nonvoid_empty_return(x: i64) -> i64
{
  return;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "non-void functions must return a value" >/dev/null
printf '%s\n' "$output" | grep "nonvoid-empty-return.sigil:5:3-9" >/dev/null

source_file="$outdir/duplicate-proof-label.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn duplicate_proof_label(x: i64) -> i64
{
  assert repeated: x == x;
  assume repeated: true;
  return x;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "duplicate proof label 'repeated'" >/dev/null
printf '%s\n' "$output" | grep "duplicate-proof-label.sigil:6:3-24" >/dev/null

source_file="$outdir/duplicate-top-level.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

struct Thing {
  value: i64;
}

fn Thing(x: i64) -> i64
{
  return x;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "duplicate top-level declaration 'Thing'" >/dev/null
printf '%s\n' "$output" | grep "duplicate-top-level.sigil:7:1-10:1" >/dev/null

source_file="$outdir/integer-overflow.sigil"
cat >"$source_file" <<'SIGIL'
module bad;

fn integer_overflow() -> i64
{
  return 9223372036854775808;
}
SIGIL

set +e
output="$("$sigil" check "$source_file" --no-z3 2>&1)"
status="$?"
set -e

echo "$output"

test "$status" -eq 1
printf '%s\n' "$output" | grep "integer literal is out of range for i64" >/dev/null
printf '%s\n' "$output" | grep "integer-overflow.sigil:5:10-28" >/dev/null

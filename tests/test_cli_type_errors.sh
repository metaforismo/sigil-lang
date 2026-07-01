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

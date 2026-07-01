#!/bin/sh
set -eu

sigil="$1"
workdir="$2"

mkdir -p "$workdir"
source_file="$workdir/unsupported-native.sigil"
cat >"$source_file" <<'SIGIL'
module native;

fn quotient(x: i64, y: i64) -> i64
{
  return x / y;
}
SIGIL

set +e
output="$("$sigil" compile "$source_file" 2>&1)"
status="$?"
set -e

echo "$output"
test "$status" -eq 2
printf '%s\n' "$output" | grep "skipped: quotient" >/dev/null
printf '%s\n' "$output" | grep "division and modulo are not native-lowered" >/dev/null
printf '%s\n' "$output" | grep "at: $source_file:5:10-14" >/dev/null

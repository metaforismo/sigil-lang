#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

set +e
output="$("$sigil_bin" check "$example_file" --strict --show-model 2>&1)"
status="$?"
set -e

printf '%s\n' "$output"
test "$status" -eq 2
printf '%s\n' "$output" | grep "\\[REFUTED\\]" >/dev/null
printf '%s\n' "$output" | grep "at: .*examples/refuted.sigil:5:1-36" >/dev/null
printf '%s\n' "$output" | grep "counterexample:" >/dev/null
printf '%s\n' "$output" | grep "x: i64 = 0" >/dev/null
printf '%s\n' "$output" | grep "result: i64 = 0" >/dev/null
printf '%s\n' "$output" | grep "model:" >/dev/null

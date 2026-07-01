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
printf '%s\n' "$output" | grep "model:" >/dev/null

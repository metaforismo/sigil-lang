#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
output_dir="$3"

rm -rf "$output_dir"
"$sigil_bin" check "$example_file" --no-z3 --solver-timeout-ms 250 --save-smt "$output_dir" >/dev/null

expected="$output_dir/fn.preserve_capacity.ensures.1.still_bounded.smt2"
test -f "$expected"
grep "(set-option :timeout 250)" "$expected" >/dev/null
grep "(check-sat)" "$expected" >/dev/null

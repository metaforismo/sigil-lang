#!/bin/sh
set -eu

sigil="$1"
example="$2"

run_and_expect() {
  function_name="$1"
  expected="$2"
  shift 2

  output="$("$sigil" run "$example" "$function_name" "$@")"
  echo "$output"
  printf '%s\n' "$output" | grep "status: invoked" >/dev/null
  printf '%s\n' "$output" | grep "result: $expected" >/dev/null
}

run_and_expect add_one 42 41
run_and_expect choose 9 true 9
run_and_expect choose 0 false 9
run_and_expect abs_value 7 -7
run_and_expect nonzero false 0
run_and_expect nonzero true 3

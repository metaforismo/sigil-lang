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

run_and_expect_failure() {
  function_name="$1"
  expected="$2"
  shift 2

  set +e
  output="$("$sigil" run "$example" "$function_name" "$@" 2>&1)"
  status="$?"
  set -e

  echo "$output"
  test "$status" -eq 1
  printf '%s\n' "$output" | grep "$expected" >/dev/null
}

run_and_expect add_one 42 41
run_and_expect choose 9 true 9
run_and_expect choose 0 false 9
run_and_expect abs_value 7 -7
run_and_expect nonzero false 0
run_and_expect nonzero true 3
run_and_expect sum3 6 1 2 3
run_and_expect choose3 10 true false 10
run_and_expect choose3 11 false true 10
run_and_expect choose3 0 false false 10
run_and_expect_failure add_one "argument 1 must be an i64" 9223372036854775808

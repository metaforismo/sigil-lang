#!/bin/sh
set -eu

sigil="$1"
expected="$2"

set +e
output="$("$sigil" backend 2>&1)"
status="$?"
set -e

echo "$output"

case "$expected" in
  available)
    test "$status" -eq 0
    printf '%s\n' "$output" | grep "^available:" >/dev/null
    printf '%s\n' "$output" | grep "compiled-with-libgccjit: yes" >/dev/null
    printf '%s\n' "$output" | grep "jit-context: available" >/dev/null
    printf '%s\n' "$output" | grep "native-lowering: available" >/dev/null
    printf '%s\n' "$output" | grep "abi-invocation: available" >/dev/null
    printf '%s\n' "$output" | grep "debug-info: enabled" >/dev/null
    ;;
  unavailable)
    test "$status" -eq 3
    printf '%s\n' "$output" | grep "^unavailable:" >/dev/null
    printf '%s\n' "$output" | grep "compiled-with-libgccjit: no" >/dev/null
    printf '%s\n' "$output" | grep "jit-context: unavailable" >/dev/null
    printf '%s\n' "$output" | grep "native-lowering: unavailable" >/dev/null
    printf '%s\n' "$output" | grep "abi-invocation: unavailable" >/dev/null
    printf '%s\n' "$output" | grep "debug-info: disabled" >/dev/null
    ;;
  *)
    echo "unknown expected backend status: $expected" >&2
    exit 1
    ;;
esac

printf '%s\n' "$output" | grep "native-ir-artifacts: available" >/dev/null

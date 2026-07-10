#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 5" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.expose_write_permission.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.2.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.safety.3.memory_write" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_preserves_write_permission.ensures.1.exact" >/dev/null

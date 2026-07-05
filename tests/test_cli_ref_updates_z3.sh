#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 11" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.safety.1.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.safety.2.memory_write" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.assert.1.still_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.assert.2.still_writable" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.assert.3.same_address" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_load.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_flag_ref.ensures.1.exact" >/dev/null

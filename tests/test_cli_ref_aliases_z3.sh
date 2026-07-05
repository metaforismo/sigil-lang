#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 6" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.same_ref_loads_match.safety.1.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.same_ref_loads_match.safety.2.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.same_ref_loads_match.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.same_bool_ref_loads_match.ensures.1.exact" >/dev/null

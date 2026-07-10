#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 5" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 18" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.expose_liveness.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_live_slice.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_live_ref.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.model_store_preserves_liveness.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.ref_store_preserves_liveness.ensures.1.preserved" >/dev/null

#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 37" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_slice.safety.6.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_slice.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_ref.safety.6.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_ref.assert.1.epoch_advanced" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_ref.ensures.1.exact" >/dev/null

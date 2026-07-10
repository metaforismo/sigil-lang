#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 17" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.safety.2.ownership_present" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.safety.3.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.safety.4.index_in_bounds" >/dev/null
printf '%s\n' "$output" | grep \
  "\[PROVEN\] fn.write_then_read.safety.7.memory_initialized" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.assert.1.length_preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.write_then_read.ensures.1.exact" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.update_flag.ensures.1.exact" >/dev/null

#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  containers: 1" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_window.container.window.invariant.1.index_non_negative" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_window.container.window.invariant.2.index_within_items" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_window.assert.1.len_visible" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_window.assert.2.index_visible" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.read_window.safety.1.memory_live" >/dev/null

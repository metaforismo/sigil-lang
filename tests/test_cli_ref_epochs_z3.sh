#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 7" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.entry_epochs_match.ensures.1.same_entry_epoch" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.1.memory_live" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.2.ownership_present" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.3.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.4.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.safety.5.memory_write" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.store_advances_epoch.ensures.1.next_epoch" >/dev/null

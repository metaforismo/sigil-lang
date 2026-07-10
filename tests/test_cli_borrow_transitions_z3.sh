#!/usr/bin/env sh
set -eu
sigil_bin="$1"
example_file="$2"
output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"
printf '%s\n' "$output" | grep "  proof obligations: 19" >/dev/null
if printf '%s\n' "$output" | grep "\[UNKNOWN\]" >/dev/null; then exit 1; fi
printf '%s\n' "$output" | grep "\[PROVEN\] fn.shared_round_trip.ensures.1.restored" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.mutable_round_trip.ensures.1.released" >/dev/null

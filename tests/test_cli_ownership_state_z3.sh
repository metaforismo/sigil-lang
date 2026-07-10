#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"

output="$("$sigil_bin" check "$example_file" --strict --solver-timeout-ms 250)"
printf '%s\n' "$output"
printf '%s\n' "$output" | grep "  proof obligations: 11" >/dev/null
if printf '%s\n' "$output" | grep "\[UNKNOWN\]" >/dev/null; then
  exit 1
fi
printf '%s\n' "$output" | grep "\[PROVEN\] fn.owner_has_identity.ensures.1.nonzero" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.alias_preserves_owner.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.model_store_preserves_borrows.ensures.1.preserved" >/dev/null
printf '%s\n' "$output" | grep "\[PROVEN\] fn.ref_store_preserves_borrows.ensures.1.preserved" >/dev/null

#!/bin/sh
set -eu

sigil="$1"
example="$2"
outdir="$3"

rm -rf "$outdir"
output="$("$sigil" compile "$example" --dump-native-ir --save-native-ir "$outdir")"
echo "$output"

artifact="$outdir/fn.add_one.native-ir.txt"
test -f "$artifact"

printf '%s\n' "$output" | grep "native-ir: $artifact" >/dev/null
printf '%s\n' "$output" | grep -- "--- fn.add_one.native-ir.txt" >/dev/null
grep "sigil-native-ir v0" "$artifact" >/dev/null
grep "signature add_one(x: i64) -> i64" "$artifact" >/dev/null
grep "debug-info enabled" "$artifact" >/dev/null
grep "debug-locations" "$artifact" >/dev/null
grep "statement.assign.y" "$artifact" >/dev/null
grep "expr.assign.y.value.rhs" "$artifact" >/dev/null
grep "requires" "$artifact" >/dev/null
grep "ensures" "$artifact" >/dev/null
grep "assign y" "$artifact" >/dev/null
grep "return @" "$artifact" >/dev/null

void_artifact="$outdir/fn.observe.native-ir.txt"
test -f "$void_artifact"
grep "status lowered" "$void_artifact" >/dev/null
grep "signature observe(flag: bool, x: i64) -> void" "$void_artifact" >/dev/null
grep "return @" "$void_artifact" >/dev/null
grep "value (none)" "$void_artifact" >/dev/null

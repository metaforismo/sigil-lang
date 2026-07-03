#!/bin/sh
set -eu

sigil="$1"
example="$2"
outdir="$3"

rm -rf "$outdir"
native_dir="$outdir/native"
binary_dir="$outdir/binary"
output="$("$sigil" compile "$example" --dump-native-ir --save-native-ir "$native_dir" \
  --dump-binary-facts --save-binary-facts "$binary_dir")"
echo "$output"

artifact="$native_dir/fn.add_one.native-ir.txt"
binary_artifact="$binary_dir/fn.add_one.binary-facts.txt"
test -f "$artifact"
test -f "$binary_artifact"

printf '%s\n' "$output" | grep "native-ir: $artifact" >/dev/null
printf '%s\n' "$output" | grep "binary-facts: $binary_artifact" >/dev/null
printf '%s\n' "$output" | grep -- "--- fn.add_one.native-ir.txt" >/dev/null
printf '%s\n' "$output" | grep -- "--- fn.add_one.binary-facts.txt" >/dev/null
grep "sigil-native-ir v0" "$artifact" >/dev/null
grep "sigil-binary-proof-facts v0" "$binary_artifact" >/dev/null
grep "signature add_one(x: i64) -> i64" "$artifact" >/dev/null
grep "native-status lowered" "$binary_artifact" >/dev/null
grep "native-ir-file fn.add_one.native-ir.txt" "$binary_artifact" >/dev/null
grep "cycle-bound-proven no" "$binary_artifact" >/dev/null
grep "crash-safety-proven no" "$binary_artifact" >/dev/null
grep "experiment-contract" "$binary_artifact" >/dev/null
grep "debug-info enabled" "$artifact" >/dev/null
grep "debug-locations" "$artifact" >/dev/null
grep "statement.assign.y" "$artifact" >/dev/null
grep "expr.assign.y.value.rhs" "$artifact" >/dev/null
grep "requires" "$artifact" >/dev/null
grep "ensures" "$artifact" >/dev/null
grep "assign y" "$artifact" >/dev/null
grep "return @" "$artifact" >/dev/null

void_artifact="$native_dir/fn.observe.native-ir.txt"
void_binary_artifact="$binary_dir/fn.observe.binary-facts.txt"
test -f "$void_artifact"
test -f "$void_binary_artifact"
grep "status lowered" "$void_artifact" >/dev/null
grep "native-status lowered" "$void_binary_artifact" >/dev/null
grep "signature observe(flag: bool, x: i64) -> void" "$void_artifact" >/dev/null
grep "return @" "$void_artifact" >/dev/null
grep "value (none)" "$void_artifact" >/dev/null

#!/usr/bin/env sh
set -eu

sigil="$1"
example="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

native_dir="$work_dir/native"
binary_dir="$work_dir/binary"
"$sigil" compile "$example" --save-native-ir "$native_dir" --save-binary-facts "$binary_dir"

native="$native_dir/fn.update_ref.native-ir.txt"
binary="$binary_dir/fn.update_ref.binary-facts.txt"
test -f "$native"
test -f "$binary"

grep "source-proof-status not-run-by-compile" "$native" >/dev/null
grep "source-proof-obligation-count 8" "$native" >/dev/null
grep "source-memory-obligation-count 7" "$native" >/dev/null
grep "fn.update_ref.safety.1.memory_live" "$native" >/dev/null
grep "fn.update_ref.safety.3.mutable_borrow_active" "$native" >/dev/null
grep "fn.update_ref.safety.5.memory_write" "$native" >/dev/null
grep "fn.update_ref.safety.7.memory_valid" "$native" >/dev/null

grep "source-proof-status not-run-by-compile" "$binary" >/dev/null
grep "source-proof-obligation-count 8" "$binary" >/dev/null
grep "source-memory-obligation-count 7" "$binary" >/dev/null
grep "source-memory-facts-linked yes" "$binary" >/dev/null
grep "goal memory_write: ptr.write" "$binary" >/dev/null
grep "source-proof-proven no" "$binary" >/dev/null
grep "crash-safety-proven no" "$binary" >/dev/null

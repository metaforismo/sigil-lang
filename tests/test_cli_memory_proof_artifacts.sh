#!/usr/bin/env sh
set -eu

sigil="$1"
example="$2"
initialization_example="$3"
work_dir="$4"

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

initialization_native_dir="$work_dir/initialization-native"
initialization_binary_dir="$work_dir/initialization-binary"
set +e
"$sigil" compile "$initialization_example" \
  --save-native-ir "$initialization_native_dir" \
  --save-binary-facts "$initialization_binary_dir"
initialization_status=$?
set -e
test "$initialization_status" -eq 2

initialization_native="$initialization_native_dir/fn.initialize_slot.native-ir.txt"
initialization_binary="$initialization_binary_dir/fn.initialize_slot.binary-facts.txt"
test -f "$initialization_native"
test -f "$initialization_binary"

grep "status skipped" "$initialization_native" >/dev/null
grep "source-memory-obligation-count 10" "$initialization_native" >/dev/null
grep "memory-obligation fn.initialize_slot.safety.11.memory_initialized" \
  "$initialization_native" >/dev/null
grep -F "goal memory_initialized: select(initialized.init, (initialized.offset + index))" \
  "$initialization_native" >/dev/null

grep "native-status skipped" "$initialization_binary" >/dev/null
grep "source-memory-obligation-count 10" "$initialization_binary" >/dev/null
grep "memory-obligation fn.initialize_slot.safety.11.memory_initialized" \
  "$initialization_binary" >/dev/null
grep -F "goal memory_initialized: select(initialized.init, (initialized.offset + index))" \
  "$initialization_binary" >/dev/null
grep "crash-safety-proven no" "$initialization_binary" >/dev/null

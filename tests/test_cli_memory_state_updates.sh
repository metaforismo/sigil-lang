#!/usr/bin/env sh
set -eu

sigil_bin="$1"
example_file="$2"
work_dir="$3"

rm -rf "$work_dir"
mkdir -p "$work_dir"

smt_dir="$work_dir/smt"
output="$("$sigil_bin" check "$example_file" --no-z3 --solver-timeout-ms 250 --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  functions: 2" >/dev/null
printf '%s\n' "$output" | grep "  proof obligations: 37" >/dev/null
printf '%s\n' "$output" | grep "fn.update_slice.safety.4.memory_live" >/dev/null
printf '%s\n' "$output" | grep "fn.update_slice.safety.5.ownership_present" >/dev/null
printf '%s\n' "$output" | grep "fn.update_slice.safety.6.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "fn.update_slice.safety.7.index_in_bounds" >/dev/null
printf '%s\n' "$output" | grep "fn.update_slice.safety.10.memory_initialized" >/dev/null
printf '%s\n' "$output" | grep "fn.update_ref.safety.6.mutable_borrow_active" >/dev/null
printf '%s\n' "$output" | grep "fn.update_ref.safety.7.memory_valid" >/dev/null
printf '%s\n' "$output" | grep "fn.update_ref.safety.8.memory_write" >/dev/null

slice_store="$smt_dir/fn.update_slice.assert.1.write_visible.smt2"
ref_store="$smt_dir/fn.update_ref.assert.1.epoch_advanced.smt2"
test -f "$slice_store"
test -f "$ref_store"
grep "(assert (= updated_mut_borrow borrowed_mut_borrow))" "$slice_store" >/dev/null
grep "(assert (= updated_offset borrowed_offset))" "$slice_store" >/dev/null
grep "(assert (= updated_data (store borrowed_data (+ borrowed_offset index) value)))" \
  "$slice_store" >/dev/null
grep "(assert (= updated_init (store borrowed_init (+ borrowed_offset index) true)))" \
  "$slice_store" >/dev/null
grep "(assert (= updated_mut_borrow borrowed_mut_borrow))" "$ref_store" >/dev/null
grep "(assert (= updated_epoch (+ borrowed_epoch 1)))" "$ref_store" >/dev/null

unsafe_source="$work_dir/unborrowed-write.sigil"
unsafe_smt="$work_dir/unsafe-smt"
cat >"$unsafe_source" <<'EOF'
module unborrowed_write;

fn write_without_borrow(ptr: Ref[i64], value: i64) -> i64
requires live: is_live(ptr);
requires owned: has_owner(ptr);
requires valid: is_valid(ptr);
requires writable: can_write(ptr);
{
  let updated: Ref[i64] = store(ptr, value);
  return load(updated);
}
EOF

set +e
unsafe_output="$("$sigil_bin" check "$unsafe_source" --strict --no-z3 --save-smt "$unsafe_smt" 2>&1)"
unsafe_status=$?
set -e
printf '%s\n' "$unsafe_output"

test "$unsafe_status" -eq 2
printf '%s\n' "$unsafe_output" | grep "\[UNKNOWN\] fn.write_without_borrow.safety.3.mutable_borrow_active" >/dev/null
printf '%s\n' "$unsafe_output" | grep "\[PROVEN\] fn.write_without_borrow.safety.5.memory_write" >/dev/null

#!/usr/bin/env sh
set -eu

sigil_bin="$1"
work_dir="$2"

rm -rf "$work_dir"
mkdir -p "$work_dir"

source_file="$work_dir/theorems.sigil"
smt_dir="$work_dir/smt"

cat >"$source_file" <<'SIGIL'
module lemmas;

theorem add_one_gt for (x: i64)
requires non_negative: x >= 0;
ensures advanced: x + 1 > x;
{
  return x + 1 > x;
}

fn use_add_one(x: i64) -> i64
requires non_negative: x >= 0;
ensures advanced: result > x;
{
  assert lemma_call: add_one_gt(x);
  let y: i64 = x + 1;
  assert from_lemma: y > x;
  return y;
}
SIGIL

output="$("$sigil_bin" check "$source_file" --no-z3 --save-smt "$smt_dir")"
printf '%s\n' "$output"

printf '%s\n' "$output" | grep "  theorems: 1" >/dev/null
printf '%s\n' "$output" | grep "theorem.add_one_gt.ensures.1.advanced" >/dev/null
printf '%s\n' "$output" | grep "theorem.add_one_gt.ensures.2.holds" >/dev/null
printf '%s\n' "$output" | grep "fn.use_add_one.assert.2.from_lemma" >/dev/null

test -f "$smt_dir/theorem.add_one_gt.ensures.1.advanced.smt2"
test -f "$smt_dir/theorem.add_one_gt.ensures.2.holds.smt2"
test -f "$smt_dir/fn.use_add_one.assert.2.from_lemma.smt2"

grep "(assert (> (+ x 1) x))" "$smt_dir/fn.use_add_one.assert.2.from_lemma.smt2" >/dev/null
grep "(assert (= add_one_gt_call_1_" "$smt_dir/fn.use_add_one.assert.2.from_lemma.smt2" >/dev/null

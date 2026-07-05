#include "sigil/parser.hpp"
#include "sigil/proof.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main() {
  const char* source = R"(
module arithmetic;

fn proof_seed(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = x + 1;
  assert y_defined: y == x + 1;
  assert still_non_negative: x >= 0;
  return y;
}
)";

  const auto module = sigil::parse_source(source, "proof.sigil");
  const auto obligations = sigil::build_obligations(module);
  expect(obligations.size() == 3, "two asserts plus ensure obligations");
  expect(obligations[0].name == "fn.proof_seed.assert.1.y_defined", "let assert obligation name");
  expect(obligations[0].location.line == 9, "assert obligation line");
  expect(obligations[0].location.column == 3, "assert obligation column");
  expect(obligations[0].range.display() == "proof.sigil:9:3-31", "assert obligation range");
  expect(obligations[2].location.line == 6, "ensures obligation line");
  expect(obligations[2].location.column == 1, "ensures obligation column");
  expect(obligations[2].range.display() == "proof.sigil:6:1-31", "ensures obligation range");

  const auto smt = sigil::emit_smt_lib(obligations[2]);
  expect(smt.find("(declare-const x Int)") != std::string::npos, "declares x");
  expect(smt.find("(declare-const y Int)") != std::string::npos, "declares y");
  expect(smt.find("(declare-const result Int)") != std::string::npos, "declares result");
  expect(smt.find("(assert (>= x 0))") != std::string::npos, "emits precondition");
  expect(smt.find("(assert (= y (+ x 1)))") != std::string::npos, "emits let equality");
  const auto counterexample = sigil::render_source_counterexample(obligations[2], R"(
(
  (define-fun x () Int
    0)
  (define-fun result () Int
    (- 1))
)
)");
  expect(counterexample.find("x: i64 = 0") != std::string::npos,
         "renders source parameter counterexample");
  expect(counterexample.find("result: i64 = -1") != std::string::npos,
         "renders source result counterexample");

  const auto results = sigil::verify_obligations(obligations, false);
  expect(results.size() == 3, "verification result count");
  expect(results[0].status == sigil::VerificationStatus::Proven, "let assert proven syntactically");
  expect(results[1].status == sigil::VerificationStatus::Proven, "precondition assert proven");
  expect(results[2].status == sigil::VerificationStatus::Unknown, "ensure needs SMT solver");
  expect(results[0].location.line == obligations[0].location.line, "result keeps source line");
  expect(results[0].location.column == obligations[0].location.column,
         "result keeps source column");
  expect(results[0].range.display() == obligations[0].range.display(), "result keeps source range");

  const char* call_source = R"(
module calls;

fn add_one(x: i64) -> i64
requires non_negative: x >= 0;
ensures advanced: result > x;
{
  return x + 1;
}

fn use_add_one(x: i64) -> i64
requires non_negative: x >= 0;
ensures advanced: result > x;
{
  let y: i64 = add_one(x);
  assert call_advanced: y > x;
  return y;
}
)";

  const auto call_module = sigil::parse_source(call_source, "calls.sigil");
  const auto call_obligations = sigil::build_obligations(call_module);
  expect(call_obligations.size() == 4, "callee ensure plus call requires, assert, caller ensure");
  expect(call_obligations[1].name == "fn.use_add_one.call.1.requires.1.non_negative",
         "call precondition obligation name");
  expect(call_obligations[1].range.display() == "calls.sigil:15:16-25",
         "call precondition points to call site");
  expect(call_obligations[2].name == "fn.use_add_one.assert.1.call_advanced",
         "caller assertion name");
  const auto call_requires_results = sigil::verify_obligations(call_obligations, false);
  expect(call_requires_results[1].status == sigil::VerificationStatus::Proven,
         "caller precondition proves callee precondition syntactically");
  const auto call_assert_smt = sigil::emit_smt_lib(call_obligations[2]);
  expect(call_assert_smt.find("(assert (> add_one_call_1_") != std::string::npos,
         "assertion SMT assumes callee postcondition on call result");
  expect(call_assert_smt.find("(assert (= y add_one_call_1_") != std::string::npos,
         "assertion SMT binds let local to call result");

  const char* theorem_source = R"(
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
)";

  const auto theorem_module = sigil::parse_source(theorem_source, "theorems.sigil");
  const auto theorem_obligations = sigil::build_obligations(theorem_module);
  expect(theorem_obligations.size() == 6,
         "theorem proof, theorem call, assertion, and ensure obligations");
  expect(theorem_obligations[0].name == "theorem.add_one_gt.ensures.1.advanced",
         "theorem explicit ensure obligation name");
  expect(theorem_obligations[1].name == "theorem.add_one_gt.ensures.2.holds",
         "theorem implicit holds obligation name");
  expect(theorem_obligations[2].name == "fn.use_add_one.call.1.requires.1.non_negative",
         "theorem call precondition obligation name");
  expect(theorem_obligations[3].name == "fn.use_add_one.assert.1.lemma_call",
         "theorem call assertion obligation name");
  expect(theorem_obligations[4].name == "fn.use_add_one.assert.2.from_lemma",
         "theorem lemma reuse assertion obligation name");
  const auto theorem_call_smt = sigil::emit_smt_lib(theorem_obligations[3]);
  expect(theorem_call_smt.find("(assert (= add_one_gt_call_1_") != std::string::npos,
         "theorem call assumes implicit holds fact");
  const auto theorem_reuse_smt = sigil::emit_smt_lib(theorem_obligations[4]);
  expect(theorem_reuse_smt.find("(assert (> (+ x 1) x))") != std::string::npos,
         "theorem call assumes explicit theorem postcondition");
  expect(theorem_reuse_smt.find("(assert (= y (+ x 1)))") != std::string::npos,
         "theorem reuse sees local let binding");
  const auto theorem_results = sigil::verify_obligations(theorem_obligations, false);
  expect(theorem_results[2].status == sigil::VerificationStatus::Proven,
         "caller precondition proves theorem precondition syntactically");

  const char* struct_source = R"(
module structs;

struct Pair {
  left: i64;
  ok: bool;

  invariant left_non_negative_when_ok: !ok || left >= 0;
}

fn read_left(x: i64) -> i64
requires non_negative: x >= 0;
ensures exact: result == x;
{
  let pair: Pair = Pair { left: x, ok: true };
  assert field_visible: pair.left == x;
  return pair.left;
}
)";

  const auto struct_module = sigil::parse_source(struct_source, "structs.sigil");
  const auto struct_obligations = sigil::build_obligations(struct_module);
  expect(struct_obligations.size() == 3, "struct invariant, field assert, and ensure obligations");
  expect(struct_obligations[0].name ==
             "fn.read_left.struct.pair.invariant.1.left_non_negative_when_ok",
         "struct invariant obligation name");
  expect(struct_obligations[1].name == "fn.read_left.assert.1.field_visible",
         "struct field assertion name");
  const auto struct_smt = sigil::emit_smt_lib(struct_obligations[1]);
  expect(struct_smt.find("(declare-const pair_left Int)") != std::string::npos,
         "struct field symbol is declared");
  expect(struct_smt.find("(assert (= pair_left x))") != std::string::npos,
         "struct literal binds scalar field");
  const auto invariant_smt = sigil::emit_smt_lib(struct_obligations[0]);
  expect(invariant_smt.find("(assert (not (or (not pair_ok) (>= pair_left 0))))") !=
             std::string::npos,
         "struct invariant goal uses materialized fields");
  const auto struct_results = sigil::verify_obligations(struct_obligations, false);
  expect(struct_results[0].status == sigil::VerificationStatus::Unknown,
         "struct invariant needs SMT solver");
  expect(struct_results[1].status == sigil::VerificationStatus::Proven,
         "field assertion proven syntactically");

  const char* theorem_invariant_source = R"(
module invariant_lemmas;

theorem non_negative for (x: i64)
requires known: x >= 0;
ensures preserved: x >= 0;
{
  return x >= 0;
}

struct Box {
  value: i64;
  invariant value_non_negative: non_negative(value);
}

fn make_box(x: i64) -> i64
requires known: x >= 0;
{
  let box: Box = Box { value: x };
  return box.value;
}
)";

  const auto theorem_invariant_module =
      sigil::parse_source(theorem_invariant_source, "invariant-theorems.sigil");
  const auto theorem_invariant_obligations = sigil::build_obligations(theorem_invariant_module);
  expect(theorem_invariant_obligations.size() == 4,
         "theorem plus theorem-backed struct invariant obligations");
  expect(theorem_invariant_obligations[2].name == "fn.make_box.call.1.requires.1.known",
         "struct invariant theorem call precondition obligation");
  expect(theorem_invariant_obligations[3].name ==
             "fn.make_box.struct.box.invariant.1.value_non_negative",
         "struct invariant theorem obligation name");
  const auto theorem_invariant_smt = sigil::emit_smt_lib(theorem_invariant_obligations[3]);
  expect(theorem_invariant_smt.find("(assert (= non_negative_call_1_") != std::string::npos,
         "struct invariant assumes theorem call holds");
  const auto theorem_invariant_results =
      sigil::verify_obligations(theorem_invariant_obligations, false);
  expect(theorem_invariant_results[2].status == sigil::VerificationStatus::Proven,
         "struct field fact proves theorem precondition");
  expect(theorem_invariant_results[3].status == sigil::VerificationStatus::Proven,
         "theorem holds fact proves struct invariant");

  const char* generic_struct_source = R"(
module generic_structs;

struct Box[T] {
  value: T;
}

fn read_i64(x: i64) -> i64
ensures exact: result == x;
{
  let box: Box[i64] = Box[i64] { value: x };
  assert field_visible: box.value == x;
  return box.value;
}

fn read_bool(flag: bool) -> bool
ensures exact: result == flag;
{
  let box: Box[bool] = Box[bool] { value: flag };
  assert field_visible: box.value == flag;
  return box.value;
}
)";

  const auto generic_struct_module =
      sigil::parse_source(generic_struct_source, "generic-structs.sigil");
  const auto generic_struct_obligations = sigil::build_obligations(generic_struct_module);
  expect(generic_struct_obligations.size() == 4, "generic struct assert and ensure obligations");
  expect(generic_struct_obligations[0].name == "fn.read_i64.assert.1.field_visible",
         "generic i64 field assertion name");
  expect(generic_struct_obligations[2].name == "fn.read_bool.assert.1.field_visible",
         "generic bool field assertion name");
  const auto generic_bool_smt = sigil::emit_smt_lib(generic_struct_obligations[2]);
  expect(generic_bool_smt.find("(declare-const box_value Bool)") != std::string::npos,
         "generic bool field is declared as Bool");
  const auto generic_struct_results = sigil::verify_obligations(generic_struct_obligations, false);
  expect(generic_struct_results[0].status == sigil::VerificationStatus::Proven,
         "generic i64 field assertion proven");
  expect(generic_struct_results[2].status == sigil::VerificationStatus::Proven,
         "generic bool field assertion proven");

  const char* generic_invariant_source = R"(
module generic_invariants;

struct Witness[T] {
  value: T;
  invariant holds: value == true;
}

fn make_witness(flag: bool) -> bool
requires flag_true: flag == true;
ensures exact: result == true;
{
  let witness: Witness[bool] = Witness[bool] { value: flag };
  return witness.value;
}
)";

  const auto generic_invariant_module =
      sigil::parse_source(generic_invariant_source, "generic-invariants.sigil");
  const auto generic_invariant_obligations = sigil::build_obligations(generic_invariant_module);
  expect(generic_invariant_obligations.size() == 2, "generic invariant and ensure obligations");
  expect(generic_invariant_obligations[0].name ==
             "fn.make_witness.struct.witness.invariant.1.holds",
         "generic invariant obligation name");
  const auto generic_invariant_smt = sigil::emit_smt_lib(generic_invariant_obligations[0]);
  expect(generic_invariant_smt.find("(declare-const witness_value Bool)") != std::string::npos,
         "generic invariant field is declared as Bool");
  const auto generic_invariant_results =
      sigil::verify_obligations(generic_invariant_obligations, false);
  expect(generic_invariant_results[0].status == sigil::VerificationStatus::Proven,
         "generic bool invariant proven");
  expect(generic_invariant_results[1].status == sigil::VerificationStatus::Proven,
         "generic bool ensure proven");

  const char* container_source = R"(
module containers;

container Window[T] {
  items: Slice[T];
  index: i64;

  invariant index_non_negative: index >= 0;
  invariant index_within_items: index < len(items);
}

fn read_window(xs: Slice[i64], index: i64) -> i64
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == at(xs, index);
{
  let window: Window[i64] = Window[i64] { items: xs, index: index };
  assert len_visible: len(window.items) == len(xs);
  assert index_visible: window.index == index;
  return at(xs, index);
}
)";

  const auto container_module = sigil::parse_source(container_source, "containers.sigil");
  const auto container_obligations = sigil::build_obligations(container_module);
  expect(container_obligations.size() == 7, "container invariants, field asserts, safety, ensure");
  expect(container_obligations[0].name ==
             "fn.read_window.container.window.invariant.1.index_non_negative",
         "container first invariant obligation name");
  expect(container_obligations[1].name ==
             "fn.read_window.container.window.invariant.2.index_within_items",
         "container second invariant obligation name");
  expect(container_obligations[2].name == "fn.read_window.assert.1.len_visible",
         "container model field length assertion name");
  expect(container_obligations[3].name == "fn.read_window.assert.2.index_visible",
         "container scalar field assertion name");
  const auto container_len_smt = sigil::emit_smt_lib(container_obligations[2]);
  expect(container_len_smt.find("(declare-const window_items_len Int)") != std::string::npos,
         "container model field length symbol");
  expect(container_len_smt.find("(declare-const window_items_data (Array Int Int))") !=
             std::string::npos,
         "container model field data symbol");
  expect(container_len_smt.find("(assert (= window_items_len xs_len))") != std::string::npos,
         "container model field length aliases initializer");
  expect(container_len_smt.find("(assert (= window_items_data xs_data))") != std::string::npos,
         "container model field data aliases initializer");
  const auto container_invariant_smt = sigil::emit_smt_lib(container_obligations[1]);
  expect(container_invariant_smt.find("(assert (not (< window_index window_items_len)))") !=
             std::string::npos,
         "container invariant uses materialized model field length");
  const auto container_results = sigil::verify_obligations(container_obligations, false);
  expect(container_results[2].status == sigil::VerificationStatus::Proven,
         "container model field assertion proven locally");
  expect(container_results[3].status == sigil::VerificationStatus::Proven,
         "container scalar field assertion proven locally");

  const char* slice_model_source = R"(
module slice_model;

fn read_slice(xs: Slice[i64], index: i64) -> i64
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == at(xs, index);
{
  return at(xs, index);
}

fn read_array(flags: Array[bool], index: i64) -> bool
requires in_bounds: index >= 0 && index < len(flags);
ensures exact: result == at(flags, index);
{
  return at(flags, index);
}
)";

  const auto slice_model_module = sigil::parse_source(slice_model_source, "slice-model.sigil");
  const auto slice_model_obligations = sigil::build_obligations(slice_model_module);
  expect(slice_model_obligations.size() == 6, "array and slice model obligations");
  expect(slice_model_obligations[0].name == "fn.read_slice.safety.1.index_in_bounds",
         "slice return access bounds obligation");
  expect(slice_model_obligations[1].name == "fn.read_slice.safety.2.index_in_bounds",
         "slice ensure access bounds obligation");
  expect(slice_model_obligations[2].name == "fn.read_slice.ensures.1.exact",
         "slice ensure obligation");
  expect(slice_model_obligations[3].name == "fn.read_array.safety.1.index_in_bounds",
         "array return access bounds obligation");
  const auto slice_safety_smt = sigil::emit_smt_lib(slice_model_obligations[0]);
  expect(slice_safety_smt.find("(declare-const xs_len Int)") != std::string::npos,
         "slice length is declared");
  expect(slice_safety_smt.find("(declare-const xs_data (Array Int Int))") != std::string::npos,
         "slice i64 data model is declared");
  expect(slice_safety_smt.find("(assert (not (and (>= index 0) (< index xs_len))))") !=
             std::string::npos,
         "slice bounds goal uses len");
  const auto array_ensure_smt = sigil::emit_smt_lib(slice_model_obligations[5]);
  expect(array_ensure_smt.find("(declare-const flags_data (Array Int Bool))") != std::string::npos,
         "array bool data model is declared");
  expect(array_ensure_smt.find("(assert (= result (select flags_data index)))") !=
             std::string::npos,
         "array ensure assumes selected element");
  const auto slice_model_results = sigil::verify_obligations(slice_model_obligations, false);
  expect(slice_model_results[0].status == sigil::VerificationStatus::Proven,
         "slice bounds proven from precondition");
  expect(slice_model_results[2].status == sigil::VerificationStatus::Proven,
         "slice ensure proven from return binding");
  expect(slice_model_results[3].status == sigil::VerificationStatus::Proven,
         "array bounds proven from precondition");
  expect(slice_model_results[5].status == sigil::VerificationStatus::Proven,
         "array ensure proven from return binding");

  const char* ref_model_source = R"(
module ref_model;

fn read_ref(ptr: Ref[i64]) -> i64
requires valid: is_valid(ptr);
ensures exact: result == load(ptr);
{
  return load(ptr);
}

fn read_flag(ptr: Ref[bool]) -> bool
requires valid: is_valid(ptr);
ensures exact: result == load(ptr);
{
  return load(ptr);
}

fn refs_are_disjoint(left: Ref[i64], right: Ref[i64]) -> bool
ensures exact: result == disjoint(left, right);
{
  return disjoint(left, right);
}
)";

  const auto ref_model_module = sigil::parse_source(ref_model_source, "ref-model.sigil");
  const auto ref_model_obligations = sigil::build_obligations(ref_model_module);
  expect(ref_model_obligations.size() == 7, "ref model obligations");
  expect(ref_model_obligations[0].name == "fn.read_ref.safety.1.memory_valid",
         "ref return load validity obligation");
  expect(ref_model_obligations[1].name == "fn.read_ref.safety.2.memory_valid",
         "ref ensure load validity obligation");
  expect(ref_model_obligations[2].name == "fn.read_ref.ensures.1.exact", "ref ensure obligation");
  expect(ref_model_obligations[6].name == "fn.refs_are_disjoint.ensures.1.exact",
         "ref disjoint ensure obligation");
  const auto ref_safety_smt = sigil::emit_smt_lib(ref_model_obligations[0]);
  expect(ref_safety_smt.find("(declare-const ptr_valid Bool)") != std::string::npos,
         "ref validity is declared");
  expect(ref_safety_smt.find("(declare-const ptr_value Int)") != std::string::npos,
         "ref i64 value is declared");
  expect(ref_safety_smt.find("(assert (not ptr_valid))") != std::string::npos,
         "ref validity goal is emitted");
  const auto bool_ref_ensure_smt = sigil::emit_smt_lib(ref_model_obligations[5]);
  expect(bool_ref_ensure_smt.find("(declare-const ptr_value Bool)") != std::string::npos,
         "ref bool value is declared");
  const auto disjoint_smt = sigil::emit_smt_lib(ref_model_obligations[6]);
  expect(disjoint_smt.find("(declare-const left_addr Int)") != std::string::npos,
         "left ref address is declared");
  expect(disjoint_smt.find("(assert (= result (distinct left_addr right_addr)))") !=
             std::string::npos,
         "disjoint lowers to address inequality");
  const auto ref_model_results = sigil::verify_obligations(ref_model_obligations, false);
  expect(ref_model_results[0].status == sigil::VerificationStatus::Proven,
         "ref load validity proven from precondition");
  expect(ref_model_results[2].status == sigil::VerificationStatus::Proven,
         "ref ensure proven from return binding");
  expect(ref_model_results[3].status == sigil::VerificationStatus::Proven,
         "bool ref load validity proven from precondition");
  expect(ref_model_results[6].status == sigil::VerificationStatus::Proven,
         "ref disjoint ensure proven from return binding");

  const char* conditional_source = R"(
module conditional;

fn abs_value(x: i64) -> i64
ensures non_negative: result >= 0;
{
  let y: i64 = if x >= 0 { x } else { -x };
  return y;
}
)";

  const auto conditional_module = sigil::parse_source(conditional_source, "conditional.sigil");
  const auto conditional_obligations = sigil::build_obligations(conditional_module);
  expect(conditional_obligations.size() == 1, "conditional ensure obligation");
  const auto conditional_smt = sigil::emit_smt_lib(conditional_obligations[0]);
  expect(conditional_smt.find("(assert (= y (ite (>= x 0) x (- x))))") != std::string::npos,
         "emits ite for if expression");
  const auto timeout_smt = sigil::emit_smt_lib(conditional_obligations[0], 250);
  expect(timeout_smt.find("(set-option :timeout 250)") != std::string::npos,
         "emits solver timeout");

  const char* division_source = R"(
module division;

fn safe_div(x: i64, y: i64) -> i64
requires nonzero: y != 0;
ensures exact: result == x / y;
{
  return x / y;
}

fn unsafe_div(x: i64, y: i64) -> i64
{
  return x / y;
}

fn guarded_div(flag: bool, x: i64, y: i64) -> i64
requires safe_when_used: !flag || y != 0;
{
  let q: i64 = if flag { x / y } else { 0 };
  return q;
}

fn guarded_and(x: i64, y: i64) -> i64
requires safe_rhs: y != 0 && x / y >= 0;
{
  return x;
}

fn guarded_or(x: i64, y: i64) -> i64
requires zero_or_safe: y == 0 || x / y >= 0;
{
  return x;
}
)";

  const auto division_module = sigil::parse_source(division_source, "division.sigil");
  const auto division_obligations = sigil::build_obligations(division_module);
  expect(division_obligations.size() == 7, "division emits safety and ensure obligations");
  expect(division_obligations[0].name == "fn.safe_div.safety.1.divisor_nonzero",
         "safe return division safety name");
  expect(division_obligations[1].name == "fn.safe_div.safety.2.divisor_nonzero",
         "safe ensure division safety name");
  expect(division_obligations[2].name == "fn.safe_div.ensures.1.exact",
         "safe division ensure name");
  expect(division_obligations[3].name == "fn.unsafe_div.safety.1.divisor_nonzero",
         "unsafe division safety name");
  expect(division_obligations[4].name == "fn.guarded_div.safety.1.divisor_nonzero",
         "guarded branch division safety name");
  expect(division_obligations[5].name == "fn.guarded_and.safety.1.divisor_nonzero",
         "and short-circuit division safety name");
  expect(division_obligations[6].name == "fn.guarded_or.safety.1.divisor_nonzero",
         "or short-circuit division safety name");
  expect(division_obligations[0].range.display() == "division.sigil:8:14",
         "return division safety points to divisor");
  const auto division_results = sigil::verify_obligations(division_obligations, false);
  expect(division_results[0].status == sigil::VerificationStatus::Proven,
         "precondition proves return divisor safety locally");
  expect(division_results[1].status == sigil::VerificationStatus::Proven,
         "precondition proves ensure divisor safety locally");
  expect(division_results[2].status == sigil::VerificationStatus::Proven,
         "return equality proves exact ensure locally");
  expect(division_results[3].status == sigil::VerificationStatus::Unknown,
         "unsafe divisor needs solver counterexample");
  expect(division_results[5].status == sigil::VerificationStatus::Proven,
         "&& lhs proves rhs divisor safety locally");
  const auto guarded_smt = sigil::emit_smt_lib(division_obligations[4]);
  expect(guarded_smt.find("(assert (or (not flag) (distinct y 0)))") != std::string::npos,
         "guarded safety keeps precondition");
  expect(guarded_smt.find("(assert flag)") != std::string::npos,
         "guarded safety assumes selected branch");
  const auto and_guarded_smt = sigil::emit_smt_lib(division_obligations[5]);
  expect(and_guarded_smt.find("(assert (distinct y 0))") != std::string::npos,
         "&& rhs safety assumes lhs");
  const auto or_guarded_smt = sigil::emit_smt_lib(division_obligations[6]);
  expect(or_guarded_smt.find("(assert (not (= y 0)))") != std::string::npos,
         "|| rhs safety assumes negated lhs");

  const char* branch_source = R"(
module branches;

fn branch_abs(x: i64) -> i64
ensures non_negative: result >= 0;
{
  if x >= 0 {
    assert then_guard: x >= 0;
    return x;
  } else {
    return -x;
  }
}
)";

  const auto branch_module = sigil::parse_source(branch_source, "branches.sigil");
  const auto branch_obligations = sigil::build_obligations(branch_module);
  expect(branch_obligations.size() == 3, "branch assert plus return-path ensure obligations");
  expect(branch_obligations[0].name == "fn.branch_abs.assert.1.then_guard", "branch assert name");
  const auto branch_results = sigil::verify_obligations(branch_obligations, false);
  expect(branch_results[0].status == sigil::VerificationStatus::Proven,
         "then branch assertion proven by branch condition");
  const auto branch_smt = sigil::emit_smt_lib(branch_obligations[1]);
  expect(branch_obligations[1].name == "fn.branch_abs.return.1.ensures.1.non_negative",
         "then return ensure name");
  expect(branch_smt.find("(assert (>= x 0))") != std::string::npos,
         "then return path assumes branch condition");
  expect(branch_smt.find("(assert (= result x))") != std::string::npos,
         "then return path binds result");
  const auto branch_else_smt = sigil::emit_smt_lib(branch_obligations[2]);
  expect(branch_obligations[2].name == "fn.branch_abs.return.2.ensures.1.non_negative",
         "else return ensure name");
  expect(branch_else_smt.find("(assert (not (>= x 0)))") != std::string::npos,
         "else return path assumes negated branch condition");
  expect(branch_else_smt.find("(assert (= result (- x)))") != std::string::npos,
         "else return path binds result");

  const char* early_return_source = R"(
module early;

fn early_then(flag: bool) -> i64
ensures zero: result == 0;
{
  if flag {
    return 1;
  } else {
    assume keep_going: true;
  }
  return 0;
}
)";

  const auto early_return_module = sigil::parse_source(early_return_source, "early.sigil");
  const auto early_return_obligations = sigil::build_obligations(early_return_module);
  expect(early_return_obligations.size() == 2, "early return creates two return-path ensures");
  expect(early_return_obligations[0].name == "fn.early_then.return.1.ensures.1.zero",
         "early return path ensure name");
  const auto early_then_smt = sigil::emit_smt_lib(early_return_obligations[0]);
  expect(early_then_smt.find("(assert flag)") != std::string::npos,
         "early return path keeps then guard");
  expect(early_then_smt.find("(assert (= result 1))") != std::string::npos,
         "early return path keeps original return value");
  expect(early_then_smt.find("(assert (= result 0))") == std::string::npos,
         "early return path is not overwritten by later return");
  const auto early_after_smt = sigil::emit_smt_lib(early_return_obligations[1]);
  expect(early_after_smt.find("(assert (not flag))") != std::string::npos,
         "continuing return path keeps else guard");
  expect(early_after_smt.find("(assert (= result 0))") != std::string::npos,
         "continuing return path binds later return value");

  const char* void_source = R"(
module voids;

fn void_return(x: i64) -> void
requires non_negative: x >= 0;
ensures preserved: x >= 0;
{
  return;
}

fn void_branch(flag: bool) -> void
ensures tautology: flag || !flag;
{
  if flag {
    return;
  } else {
    return;
  }
}

fn void_fallthrough(flag: bool) -> void
ensures tautology: flag || !flag;
{
  if flag {
    return;
  } else {
    assume else_path: !flag;
  }
}
)";

  const auto void_module = sigil::parse_source(void_source, "voids.sigil");
  const auto void_obligations = sigil::build_obligations(void_module);
  expect(void_obligations.size() == 5, "void ensures include explicit and fallthrough paths");
  expect(void_obligations[0].name == "fn.void_return.ensures.1.preserved",
         "single void return keeps stable ensure name");
  expect(void_obligations[1].name == "fn.void_branch.return.1.ensures.1.tautology",
         "void then return ensure names return path");
  expect(void_obligations[2].name == "fn.void_branch.return.2.ensures.1.tautology",
         "void else return ensure names return path");
  expect(void_obligations[3].name == "fn.void_fallthrough.return.1.ensures.1.tautology",
         "void mixed explicit return names return path");
  expect(void_obligations[4].name == "fn.void_fallthrough.fallthrough.ensures.1.tautology",
         "void mixed fallthrough names fallthrough path");
  const auto void_return_smt = sigil::emit_smt_lib(void_obligations[0]);
  expect(void_return_smt.find("result") == std::string::npos, "void return has no result symbol");
  const auto void_explicit_smt = sigil::emit_smt_lib(void_obligations[3]);
  expect(void_explicit_smt.find("(assert flag)") != std::string::npos,
         "void explicit return keeps branch condition");
  const auto void_fallthrough_smt = sigil::emit_smt_lib(void_obligations[4]);
  expect(void_fallthrough_smt.find("(assert (not flag))") != std::string::npos,
         "void fallthrough keeps branch condition");

  const char* assignment_source = R"(
module assignment;

fn increment_once(x: i64) -> i64
requires non_negative: x >= 0;
ensures advanced: result > x;
{
  let y: i64 = x;
  y = y + 1;
  return y;
}

fn branch_mutation(flag: bool, x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = 0;
  if flag {
    y = x;
  } else {
    y = 0;
  }
  return y;
}
)";

  const auto assignment_module = sigil::parse_source(assignment_source, "assignment.sigil");
  const auto assignment_obligations = sigil::build_obligations(assignment_module);
  expect(assignment_obligations.size() == 2, "assignment examples produce two ensures");
  const auto assignment_smt = sigil::emit_smt_lib(assignment_obligations[0]);
  expect(assignment_smt.find("y_assign_") != std::string::npos,
         "assignment creates versioned symbol");
  expect(assignment_smt.find("(assert (= y_assign_") != std::string::npos,
         "assignment emits version equality");
  const auto branch_assignment_smt = sigil::emit_smt_lib(assignment_obligations[1]);
  expect(assignment_obligations[1].name == "fn.branch_mutation.ensures.1.preserved",
         "single return path keeps stable ensure name");
  expect(branch_assignment_smt.find("y_join_") != std::string::npos,
         "branch assignment creates join symbol");
  expect(branch_assignment_smt.find("(ite flag") != std::string::npos,
         "branch assignment join uses ite");

  const char* wp_source = R"(
module wp;

fn two_steps(x: i64) -> i64
ensures exact: result == (x + 1) + 1;
{
  let y: i64 = x;
  y = y + 1;
  y = y + 1;
  assert exact_now: y == (x + 1) + 1;
  return y;
}
)";

  const auto wp_module = sigil::parse_source(wp_source, "wp.sigil");
  const auto wp_obligations = sigil::build_obligations(wp_module);
  expect(wp_obligations.size() == 2, "wp assert plus ensure obligations");
  const auto wp_results = sigil::verify_obligations(wp_obligations, false);
  expect(wp_results[0].status == sigil::VerificationStatus::Proven,
         "wp proves straight-line assignment assertion locally");
  expect(wp_results[1].status == sigil::VerificationStatus::Proven,
         "wp proves straight-line assignment ensure locally");
  expect(wp_results[0].details == "proved by weakest-precondition substitution",
         "wp assertion uses local substitution proof rule");
  expect(wp_results[1].details == "proved by weakest-precondition substitution",
         "wp ensure uses local substitution proof rule");

  const char* hint_source = R"(
module hints;

fn add_one(x: i64) -> i64
ensures advanced: result > x;
{
  return x + 1;
}
)";

  const auto hint_module = sigil::parse_source(hint_source, "hints.sigil");
  const auto hint_obligations = sigil::build_obligations(hint_module);
  const auto hint_results = sigil::verify_obligations(hint_obligations, false);
  const auto hint_artifacts = sigil::build_proof_hint_artifacts(hint_obligations, hint_results);
  expect(hint_artifacts.size() == 1, "proof hint artifact generated for unproven obligation");
  expect(hint_artifacts[0].file_name == "fn.add_one.ensures.1.advanced.proof-hint.txt",
         "proof hint file name is stable");
  expect(hint_artifacts[0].text.find("sigil-proof-hint-v1") != std::string::npos,
         "proof hint includes format marker");
  expect(hint_artifacts[0].text.find("goal:\n  advanced: (result > x)") != std::string::npos,
         "proof hint includes source goal");
  expect(hint_artifacts[0].text.find("agent-contract:") != std::string::npos,
         "proof hint includes agent contract");
  expect(hint_artifacts[0].text.find("(check-sat)") != std::string::npos,
         "proof hint embeds smt query");

  const auto agent_artifacts = sigil::build_agent_handoff_artifacts(hint_obligations, hint_results);
  expect(agent_artifacts.size() == 2, "agent handoff artifacts generated");
  expect(agent_artifacts[0].label == "agent-request", "agent request label");
  expect(agent_artifacts[0].file_name == "fn.add_one.ensures.1.advanced.agent-request.txt",
         "agent request file name is stable");
  expect(agent_artifacts[0].text.find("sigil-agent-request-v1") != std::string::npos,
         "agent request includes format marker");
  expect(agent_artifacts[0].text.find(
             "candidate-file: fn.add_one.ensures.1.advanced.candidate.sigil") != std::string::npos,
         "agent request points at candidate skeleton");
  expect(agent_artifacts[0].text.find("acceptance-gate:") != std::string::npos,
         "agent request includes acceptance gate");
  expect(agent_artifacts[1].label == "theorem-candidate", "theorem candidate label");
  expect(agent_artifacts[1].file_name == "fn.add_one.ensures.1.advanced.candidate.sigil",
         "theorem candidate file name is stable");
  expect(agent_artifacts[1].text.find("sigil-theorem-candidate-v1") != std::string::npos,
         "theorem candidate includes marker");
  expect(agent_artifacts[1].text.find("module candidate_fn_add_one_ensures_1_advanced;") !=
             std::string::npos,
         "theorem candidate module is sanitized");

  const char* loop_source = R"(
module loops;

fn count_to(n: i64) -> i64
requires non_negative: n >= 0;
ensures exact: result == n;
{
  let i: i64 = 0;
  while i < n
  invariant lower: i >= 0;
  invariant upper: i <= n;
  {
    i = i + 1;
  }
  return i;
}
)";

  const auto loop_module = sigil::parse_source(loop_source, "loops.sigil");
  const auto loop_obligations = sigil::build_obligations(loop_module);
  expect(loop_obligations.size() == 5, "loop invariants plus ensure obligations");
  expect(loop_obligations[0].name == "fn.count_to.loop.1.invariant.1.lower.initial",
         "loop initial invariant name");
  expect(loop_obligations[2].name == "fn.count_to.loop.1.invariant.1.lower.preserved",
         "loop preserved invariant name");
  expect(loop_obligations[4].name == "fn.count_to.ensures.1.exact", "loop ensure name");
  const auto loop_preserve_smt = sigil::emit_smt_lib(loop_obligations[3]);
  expect(loop_preserve_smt.find("i_loop_") != std::string::npos,
         "loop preservation uses loop head symbol");
  expect(loop_preserve_smt.find("i_assign_") != std::string::npos,
         "loop preservation uses assigned symbol");
  const auto loop_ensure_smt = sigil::emit_smt_lib(loop_obligations[4]);
  expect(loop_ensure_smt.find("i_loop_exit_") != std::string::npos,
         "loop ensure uses loop exit symbol");
  expect(loop_ensure_smt.find("(not (< i_loop_exit_") != std::string::npos,
         "loop ensure assumes exit condition");
  return 0;
}

#include "sigil/parser.hpp"
#include "sigil/typecheck.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void expect_diagnostic(const char* source, const std::string& needle, std::size_t expected_line,
                       std::size_t expected_column, const std::string& expected_range = "") {
  try {
    const auto module = sigil::parse_source(source, "typecheck.sigil");
    sigil::validate_module(module);
  } catch (const sigil::Diagnostic& diagnostic) {
    const std::string message = diagnostic.what();
    if (message.find(needle) != std::string::npos) {
      if (expected_line != 0) {
        expect(diagnostic.location().line == expected_line, "typecheck diagnostic line");
        expect(diagnostic.location().column == expected_column, "typecheck diagnostic column");
      }
      if (!expected_range.empty()) {
        expect(diagnostic.range().display() == expected_range, "typecheck diagnostic range");
      }
      return;
    }
    std::cerr << "FAIL: diagnostic did not contain '" << needle << "': " << message << "\n";
    std::exit(1);
  }

  std::cerr << "FAIL: expected diagnostic containing '" << needle << "'\n";
  std::exit(1);
}

void expect_diagnostic(const char* source, const std::string& needle) {
  expect_diagnostic(source, needle, 0, 0);
}

} // namespace

int main() {
  const char* valid = R"(
module ok;

struct Counter {
  value: i64;
  invariant non_negative: value >= 0;
}

struct Pair {
  left: i64;
  ok: bool;
}

struct Box[T] {
  value: T;
}

struct PairBox[A, B] {
  left: A;
  right: B;
}

container Window[T] {
  items: Slice[T];
  index: i64;

  invariant index_non_negative: index >= 0;
  invariant index_within_items: index < len(items);
}

theorem nonzero_stays_nonzero for (x: i64)
requires nonzero: x != 0;
ensures preserved: x != 0;
{
  return x != 0;
}

fn id(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = x + 1;
  let z: i64 = if y >= 0 { y } else { 0 };
  z = z + 1;
  assert visible: x >= 0;
  assert y_visible: z >= x;
  if z >= x {
    assert branch_visible: z >= x;
  } else {
    assume impossible: false;
  }
  return z;
}

fn branch_returns(flag: bool, x: i64) -> i64
{
  if flag {
    return x;
  } else {
    return 0;
  }
}

fn loop_valid(n: i64) -> i64
requires non_negative: n >= 0;
ensures bounded: result <= n;
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

fn observe(flag: bool, x: i64) -> void
requires non_negative: x >= 0;
ensures still_non_negative: x >= 0;
{
  if flag {
    return;
  } else {
    assume keep_going: true;
  }
}

fn implicit_labels_can_repeat(x: i64) -> i64
{
  assert x == x;
  assume true;
  assert x >= x;
  return x;
}

fn add_one(x: i64) -> i64
requires non_negative: x >= 0;
ensures positive: result > x;
{
  return x + 1;
}

fn is_nonzero(x: i64) -> bool
{
  return x != 0;
}

fn call_examples(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= x;
{
  let next: i64 = add_one(x);
  let flag: bool = is_nonzero(next);
  if flag {
    return next;
  } else {
    return add_later(next, 0);
  }
}

fn add_later(x: i64, y: i64) -> i64
{
  return x + y;
}

fn read_pair(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, ok: true };
  assert field_visible: pair.left == x;
  return pair.left;
}

fn unwrap_box(x: i64) -> i64
{
  let box: Box[i64] = Box[i64] { value: x };
  assert field_visible: box.value == x;
  return box.value;
}

fn read_pair_box(x: i64, flag: bool) -> i64
{
  let pair: PairBox[i64, bool] = PairBox[i64, bool] { left: x, right: flag };
  if pair.right {
    return pair.left;
  } else {
    return 0;
  }
}

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

fn read_window(xs: Slice[i64], index: i64) -> i64
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == at(xs, index);
{
  let window: Window[i64] = Window[i64] { items: xs, index: index };
  assert len_visible: len(window.items) == len(xs);
  assert index_visible: window.index == index;
  return at(xs, index);
}

fn write_then_read(xs: Slice[i64], index: i64, value: i64) -> i64
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == value;
{
  let updated: Slice[i64] = store(xs, index, value);
  assert length_preserved: len(updated) == len(xs);
  return at(updated, index);
}

fn update_flag(flags: Array[bool], index: i64, value: bool) -> bool
requires in_bounds: index >= 0 && index < len(flags);
ensures exact: result == value;
{
  let updated: Array[bool] = store(flags, index, value);
  return at(updated, index);
}

fn read_ref(ptr: Ref[i64]) -> i64
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

fn write_then_load(ptr: Ref[i64], value: i64) -> i64
requires valid: is_valid(ptr);
ensures exact: result == value;
{
  let updated: Ref[i64] = store(ptr, value);
  assert still_valid: is_valid(updated);
  assert same_address: addr(updated) == addr(ptr);
  return load(updated);
}

fn update_flag_ref(ptr: Ref[bool], value: bool) -> bool
requires valid: is_valid(ptr);
ensures exact: result == value;
{
  let updated: Ref[bool] = store(ptr, value);
  return load(updated);
}

fn entry_epochs_match(left: Ref[i64], right: Ref[bool]) -> i64
ensures same_entry_epoch: epoch(left) == epoch(right);
{
  return 0;
}

fn store_advances_epoch(ptr: Ref[i64], value: i64) -> i64
requires valid: is_valid(ptr);
ensures next_epoch: result == epoch(ptr) + 1;
{
  let updated: Ref[i64] = store(ptr, value);
  return epoch(updated);
}

fn proof_only_lemma_use(x: i64) -> i64
requires nonzero: x != 0;
ensures preserved: result != 0;
{
  assert theorem_visible: nonzero_stays_nonzero(x);
  return x;
}
)";

  sigil::validate_module(sigil::parse_source(valid, "valid.sigil"));

  expect_diagnostic(R"(
module bad;
struct Box[T] {
  value: T;
}

fn missing_type_arg(x: i64) -> i64
{
  let box: Box = Box { value: x };
  return box.value;
}
)",
                    "generic struct 'Box' expects 1 type argument(s), got 0");

  expect_diagnostic(R"(
module bad;
struct Box[T] {
  value: T;
}

fn too_many_type_args(x: i64) -> i64
{
  let box: Box[i64, bool] = Box[i64, bool] { value: x };
  return box.value;
}
)",
                    "generic struct 'Box' expects 1 type argument(s), got 2");

  expect_diagnostic(R"(
module bad;
struct Pair {
  value: i64;
}

fn non_generic_has_args(x: i64) -> i64
{
  let pair: Pair[i64] = Pair[i64] { value: x };
  return pair.value;
}
)",
                    "struct 'Pair' expects 0 type argument(s), got 1");

  expect_diagnostic(R"(
module bad;
fn builtin_type_args(x: i64) -> i64
{
  let y: i64[i64] = x;
  return y;
}
)",
                    "type 'i64' cannot take type arguments");

  expect_diagnostic(R"(
module bad;
struct Box[T] {
  value: T;
}

fn wrong_field_type(x: i64) -> i64
{
  let box: Box[i64] = Box[i64] { value: true };
  return x;
}
)",
                    "field 'Box.value' type mismatch: expected i64, found bool");

  expect_diagnostic(R"(
module bad;
struct Box[T] {
  value: T;
}

fn wrong_literal_type(x: i64) -> i64
{
  let box: Box[bool] = Box[i64] { value: x };
  return x;
}
)",
                    "let type mismatch: expected Box[bool], found Box[i64]");

  expect_diagnostic(R"(
module bad;
struct Box[T, T] {
  value: T;
}
)",
                    "duplicate type parameter 'T'");

  expect_diagnostic(R"(
module bad;
struct Box[i64] {
  value: i64;
}
)",
                    "type parameter cannot use reserved type name 'i64'");

  expect_diagnostic(R"(
module bad;
struct LowerBound[T] {
  value: T;
  invariant non_negative: value >= 0;
}

fn invalid_invariant_instantiation(flag: bool) -> bool
{
  let box: LowerBound[bool] = LowerBound[bool] { value: flag };
  return box.value;
}
)",
                    "comparison operator requires i64 operands");

  expect_diagnostic(R"(
module bad;
fn missing_model_type_arg(xs: Slice) -> i64
{
  return 0;
}
)",
                    "model type 'Slice' expects 1 type argument(s), got 0");

  expect_diagnostic(R"(
module bad;
fn bad_model_element(xs: Slice[void]) -> i64
{
  return 0;
}
)",
                    "type argument for 'Slice[void]' cannot use void as a type argument");

  expect_diagnostic(R"(
module bad;
fn len_on_scalar(x: i64) -> i64
{
  return len(x);
}
)",
                    "len expects an Array[T] or Slice[T] argument");

  expect_diagnostic(R"(
module bad;
fn at_index_type(xs: Slice[i64], flag: bool) -> i64
{
  return at(xs, flag);
}
)",
                    "at index must be i64");

  expect_diagnostic(R"(
module bad;
fn store_arity(xs: Slice[i64], index: i64, value: i64) -> i64
{
  let updated: Slice[i64] = store(xs, index);
  return at(updated, index);
}
)",
                    "store expects 3 arguments, got 2");

  expect_diagnostic(R"(
module bad;
fn store_scalar(x: i64, index: i64, value: i64) -> i64
{
  let updated: i64 = store(x, index, value);
  return updated;
}
)",
                    "store expects an Array[T] or Slice[T] argument");

  expect_diagnostic(R"(
module bad;
fn store_index_type(xs: Slice[i64], flag: bool, value: i64) -> i64
{
  let updated: Slice[i64] = store(xs, flag, value);
  return at(updated, 0);
}
)",
                    "store index must be i64");

  expect_diagnostic(R"(
module bad;
fn store_value_type(xs: Slice[i64], index: i64, flag: bool) -> i64
{
  let updated: Slice[i64] = store(xs, index, flag);
  return at(updated, index);
}
)",
                    "store value type mismatch: expected i64, found bool");

  expect_diagnostic(R"(
module bad;
fn store_ref_scalar(x: i64, value: i64) -> i64
{
  let updated: i64 = store(x, value);
  return updated;
}
)",
                    "store expects a Ref[T] argument");

  expect_diagnostic(R"(
module bad;
fn store_ref_value_type(ptr: Ref[i64], flag: bool) -> i64
{
  let updated: Ref[i64] = store(ptr, flag);
  return load(updated);
}
)",
                    "store value type mismatch: expected i64, found bool");

  expect_diagnostic(R"(
module bad;
fn store_ref_target_type(ptr: Ref[i64], value: i64) -> i64
{
  let updated: Ref[bool] = store(ptr, value);
  return 0;
}
)",
                    "let type mismatch: expected Ref[bool], found Ref[i64]");

  expect_diagnostic(R"(
module bad;
struct Pair {
  value: i64;
}

fn bad_model_element(xs: Slice[Pair]) -> i64
{
  return 0;
}
)",
                    "model type 'Slice' element type cannot be aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Window {
  items: Slice[i64];
}
)",
                    "field 'Window.items' cannot use model type 'Slice[i64]' until aggregate model "
                    "fields are supported");

  expect_diagnostic(R"(
module bad;
container Bad {
  item: Slice[void];
}
)",
                    "type argument for 'Slice[void]' cannot use void as a type argument");

  expect_diagnostic(R"(
module bad;
fn load_scalar(x: i64) -> i64
{
  return load(x);
}
)",
                    "load expects a Ref[T] argument");

  expect_diagnostic(R"(
module bad;
fn epoch_scalar(x: i64) -> i64
{
  return epoch(x);
}
)",
                    "epoch expects a Ref[T] argument");

  expect_diagnostic(R"(
module bad;
fn bad_ref_element(ptr: Ref[Slice[i64]]) -> i64
{
  return 0;
}
)",
                    "model type 'Ref' element type cannot be aggregate type 'Slice[i64]'");

  expect_diagnostic(R"(
module bad;
fn ref_as_return(ptr: Ref[i64]) -> Ref[i64]
{
  return ptr;
}
)",
                    "function 'ref_as_return' return type cannot use aggregate type 'Ref[i64]' "
                    "until aggregate returns are supported");

  expect_diagnostic(R"(
module bad;
fn missing(x: i64) -> i64
requires nope: missing_name >= 0;
{
  return x;
}
)",
                    "unknown identifier 'missing_name'", 4, 16, "typecheck.sigil:4:16-27");

  expect_diagnostic(R"(
module bad;
fn mismatched(flag: bool) -> i64
{
  return flag;
}
)",
                    "return type mismatch");

  expect_diagnostic(R"(
module bad;
fn nonvoid_empty_return(x: i64) -> i64
{
  return;
}
)",
                    "non-void functions must return a value");

  expect_diagnostic(R"(
module bad;
fn void_value_return(x: i64) -> void
{
  return x;
}
)",
                    "void functions cannot return a value");

  expect_diagnostic(R"(
module bad;
fn bad_let(x: i64) -> i64
{
  let flag: bool = x + 1;
  return x;
}
)",
                    "let type mismatch");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

struct Node {
  next: Node;
}
)",
                    "recursive struct value types are not supported yet");

  expect_diagnostic(R"(
module bad;
struct A {
  b: B;
}

struct B {
  a: A;
}
)",
                    "recursive struct value types are not supported yet");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn aggregate_parameter(pair: Pair) -> i64
{
  return pair.left;
}
)",
                    "parameter 'aggregate_parameter.pair' cannot use aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn aggregate_return(x: i64) -> Pair
{
  return Pair { left: x, ok: true };
}
)",
                    "function 'aggregate_return' return type cannot use aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn copy_pair(x: i64) -> i64
{
  let first: Pair = Pair { left: x, ok: true };
  let second: Pair = first;
  return second.left;
}
)",
                    "struct local 'copy_pair.second' must be initialized with a struct literal");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn assign_pair(x: i64) -> i64
{
  let first: Pair = Pair { left: x, ok: true };
  let second: Pair = Pair { left: 0, ok: false };
  second = first;
  return second.left;
}
)",
                    "assignment target 'second' has aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn compare_pairs(x: i64) -> bool
{
  let first: Pair = Pair { left: x, ok: true };
  let second: Pair = Pair { left: x, ok: true };
  return first == second;
}
)",
                    "equality does not support aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn choose_pair(flag: bool, x: i64) -> i64
{
  let pair: Pair = if flag {
    Pair { left: x, ok: true }
  } else {
    Pair { left: 0, ok: false }
  };
  return pair.left;
}
)",
                    "if expression cannot produce aggregate type 'Pair'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn missing_field(x: i64) -> i64
{
  let pair: Pair = Pair { left: x };
  return pair.left;
}
)",
                    "missing initializer for field 'Pair.ok'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn duplicate_field(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, left: 1, ok: true };
  return pair.left;
}
)",
                    "duplicate initializer for field 'left'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn unknown_field(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, nope: true, ok: true };
  return pair.left;
}
)",
                    "struct 'Pair' has no field 'nope'");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn wrong_field_type(x: i64) -> i64
{
  let pair: Pair = Pair { left: true, ok: true };
  return pair.left;
}
)",
                    "field 'Pair.left' type mismatch");

  expect_diagnostic(R"(
module bad;
struct Pair {
  left: i64;
  ok: bool;
}

fn missing_access(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, ok: true };
  return pair.right;
}
)",
                    "struct 'Pair' has no field 'right'");

  expect_diagnostic(R"(
module bad;
fn field_on_scalar(x: i64) -> i64
{
  return x.left;
}
)",
                    "field access requires an aggregate value, found i64");

  expect_diagnostic(R"(
module bad;
fn caller(x: i64) -> i64
{
  return missing_call(x);
}
)",
                    "unknown function 'missing_call'");

  expect_diagnostic(R"(
module bad;
theorem is_nonzero for (x: i64)
{
  return x != 0;
}

fn caller(x: i64) -> bool
{
  let ok: bool = is_nonzero(x);
  return ok;
}
)",
                    "theorem 'is_nonzero' can only be used in proof-only expressions");

  expect_diagnostic(R"(
module bad;
theorem returns_i64 for (x: i64)
{
  return x;
}
)",
                    "return type mismatch: expected bool, found i64");

  expect_diagnostic(R"(
module bad;
theorem dup for ()
{
  return true;
}

fn dup() -> bool
{
  return true;
}
)",
                    "duplicate top-level declaration 'dup'");

  expect_diagnostic(R"(
module bad;
theorem self for (x: i64)
{
  return self(x);
}
)",
                    "recursive theorem calls are not supported yet");

  expect_diagnostic(R"(
module bad;
fn add_one(x: i64) -> i64
{
  return x + 1;
}

fn caller(x: i64) -> i64
{
  return add_one(x, 1);
}
)",
                    "function 'add_one' expects 1 argument(s), got 2");

  expect_diagnostic(R"(
module bad;
fn add_one(x: i64) -> i64
{
  return x + 1;
}

fn caller(flag: bool) -> i64
{
  return add_one(flag);
}
)",
                    "argument 1 for function 'add_one' type mismatch");

  expect_diagnostic(R"(
module bad;
fn observe(x: i64) -> void
{
  return;
}

fn caller(x: i64) -> i64
{
  return observe(x);
}
)",
                    "function 'observe' returns void and cannot be used as a value");

  expect_diagnostic(R"(
module bad;
fn recursive(x: i64) -> i64
{
  return recursive(x);
}
)",
                    "recursive function calls are not supported yet");

  expect_diagnostic(R"(
module bad;
fn first(x: i64) -> i64
{
  return second(x);
}

fn second(x: i64) -> i64
{
  return first(x);
}
)",
                    "recursive function calls are not supported yet");

  expect_diagnostic(R"(
module bad;
fn assign_parameter(x: i64) -> i64
{
  x = x + 1;
  return x;
}
)",
                    "assignment target 'x' is not a mutable local");

  expect_diagnostic(R"(
module bad;
fn assign_missing(x: i64) -> i64
{
  y = x + 1;
  return x;
}
)",
                    "assignment target 'y' is not declared");

  expect_diagnostic(R"(
module bad;
fn assign_wrong_type(x: i64) -> i64
{
  let y: i64 = x;
  y = false;
  return y;
}
)",
                    "assignment type mismatch");

  expect_diagnostic(R"(
module bad;
fn bad_if_condition(x: i64) -> i64
{
  let y: i64 = if x { 1 } else { 0 };
  return y;
}
)",
                    "if condition must be bool");

  expect_diagnostic(R"(
module bad;
fn bad_if_statement_condition(x: i64) -> i64
{
  if x {
    return x;
  } else {
    return 0;
  }
}
)",
                    "if statement condition must be bool");

  expect_diagnostic(R"(
module bad;
fn bad_if_branches(flag: bool) -> i64
{
  let y: i64 = if flag { 1 } else { false };
  return y;
}
)",
                    "if branches must have the same type");

  expect_diagnostic(R"(
module bad;
fn missing_return(x: i64) -> i64
{
  let y: i64 = x + 1;
}
)",
                    "function 'missing_return' must return a value on every path");

  expect_diagnostic(R"(
module bad;
fn partial_return(flag: bool, x: i64) -> i64
{
  if flag {
    return x;
  } else {
    assume keep_going: true;
  }
}
)",
                    "function 'partial_return' must return a value on every path");

  expect_diagnostic(R"(
module bad;
fn unreachable_after_return(x: i64) -> i64
{
  return x;
  assert never: true;
}
)",
                    "unreachable statement after guaranteed return");

  expect_diagnostic(R"(
module bad;
fn unreachable_after_void_return() -> void
{
  return;
  assume never: true;
}
)",
                    "unreachable statement after guaranteed return");

  expect_diagnostic(R"(
module bad;
fn unreachable_after_if(flag: bool, x: i64) -> i64
{
  if flag {
    return x;
  } else {
    return 0;
  }
  assume never: true;
}
)",
                    "unreachable statement after guaranteed return");

  expect_diagnostic(R"(
module bad;
fn unreachable_inside_branch(flag: bool, x: i64) -> i64
{
  if flag {
    return x;
    assert never: true;
  } else {
    return 0;
  }
}
)",
                    "unreachable statement after guaranteed return");

  expect_diagnostic(R"(
module bad;
fn duplicate_local(x: i64) -> i64
{
  let x: i64 = 1;
  return x;
}
)",
                    "duplicate local 'x'");

  expect_diagnostic(R"(
module bad;
fn duplicate_proof_label(x: i64) -> i64
{
  assert repeated: x == x;
  assume repeated: true;
  return x;
}
)",
                    "duplicate proof label 'repeated'");

  expect_diagnostic(R"(
module bad;
fn duplicate_branch_proof_label(flag: bool, x: i64) -> i64
{
  if flag {
    assert repeated: x == x;
  } else {
    assert repeated: x >= x;
  }
  return x;
}
)",
                    "duplicate proof label 'repeated'");

  expect_diagnostic(R"(
module bad;
fn duplicate_contract_label(x: i64) -> i64
requires stable: x >= 0;
ensures stable: result >= 0;
{
  return x;
}
)",
                    "duplicate contract label 'stable'");

  expect_diagnostic(R"(
module bad;
fn duplicate_contract_body_label(x: i64) -> i64
ensures stable: result >= 0;
{
  assert stable: x >= 0;
  return x;
}
)",
                    "duplicate proof label 'stable'");

  expect_diagnostic(R"(
module bad;
fn reserved_result_parameter(result: i64) -> i64
{
  return result;
}
)",
                    "parameter 'reserved_result_parameter.result' cannot use reserved name "
                    "'result'");

  expect_diagnostic(R"(
module bad;
fn reserved_result_local(x: i64) -> i64
{
  let result: i64 = x;
  return result;
}
)",
                    "local 'reserved_result_local.result' cannot use reserved name 'result'");

  expect_diagnostic(R"(
module bad;
fn reserved_type_parameter(i64: i64) -> i64
{
  return i64;
}
)",
                    "parameter 'reserved_type_parameter.i64' cannot use reserved type name 'i64'");

  expect_diagnostic(R"(
module bad;
fn reserved_type_local(x: i64) -> i64
{
  let bool: i64 = x;
  return bool;
}
)",
                    "local 'reserved_type_local.bool' cannot use reserved type name 'bool'");

  expect_diagnostic(R"(
module bad;
struct ReservedResult {
  result: i64;
}
)",
                    "field 'ReservedResult.result' cannot use reserved name 'result'");

  expect_diagnostic(R"(
module bad;
struct ReservedType {
  void: i64;
}
)",
                    "field 'ReservedType.void' cannot use reserved type name 'void'");

  expect_diagnostic(R"(
module bad;
fn bad_while_condition(x: i64) -> i64
{
  let i: i64 = 0;
  while i
  invariant lower: i >= 0;
  {
    i = i + 1;
  }
  return i;
}
)",
                    "while condition must be bool");

  expect_diagnostic(R"(
module bad;
fn bad_loop_invariant(x: i64) -> i64
{
  let i: i64 = 0;
  while i < x
  invariant not_bool: i + 1;
  {
    i = i + 1;
  }
  return i;
}
)",
                    "loop invariant 'not_bool' must be bool");

  expect_diagnostic(R"(
module bad;
fn duplicate_loop_invariant(x: i64) -> i64
{
  let i: i64 = 0;
  while i < x
  invariant bound: i >= 0;
  invariant bound: i <= x;
  {
    i = i + 1;
  }
  return i;
}
)",
                    "duplicate loop invariant 'bound'");

  expect_diagnostic(R"(
module bad;
fn duplicate_invariant_proof_label(n: i64) -> i64
{
  let i: i64 = 0;
  assert bound: i >= 0;
  while i < n
  invariant bound: i >= 0;
  {
    i = i + 1;
  }
  return i;
}
)",
                    "duplicate proof label 'bound'");

  expect_diagnostic(R"(
module bad;
fn return_inside_while(n: i64) -> i64
{
  let i: i64 = 0;
  while i < n
  invariant lower: i >= 0;
  {
    return i;
  }
  return i;
}
)",
                    "while bodies cannot contain return statements yet", 9, 5,
                    "typecheck.sigil:9:5-13");

  expect_diagnostic(R"(
module bad;
fn nested_return_inside_while(n: i64, stop: bool) -> i64
{
  let i: i64 = 0;
  while i < n
  invariant lower: i >= 0;
  {
    if stop {
      return i;
    } else {
      i = i + 1;
    }
  }
  return i;
}
)",
                    "while bodies cannot contain return statements yet");

  expect_diagnostic(R"(
module bad;
fn branch_local_does_not_escape(x: i64) -> i64
{
  if x >= 0 {
    let y: i64 = x;
  } else {
    let y: i64 = 0;
  }
  return y;
}
)",
                    "unknown identifier 'y'");

  expect_diagnostic(R"(
module bad;
fn ensure_leaks_local(x: i64) -> i64
ensures leaked: tmp >= 0;
{
  let tmp: i64 = x;
  return tmp;
}
)",
                    "unknown identifier 'tmp'");

  expect_diagnostic(R"(
module bad;
struct Bad {
  enabled: bool;
  invariant wrong: enabled >= 0;
}
)",
                    "comparison operator requires i64 operands");

  expect_diagnostic(R"(
module bad;
struct i64 {
  value: i64;
}
)",
                    "struct 'i64' cannot use reserved type name 'i64'");

  expect_diagnostic(R"(
module bad;
fn bool(x: i64) -> i64
{
  return x;
}
)",
                    "function 'bool' cannot use reserved type name 'bool'");

  expect_diagnostic(R"(
module bad;
struct Thing {
  value: i64;
}

fn Thing(x: i64) -> i64
{
  return x;
}
)",
                    "duplicate top-level declaration 'Thing'");

  expect(true, "typecheck tests completed");
  return 0;
}

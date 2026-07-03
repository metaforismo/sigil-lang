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
)";

  sigil::validate_module(sigil::parse_source(valid, "valid.sigil"));

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
fn caller(x: i64) -> i64
{
  return missing_call(x);
}
)",
                    "unknown function 'missing_call'");

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

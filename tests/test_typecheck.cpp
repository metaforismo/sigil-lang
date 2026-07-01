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
fn bad_let(x: i64) -> i64
{
  let flag: bool = x + 1;
  return x;
}
)",
                    "let type mismatch");

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
fn duplicate_local(x: i64) -> i64
{
  let x: i64 = 1;
  return x;
}
)",
                    "duplicate local 'x'");

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

  expect(true, "typecheck tests completed");
  return 0;
}

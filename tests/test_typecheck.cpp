#include "sigil/parser.hpp"
#include "sigil/typecheck.hpp"

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

void expect_diagnostic(const char* source, const std::string& needle) {
  try {
    const auto module = sigil::parse_source(source, "typecheck.sigil");
    sigil::validate_module(module);
  } catch (const sigil::Diagnostic& diagnostic) {
    const std::string message = diagnostic.what();
    if (message.find(needle) != std::string::npos) {
      return;
    }
    std::cerr << "FAIL: diagnostic did not contain '" << needle << "': " << message << "\n";
    std::exit(1);
  }

  std::cerr << "FAIL: expected diagnostic containing '" << needle << "'\n";
  std::exit(1);
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
  assert visible: x >= 0;
  assert y_visible: y >= x;
  return y;
}
)";

  sigil::validate_module(sigil::parse_source(valid, "valid.sigil"));

  expect_diagnostic(R"(
module bad;
fn missing(x: i64) -> i64
requires nope: y >= 0;
{
  return x;
}
)",
                    "unknown identifier 'y'");

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
fn duplicate_local(x: i64) -> i64
{
  let x: i64 = 1;
  return x;
}
)",
                    "duplicate local 'x'");

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

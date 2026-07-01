#include "sigil/gccjit_backend.hpp"
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

} // namespace

int main() {
  const char* source = R"(
module native;

fn add_one(x: i64) -> i64
{
  let y: i64 = x;
  y = y + 1;
  return y;
}

fn choose(flag: bool, x: i64) -> i64
{
  if flag {
    return x;
  } else {
    return 0;
  }
}

fn nonzero(x: i64) -> bool
{
  return x != 0;
}
)";

  const auto module = sigil::parse_source(source, "backend.sigil");
  sigil::validate_module(module);
  const auto result = sigil::compile_module_with_gccjit(module);

#if SIGIL_HAVE_GCCJIT
  expect(result.available, "gccjit compile result is available");
  expect(result.compiled, "gccjit compile result compiled");
  expect(result.functions.size() == 3, "three function reports");
  expect(result.functions[0].name == "add_one", "first function name");
  expect(result.functions[0].lowered, "add_one lowered");
  expect(result.functions[1].name == "choose", "second function name");
  expect(result.functions[1].lowered, "choose lowered");
  expect(result.functions[2].name == "nonzero", "third function name");
  expect(result.functions[2].lowered, "nonzero lowered");
#else
  expect(!result.available, "gccjit compile result unavailable without backend");
  expect(!result.compiled, "gccjit compile result not compiled without backend");
#endif

  const char* unsupported_source = R"(
module native;

fn quotient(x: i64, y: i64) -> i64
{
  return x / y;
}
)";

  const auto unsupported_module = sigil::parse_source(unsupported_source, "unsupported.sigil");
  sigil::validate_module(unsupported_module);
  const auto unsupported_result = sigil::compile_module_with_gccjit(unsupported_module);
#if SIGIL_HAVE_GCCJIT
  expect(unsupported_result.available, "unsupported result sees backend");
  expect(!unsupported_result.compiled, "unsupported function is not compiled");
  expect(unsupported_result.functions.size() == 1, "unsupported function report count");
  expect(!unsupported_result.functions[0].lowered, "division function skipped");
  expect(unsupported_result.functions[0].detail.find("division") != std::string::npos,
         "division skip explains semantic gap");
#else
  expect(!unsupported_result.available, "unsupported unavailable without backend");
#endif

  return 0;
}

#include "sigil/gccjit_backend.hpp"
#include "sigil/parser.hpp"
#include "sigil/typecheck.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

  const auto add_one =
      sigil::invoke_function_with_gccjit(module, "add_one", {sigil::gccjit_i64(41)});
  expect(add_one.available, "add_one invocation sees backend");
  expect(add_one.compiled, "add_one invocation compiled");
  expect(add_one.invoked, "add_one invoked");
  expect(add_one.value.kind == sigil::TypeKind::I64, "add_one result kind");
  expect(add_one.value.integer == 42, "add_one ABI result");

  const auto choose_true = sigil::invoke_function_with_gccjit(
      module, "choose", {sigil::gccjit_bool(true), sigil::gccjit_i64(9)});
  expect(choose_true.invoked, "choose true invoked");
  expect(choose_true.value.integer == 9, "choose true ABI result");

  const auto choose_false = sigil::invoke_function_with_gccjit(
      module, "choose", {sigil::gccjit_bool(false), sigil::gccjit_i64(9)});
  expect(choose_false.invoked, "choose false invoked");
  expect(choose_false.value.integer == 0, "choose false ABI result");

  const auto nonzero_false =
      sigil::invoke_function_with_gccjit(module, "nonzero", {sigil::gccjit_i64(0)});
  expect(nonzero_false.invoked, "nonzero false invoked");
  expect(nonzero_false.value.kind == sigil::TypeKind::Bool, "nonzero result kind");
  expect(!nonzero_false.value.boolean, "nonzero false ABI result");

  const auto nonzero_true =
      sigil::invoke_function_with_gccjit(module, "nonzero", {sigil::gccjit_i64(5)});
  expect(nonzero_true.invoked, "nonzero true invoked");
  expect(nonzero_true.value.boolean, "nonzero true ABI result");

  const auto wrong_argument =
      sigil::invoke_function_with_gccjit(module, "add_one", {sigil::gccjit_bool(true)});
  expect(!wrong_argument.invoked, "wrong argument not invoked");
  expect(wrong_argument.detail.find("must be i64") != std::string::npos,
         "wrong argument explains expected type");
#else
  expect(!result.available, "gccjit compile result unavailable without backend");
  expect(!result.compiled, "gccjit compile result not compiled without backend");
  const auto invocation =
      sigil::invoke_function_with_gccjit(module, "add_one", {sigil::gccjit_i64(41)});
  expect(!invocation.available, "gccjit invocation unavailable without backend");
  expect(!invocation.invoked, "gccjit invocation not invoked without backend");
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
  expect(unsupported_result.functions[0].range.display() == "unsupported.sigil:6:10-14",
         "division skip points to expression range");

  const auto unsupported_invocation = sigil::invoke_function_with_gccjit(
      unsupported_module, "quotient", {sigil::gccjit_i64(4), sigil::gccjit_i64(2)});
  expect(!unsupported_invocation.invoked, "unsupported invocation not invoked");
  expect(unsupported_invocation.range.display() == "unsupported.sigil:6:10-14",
         "unsupported invocation points to expression range");
#else
  expect(!unsupported_result.available, "unsupported unavailable without backend");
#endif

  return 0;
}

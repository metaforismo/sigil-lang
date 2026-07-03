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

fn sum3(a: i64, b: i64, c: i64) -> i64
{
  let total: i64 = a + b;
  total = total + c;
  return total;
}

fn choose3(first: bool, second: bool, x: i64) -> i64
{
  if first {
    return x;
  } else {
    if second {
      return x + 1;
    } else {
      return 0;
    }
  }
}

fn sum4(a: i64, b: i64, c: i64, d: i64) -> i64
{
  let total: i64 = a + b;
  total = total + c;
  total = total + d;
  return total;
}

fn sum8(a: i64, b: i64, c: i64, d: i64, e: i64, f: i64, g: i64, h: i64) -> i64
{
  let total: i64 = a + b;
  total = total + c;
  total = total + d;
  total = total + e;
  total = total + f;
  total = total + g;
  total = total + h;
  return total;
}

fn all4(a: bool, b: bool, c: bool, d: bool) -> bool
{
  return a && b && c && d;
}

fn all8(a: bool, b: bool, c: bool, d: bool, e: bool, f: bool, g: bool, h: bool) -> bool
{
  return a && b && c && d && e && f && g && h;
}

fn observe(flag: bool, x: i64) -> void
{
  if flag {
    return;
  } else {
    assume keep_going: true;
  }
}

fn call_add_one(x: i64) -> i64
{
  return add_one(x);
}

fn call_twice(x: i64) -> i64
{
  let once: i64 = add_one(x);
  return add_one(once);
}
)";

  const auto module = sigil::parse_source(source, "backend.sigil");
  sigil::validate_module(module);
  const auto result = sigil::compile_module_with_gccjit(module);
  const auto artifacts = sigil::build_native_ir_artifacts(module, result);
  const auto binary_artifacts = sigil::build_binary_proof_artifacts(module, result);
  expect(artifacts.size() == 12, "native artifact count");
  expect(binary_artifacts.size() == 12, "binary proof artifact count");
  expect(artifacts[0].file_name == "fn.add_one.native-ir.txt", "native artifact file name");
  expect(binary_artifacts[0].file_name == "fn.add_one.binary-facts.txt",
         "binary proof artifact file name");
  expect(artifacts[0].text.find("signature add_one(x: i64) -> i64") != std::string::npos,
         "native artifact signature");
  expect(binary_artifacts[0].text.find("sigil-binary-proof-facts v0") != std::string::npos,
         "binary proof artifact marker");
  expect(binary_artifacts[0].text.find("native-ir-file fn.add_one.native-ir.txt") !=
             std::string::npos,
         "binary proof artifact links native IR file");
  expect(binary_artifacts[0].text.find("cycle-bound-proven no") != std::string::npos,
         "binary proof artifact does not claim cycle proof");
  expect(binary_artifacts[0].text.find("crash-safety-proven no") != std::string::npos,
         "binary proof artifact does not claim crash proof");
  expect(binary_artifacts[0].text.find("experiment-contract") != std::string::npos,
         "binary proof artifact includes experiment contract");
  expect(artifacts[0].text.find("assign y") != std::string::npos, "native artifact assignment");
  expect(artifacts[0].text.find("debug-locations") != std::string::npos,
         "native artifact debug location section");
  expect(artifacts[0].text.find("param.x backend.sigil:") != std::string::npos,
         "native artifact parameter debug location");
  expect(artifacts[0].text.find("statement.assign.y backend.sigil:") != std::string::npos,
         "native artifact assignment debug location");
  expect(artifacts[0].text.find("expr.assign.y.value.rhs backend.sigil:") != std::string::npos,
         "native artifact nested expression debug location");
  expect(artifacts[1].text.find("if @") != std::string::npos, "native artifact if statement");
  expect(artifacts[3].text.find("signature sum3(a: i64, b: i64, c: i64) -> i64") !=
             std::string::npos,
         "native artifact three-parameter signature");
  expect(artifacts[5].text.find("signature sum4(a: i64, b: i64, c: i64, d: i64) -> i64") !=
             std::string::npos,
         "native artifact four-parameter signature");
  expect(artifacts[6].text.find("signature sum8(a: i64, b: i64, c: i64, d: i64, "
                                "e: i64, f: i64, g: i64, h: i64) -> i64") != std::string::npos,
         "native artifact eight-parameter i64 signature");
  expect(artifacts[8].text.find("signature all8(a: bool, b: bool, c: bool, d: bool, "
                                "e: bool, f: bool, g: bool, h: bool) -> bool") != std::string::npos,
         "native artifact eight-parameter bool signature");
  expect(artifacts[9].text.find("signature observe(flag: bool, x: i64) -> void") !=
             std::string::npos,
         "native artifact void signature");
  expect(artifacts[10].text.find("signature call_add_one(x: i64) -> i64") != std::string::npos,
         "native artifact call signature");
  expect(artifacts[10].text.find("value @backend.sigil:86:10-19: add_one(x)") != std::string::npos,
         "native artifact call expression");
  expect(artifacts[10].text.find("expr.return.value.arg0 backend.sigil:86:18") != std::string::npos,
         "native artifact call argument debug location");

#if SIGIL_HAVE_GCCJIT
  expect(result.available, "gccjit compile result is available");
  expect(result.compiled, "gccjit compile result compiled");
  expect(result.debug_info_enabled, "gccjit debug info enabled");
  expect(binary_artifacts[0].text.find("native-status lowered") != std::string::npos,
         "binary proof artifact lowered status");
  expect(binary_artifacts[0].text.find("candidate yes") != std::string::npos,
         "binary proof artifact candidate when lowered");
  expect(artifacts[0].text.find("debug-info enabled") != std::string::npos,
         "native artifact records enabled debug info");
  expect(result.functions.size() == 12, "twelve function reports");
  expect(result.functions[0].name == "add_one", "first function name");
  expect(result.functions[0].lowered, "add_one lowered");
  expect(result.functions[1].name == "choose", "second function name");
  expect(result.functions[1].lowered, "choose lowered");
  expect(result.functions[2].name == "nonzero", "third function name");
  expect(result.functions[2].lowered, "nonzero lowered");
  expect(result.functions[3].name == "sum3", "fourth function name");
  expect(result.functions[3].lowered, "sum3 lowered");
  expect(result.functions[4].name == "choose3", "fifth function name");
  expect(result.functions[4].lowered, "choose3 lowered");
  expect(result.functions[5].name == "sum4", "sixth function name");
  expect(result.functions[5].lowered, "sum4 lowered");
  expect(result.functions[6].name == "sum8", "seventh function name");
  expect(result.functions[6].lowered, "sum8 lowered");
  expect(result.functions[7].name == "all4", "eighth function name");
  expect(result.functions[7].lowered, "all4 lowered");
  expect(result.functions[8].name == "all8", "ninth function name");
  expect(result.functions[8].lowered, "all8 lowered");
  expect(result.functions[9].name == "observe", "tenth function name");
  expect(result.functions[9].lowered, "observe lowered");
  expect(result.functions[10].name == "call_add_one", "eleventh function name");
  expect(result.functions[10].lowered, "call_add_one lowered");
  expect(result.functions[11].name == "call_twice", "twelfth function name");
  expect(result.functions[11].lowered, "call_twice lowered");

  const auto add_one =
      sigil::invoke_function_with_gccjit(module, "add_one", {sigil::gccjit_i64(41)});
  expect(add_one.available, "add_one invocation sees backend");
  expect(add_one.compiled, "add_one invocation compiled");
  expect(add_one.invoked, "add_one invoked");
  expect(add_one.value.kind == sigil::TypeKind::I64, "add_one result kind");
  expect(add_one.value.integer == 42, "add_one ABI result");
  expect(add_one.range.display().find("backend.sigil:4:1-") == 0,
         "add_one invocation range points to function");

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

  const auto sum3 = sigil::invoke_function_with_gccjit(
      module, "sum3", {sigil::gccjit_i64(1), sigil::gccjit_i64(2), sigil::gccjit_i64(3)});
  expect(sum3.invoked, "sum3 invoked");
  expect(sum3.value.kind == sigil::TypeKind::I64, "sum3 result kind");
  expect(sum3.value.integer == 6, "sum3 ABI result");

  const auto choose3_first = sigil::invoke_function_with_gccjit(
      module, "choose3",
      {sigil::gccjit_bool(true), sigil::gccjit_bool(false), sigil::gccjit_i64(10)});
  expect(choose3_first.invoked, "choose3 first invoked");
  expect(choose3_first.value.integer == 10, "choose3 first ABI result");

  const auto choose3_second = sigil::invoke_function_with_gccjit(
      module, "choose3",
      {sigil::gccjit_bool(false), sigil::gccjit_bool(true), sigil::gccjit_i64(10)});
  expect(choose3_second.invoked, "choose3 second invoked");
  expect(choose3_second.value.integer == 11, "choose3 second ABI result");

  const auto sum4 = sigil::invoke_function_with_gccjit(
      module, "sum4",
      {sigil::gccjit_i64(1), sigil::gccjit_i64(2), sigil::gccjit_i64(3), sigil::gccjit_i64(4)});
  expect(sum4.invoked, "sum4 invoked");
  expect(sum4.value.kind == sigil::TypeKind::I64, "sum4 result kind");
  expect(sum4.value.integer == 10, "sum4 ABI result");

  const auto sum8 = sigil::invoke_function_with_gccjit(
      module, "sum8",
      {sigil::gccjit_i64(1), sigil::gccjit_i64(2), sigil::gccjit_i64(3), sigil::gccjit_i64(4),
       sigil::gccjit_i64(5), sigil::gccjit_i64(6), sigil::gccjit_i64(7), sigil::gccjit_i64(8)});
  expect(sum8.invoked, "sum8 invoked");
  expect(sum8.value.kind == sigil::TypeKind::I64, "sum8 result kind");
  expect(sum8.value.integer == 36, "sum8 ABI result");

  const auto all4_true =
      sigil::invoke_function_with_gccjit(module, "all4",
                                         {sigil::gccjit_bool(true), sigil::gccjit_bool(true),
                                          sigil::gccjit_bool(true), sigil::gccjit_bool(true)});
  expect(all4_true.invoked, "all4 true invoked");
  expect(all4_true.value.kind == sigil::TypeKind::Bool, "all4 result kind");
  expect(all4_true.value.boolean, "all4 true ABI result");

  const auto all4_false =
      sigil::invoke_function_with_gccjit(module, "all4",
                                         {sigil::gccjit_bool(true), sigil::gccjit_bool(true),
                                          sigil::gccjit_bool(false), sigil::gccjit_bool(true)});
  expect(all4_false.invoked, "all4 false invoked");
  expect(!all4_false.value.boolean, "all4 false ABI result");

  const auto all8_true = sigil::invoke_function_with_gccjit(
      module, "all8",
      {sigil::gccjit_bool(true), sigil::gccjit_bool(true), sigil::gccjit_bool(true),
       sigil::gccjit_bool(true), sigil::gccjit_bool(true), sigil::gccjit_bool(true),
       sigil::gccjit_bool(true), sigil::gccjit_bool(true)});
  expect(all8_true.invoked, "all8 true invoked");
  expect(all8_true.value.kind == sigil::TypeKind::Bool, "all8 result kind");
  expect(all8_true.value.boolean, "all8 true ABI result");

  const auto all8_false = sigil::invoke_function_with_gccjit(
      module, "all8",
      {sigil::gccjit_bool(true), sigil::gccjit_bool(true), sigil::gccjit_bool(true),
       sigil::gccjit_bool(true), sigil::gccjit_bool(false), sigil::gccjit_bool(true),
       sigil::gccjit_bool(true), sigil::gccjit_bool(true)});
  expect(all8_false.invoked, "all8 false invoked");
  expect(!all8_false.value.boolean, "all8 false ABI result");

  const auto observe_true = sigil::invoke_function_with_gccjit(
      module, "observe", {sigil::gccjit_bool(true), sigil::gccjit_i64(7)});
  expect(observe_true.invoked, "observe true invoked");
  expect(observe_true.value.kind == sigil::TypeKind::Void, "observe true result kind");

  const auto observe_false = sigil::invoke_function_with_gccjit(
      module, "observe", {sigil::gccjit_bool(false), sigil::gccjit_i64(7)});
  expect(observe_false.invoked, "observe false invoked");
  expect(observe_false.value.kind == sigil::TypeKind::Void, "observe false result kind");

  const auto call_add_one =
      sigil::invoke_function_with_gccjit(module, "call_add_one", {sigil::gccjit_i64(41)});
  expect(call_add_one.invoked, "call_add_one invoked");
  expect(call_add_one.value.kind == sigil::TypeKind::I64, "call_add_one result kind");
  expect(call_add_one.value.integer == 42, "call_add_one ABI result");

  const auto call_twice =
      sigil::invoke_function_with_gccjit(module, "call_twice", {sigil::gccjit_i64(40)});
  expect(call_twice.invoked, "call_twice invoked");
  expect(call_twice.value.kind == sigil::TypeKind::I64, "call_twice result kind");
  expect(call_twice.value.integer == 42, "call_twice ABI result");

  const auto wrong_argument =
      sigil::invoke_function_with_gccjit(module, "add_one", {sigil::gccjit_bool(true)});
  expect(!wrong_argument.invoked, "wrong argument not invoked");
  expect(wrong_argument.detail.find("must be i64") != std::string::npos,
         "wrong argument explains expected type");
#else
  expect(!result.available, "gccjit compile result unavailable without backend");
  expect(!result.compiled, "gccjit compile result not compiled without backend");
  expect(!result.debug_info_enabled, "gccjit debug info disabled without backend");
  expect(binary_artifacts[0].text.find("native-status skipped") != std::string::npos,
         "binary proof artifact skipped without backend");
  expect(binary_artifacts[0].text.find("candidate no") != std::string::npos,
         "binary proof artifact not candidate without backend");
  expect(artifacts[0].text.find("debug-info disabled") != std::string::npos,
         "native artifact records disabled debug info");
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
  const auto unsupported_artifacts =
      sigil::build_native_ir_artifacts(unsupported_module, unsupported_result);
  const auto unsupported_binary_artifacts =
      sigil::build_binary_proof_artifacts(unsupported_module, unsupported_result);
  expect(unsupported_artifacts.size() == 1, "unsupported native artifact count");
  expect(unsupported_binary_artifacts.size() == 1, "unsupported binary proof artifact count");
  expect(unsupported_artifacts[0].text.find("status skipped") != std::string::npos,
         "unsupported native artifact status");
  expect(unsupported_binary_artifacts[0].text.find("native-status skipped") != std::string::npos,
         "unsupported binary proof artifact status");
  expect(unsupported_binary_artifacts[0].text.find("candidate no") != std::string::npos,
         "unsupported binary proof artifact not a candidate");
  expect(unsupported_binary_artifacts[0].text.find("blocking-range unsupported.sigil:6:10-14") !=
             std::string::npos,
         "unsupported binary proof artifact blocking range");
  expect(unsupported_artifacts[0].text.find("diagnostic unsupported.sigil:6:10-14") !=
             std::string::npos,
         "unsupported native artifact diagnostic range");
  expect(unsupported_artifacts[0].text.find("value @unsupported.sigil:6:10-14: (x / y)") !=
             std::string::npos,
         "unsupported native artifact body expression");
  expect(unsupported_artifacts[0].text.find("debug-info enabled") != std::string::npos,
         "unsupported native artifact records debug info");
  expect(unsupported_artifacts[0].text.find("expr.return.value unsupported.sigil:6:10-14") !=
             std::string::npos,
         "unsupported native artifact records expression debug location");

  const auto unsupported_invocation = sigil::invoke_function_with_gccjit(
      unsupported_module, "quotient", {sigil::gccjit_i64(4), sigil::gccjit_i64(2)});
  expect(!unsupported_invocation.invoked, "unsupported invocation not invoked");
  expect(unsupported_invocation.range.display() == "unsupported.sigil:6:10-14",
         "unsupported invocation points to expression range");
#else
  expect(!unsupported_result.available, "unsupported unavailable without backend");
#endif

  const char* struct_native_source = R"(
module native;

struct Pair {
  left: i64;
  ok: bool;
}

fn read_left(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, ok: true };
  return pair.left;
}
)";

  const auto struct_native_module =
      sigil::parse_source(struct_native_source, "struct-native.sigil");
  sigil::validate_module(struct_native_module);
  const auto struct_native_result = sigil::compile_module_with_gccjit(struct_native_module);
#if SIGIL_HAVE_GCCJIT
  expect(struct_native_result.available, "struct native result sees backend");
  expect(!struct_native_result.compiled, "struct native function is not compiled");
  expect(struct_native_result.functions.size() == 1, "struct native function report count");
  expect(!struct_native_result.functions[0].lowered, "struct native function skipped");
  expect(struct_native_result.functions[0].detail.find("unsupported native type 'Pair'") !=
             std::string::npos,
         "struct native skip explains unsupported type");
  const auto struct_artifacts =
      sigil::build_native_ir_artifacts(struct_native_module, struct_native_result);
  expect(struct_artifacts[0].text.find("Pair { left: x, ok: true }") != std::string::npos,
         "struct native artifact displays literal");
  expect(struct_artifacts[0].text.find("pair.left") != std::string::npos,
         "struct native artifact displays field access");
#else
  expect(!struct_native_result.available, "struct native unavailable without backend");
#endif

  const char* loop_source = R"(
module native;

fn count_to(n: i64) -> i64
requires non_negative: n >= 0;
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

  const auto loop_module = sigil::parse_source(loop_source, "loop-native.sigil");
  sigil::validate_module(loop_module);
  const auto loop_result = sigil::compile_module_with_gccjit(loop_module);
#if SIGIL_HAVE_GCCJIT
  expect(loop_result.available, "loop result sees backend");
  expect(!loop_result.compiled, "loop function is not compiled");
  expect(loop_result.functions.size() == 1, "loop function report count");
  expect(!loop_result.functions[0].lowered, "loop function skipped");
  expect(loop_result.functions[0].detail.find("while loops") != std::string::npos,
         "loop skip explains native gap");
  const auto loop_artifacts = sigil::build_native_ir_artifacts(loop_module, loop_result);
  expect(loop_artifacts[0].text.find("while @loop-native.sigil:8:3-13:3") != std::string::npos,
         "loop native artifact body");
  expect(loop_artifacts[0].text.find("lower @loop-native.sigil:9:3-26") != std::string::npos,
         "loop native artifact invariant");
#else
  expect(!loop_result.available, "loop unavailable without backend");
#endif

  return 0;
}

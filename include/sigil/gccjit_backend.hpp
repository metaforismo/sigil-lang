#pragma once

#include "sigil/ast.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sigil {

struct GccJitStatus {
  bool available = false;
  std::string detail;
};

GccJitStatus gccjit_status();

struct GccJitFunctionReport {
  std::string name;
  bool lowered = false;
  std::string detail;
  SourceRange range;
};

struct GccJitCompileResult {
  bool available = false;
  bool compiled = false;
  std::string detail;
  std::vector<GccJitFunctionReport> functions;
  bool debug_info_enabled = false;
};

GccJitCompileResult compile_module_with_gccjit(const Module& module);

struct GccJitScalarValue {
  TypeKind kind = TypeKind::I64;
  std::int64_t integer = 0;
  bool boolean = false;
};

GccJitScalarValue gccjit_i64(std::int64_t value);
GccJitScalarValue gccjit_bool(bool value);
std::string display_gccjit_value(const GccJitScalarValue& value);

struct GccJitInvocationResult {
  bool available = false;
  bool compiled = false;
  bool invoked = false;
  std::string detail;
  SourceRange range;
  GccJitScalarValue value;
};

GccJitInvocationResult invoke_function_with_gccjit(const Module& module,
                                                   const std::string& function_name,
                                                   const std::vector<GccJitScalarValue>& arguments);

struct GccJitNativeArtifact {
  std::string function_name;
  std::string file_name;
  std::string text;
  SourceRange range;
};

std::string native_ir_file_name_for_function(const std::string& function_name);
std::vector<GccJitNativeArtifact> build_native_ir_artifacts(const Module& module,
                                                            const GccJitCompileResult& result);

} // namespace sigil

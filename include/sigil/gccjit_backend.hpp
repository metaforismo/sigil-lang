#pragma once

#include "sigil/ast.hpp"

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
};

struct GccJitCompileResult {
  bool available = false;
  bool compiled = false;
  std::string detail;
  std::vector<GccJitFunctionReport> functions;
};

GccJitCompileResult compile_module_with_gccjit(const Module& module);

} // namespace sigil

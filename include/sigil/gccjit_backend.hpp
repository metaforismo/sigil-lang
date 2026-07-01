#pragma once

#include "sigil/ast.hpp"

#include <string>

namespace sigil {

struct GccJitStatus {
  bool available = false;
  std::string detail;
};

GccJitStatus gccjit_status();

} // namespace sigil

#include "sigil/parser.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  const char* source = R"(
module cache;

struct CacheLine {
  key: i64;
  valid: bool;
  invariant valid_key: !valid || key >= 0;
}

fn keep(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  assert non_negative: x >= 0;
  return x;
}
)";

  const auto module = sigil::parse_source(source, "inline.sigil");
  expect(module.name == "cache", "module name");
  expect(module.structs.size() == 1, "struct count");
  expect(module.structs[0].fields.size() == 2, "field count");
  expect(module.structs[0].invariants.size() == 1, "invariant count");
  expect(module.functions.size() == 1, "function count");
  expect(module.functions[0].requires.size() == 1, "requires count");
  expect(module.functions[0].ensures.size() == 1, "ensures count");
  expect(module.functions[0].body.size() == 2, "body count");
  expect(sigil::display_expr(module.structs[0].invariants[0].expr) == "(!valid || (key >= 0))",
         "display invariant");
  return 0;
}

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

} // namespace

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
  let y: i64 = if x >= 0 { x + 1 } else { 1 };
  assert advanced: y >= x;
  assert non_negative: x >= 0;
  return y;
}
)";

  const auto module = sigil::parse_source(source, "inline.sigil");
  expect(module.name == "cache", "module name");
  expect(module.structs.size() == 1, "struct count");
  expect(module.structs[0].fields.size() == 2, "field count");
  expect(module.structs[0].invariants.size() == 1, "invariant count");
  expect(module.functions.size() == 1, "function count");
  expect(module.functions[0].preconditions.size() == 1, "requires count");
  expect(module.functions[0].ensures.size() == 1, "ensures count");
  expect(module.functions[0].body.size() == 4, "body count");
  expect(module.functions[0].body[0].kind == sigil::StatementKind::Let, "let statement kind");
  expect(module.functions[0].body[0].name == "y", "let binding name");
  expect(module.functions[0].body[0].type.kind == sigil::TypeKind::I64, "let binding type");
  expect(sigil::display_expr(module.functions[0].body[0].expr) ==
             "(if (x >= 0) { (x + 1) } else { 1 })",
         "display if expression");
  expect(sigil::display_expr(module.structs[0].invariants[0].expr) == "(!valid || (key >= 0))",
         "display invariant");
  return 0;
}

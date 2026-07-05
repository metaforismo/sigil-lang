#include "sigil/parser.hpp"

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

void expect_parse_diagnostic(const char* source, const std::string& needle,
                             std::size_t expected_line, std::size_t expected_column,
                             const std::string& expected_range = "") {
  try {
    (void)sigil::parse_source(source, "parse-error.sigil");
  } catch (const sigil::Diagnostic& diagnostic) {
    const std::string message = diagnostic.what();
    expect(message.find(needle) != std::string::npos, "parser diagnostic message");
    expect(diagnostic.location().line == expected_line, "parser diagnostic line");
    expect(diagnostic.location().column == expected_column, "parser diagnostic column");
    if (!expected_range.empty()) {
      expect(diagnostic.range().display() == expected_range, "parser diagnostic range");
    }
    return;
  }

  std::cerr << "FAIL: expected parser diagnostic containing '" << needle << "'\n";
  std::exit(1);
}

} // namespace

int main() {
  expect_parse_diagnostic("module broken;\nfn nope() -> i64 {\n  return 1\n}\n",
                          "expected ';' after return statement", 4, 1);
  expect_parse_diagnostic("module broken;\nfn nope() -> i64 {\n  return 1;\n",
                          "expected '}' after function body", 4, 1);
  expect_parse_diagnostic("module broken;\nstruct Missing {\n  value: i64;\n",
                          "expected '}' after struct body", 4, 1);
  expect_parse_diagnostic("module broken;\ncontainer Missing {\n  value: i64;\n",
                          "expected '}' after container body", 4, 1);
  expect_parse_diagnostic("module broken;\nwat\n",
                          "expected struct, container, theorem, or function declaration", 2, 1,
                          "parse-error.sigil:2:1-3");
  expect_parse_diagnostic(
      "module broken;\nfn nope(x: i64) -> i64 {\n  while x < 10 {\n  }\n  return x;\n}\n",
      "expected at least one invariant before while body", 3, 16);
  expect_parse_diagnostic("module broken;\nfn nope() -> i64 {\n  return 9223372036854775808;\n}\n",
                          "integer literal is out of range for i64", 3, 10,
                          "parse-error.sigil:3:10-28");

  const auto max_literal_module = sigil::parse_source(
      "module ok;\nfn max_literal() -> i64 { return 9223372036854775807; }\n", "max-literal.sigil");
  expect(max_literal_module.functions[0].body[0].expr->integer_value == 9223372036854775807LL,
         "max i64 literal parses exactly");

  const auto call_module = sigil::parse_source(
      R"(module calls;
fn call_examples(x: i64, y: i64) -> i64
{
  let next: i64 = add_one(x);
  return sum2(next, y);
}
)",
      "calls.sigil");
  expect(sigil::display_expr(call_module.functions[0].body[0].expr) == "add_one(x)",
         "display single-argument call");
  expect(sigil::display_expr(call_module.functions[0].body[1].expr) == "sum2(next, y)",
         "display multi-argument call");

  const auto theorem_module = sigil::parse_source(
      R"(module lemmas;
theorem add_one_gt for (x: i64)
requires non_negative: x >= 0;
ensures advanced: x + 1 > x;
{
  return x + 1 > x;
}
)",
      "lemmas.sigil");
  expect(theorem_module.functions.empty(), "theorem is not a runtime function");
  expect(theorem_module.theorems.size() == 1, "theorem count");
  expect(theorem_module.theorems[0].name == "add_one_gt", "theorem name");
  expect(theorem_module.theorems[0].params.size() == 1, "theorem param count");
  expect(theorem_module.theorems[0].preconditions.size() == 1, "theorem requires count");
  expect(theorem_module.theorems[0].ensures.size() == 1, "theorem ensures count");
  expect(theorem_module.theorems[0].body.size() == 1, "theorem body count");
  expect(theorem_module.theorems[0].body[0].kind == sigil::StatementKind::Return,
         "theorem return statement");
  expect(theorem_module.theorems[0].range.display() == "lemmas.sigil:2:1-7:1", "theorem range");

  const auto generic_module = sigil::parse_source(
      R"(module generics;
struct Box[T] {
  value: T;
}

fn unwrap(x: i64) -> i64
{
  let box: Box[i64] = Box[i64] { value: x };
  return box.value;
}
)",
      "generics.sigil");
  expect(generic_module.structs[0].name == "Box", "generic struct name");
  expect(generic_module.structs[0].type_params.size() == 1, "generic struct type param count");
  expect(generic_module.structs[0].type_params[0].name == "T", "generic struct type param name");
  expect(generic_module.structs[0].fields[0].type.display() == "T", "generic field type");
  expect(generic_module.functions[0].body[0].type.display() == "Box[i64]",
         "generic local type display");
  expect(generic_module.functions[0].body[0].expr->literal_type.display() == "Box[i64]",
         "generic struct literal type");
  expect(sigil::display_expr(generic_module.functions[0].body[0].expr) == "Box[i64] { value: x }",
         "display generic struct literal");

  const auto container_module = sigil::parse_source(
      R"(module containers;
container Window[T] {
  items: Slice[T];
  index: i64;

  invariant index_within_items: index < len(items);
}
)",
      "containers.sigil");
  expect(container_module.structs.size() == 1, "container stored with aggregate declarations");
  expect(container_module.structs[0].is_container, "container declaration flag");
  expect(container_module.structs[0].name == "Window", "container name");
  expect(container_module.structs[0].type_params.size() == 1, "generic container type param");
  expect(container_module.structs[0].fields[0].type.display() == "Slice[T]",
         "container model field type");
  expect(container_module.structs[0].invariants.size() == 1, "container invariant count");

  const auto struct_value_module = sigil::parse_source(
      R"(module structs;
struct Pair {
  left: i64;
  ok: bool;
}

fn read_left(x: i64) -> i64
{
  let pair: Pair = Pair { left: x, ok: true };
  return pair.left;
}
)",
      "structs.sigil");
  expect(sigil::display_expr(struct_value_module.functions[0].body[0].expr) ==
             "Pair { left: x, ok: true }",
         "display struct literal");
  expect(sigil::display_expr(struct_value_module.functions[0].body[1].expr) == "pair.left",
         "display field access");

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
  y = y + 1;
  assert advanced: y >= x;
  assert non_negative: x >= 0;
  return y;
}

fn choose(x: i64) -> i64
{
  if x >= 0 {
    return x;
  } else {
    return -x;
  }
}

fn count_to(n: i64) -> i64
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

fn observe(flag: bool) -> void
{
  if flag {
    return;
  } else {
    assume idle: true;
  }
}
)";

  const auto module = sigil::parse_source(source, "inline.sigil");
  expect(module.name == "cache", "module name");
  expect(module.structs.size() == 1, "struct count");
  expect(module.structs[0].fields.size() == 2, "field count");
  expect(module.structs[0].invariants.size() == 1, "invariant count");
  expect(module.functions.size() == 4, "function count");
  expect(module.functions[0].preconditions.size() == 1, "requires count");
  expect(module.functions[0].ensures.size() == 1, "ensures count");
  expect(module.functions[0].body.size() == 5, "body count");
  expect(module.functions[0].body[0].kind == sigil::StatementKind::Let, "let statement kind");
  expect(module.functions[0].body[0].name == "y", "let binding name");
  expect(module.functions[0].body[0].type.kind == sigil::TypeKind::I64, "let binding type");
  expect(module.functions[0].body[1].kind == sigil::StatementKind::Assign,
         "assignment statement kind");
  expect(module.functions[0].body[1].name == "y", "assignment target name");
  expect(module.functions[0].body[2].has_explicit_label, "assert label is explicit");
  expect(sigil::display_expr(module.functions[0].body[0].expr) ==
             "(if (x >= 0) { (x + 1) } else { 1 })",
         "display if expression");
  expect(module.functions[0].body[0].range.display() == "inline.sigil:14:3-46",
         "let statement range");
  expect(module.functions[0].body[0].expr->range.display() == "inline.sigil:14:16-45",
         "if expression range");
  expect(sigil::display_expr(module.structs[0].invariants[0].expr) == "(!valid || (key >= 0))",
         "display invariant");
  expect(module.functions[1].body.size() == 1, "if statement body count");
  expect(module.functions[1].body[0].kind == sigil::StatementKind::If, "if statement kind");
  expect(sigil::display_expr(module.functions[1].body[0].expr) == "(x >= 0)",
         "if statement condition");
  expect(module.functions[1].body[0].then_branch.size() == 1, "then branch count");
  expect(module.functions[1].body[0].else_branch.size() == 1, "else branch count");
  expect(module.functions[1].body[0].range.display() == "inline.sigil:23:3-27:3",
         "if statement range");
  expect(module.functions[2].body.size() == 3, "loop function body count");
  expect(module.functions[2].body[1].kind == sigil::StatementKind::While, "while statement kind");
  expect(module.functions[2].body[1].loop_invariants.size() == 2, "loop invariant count");
  expect(module.functions[2].body[1].loop_invariants[0].name == "lower", "loop invariant name");
  expect(sigil::display_expr(module.functions[2].body[1].expr) == "(i < n)", "while condition");
  expect(module.functions[2].body[1].then_branch.size() == 1, "while body count");
  expect(module.functions[3].return_type.kind == sigil::TypeKind::Void, "void return type");
  expect(module.functions[3].body[0].kind == sigil::StatementKind::If, "void if statement");
  expect(!module.functions[3].body[0].then_branch[0].expr, "void return has no expression");
  expect(module.functions[3].body[0].then_branch[0].range.display() == "inline.sigil:45:5-11",
         "void return range");
  expect(module.functions[3].body[0].else_branch[0].has_explicit_label, "assume label is explicit");
  return 0;
}

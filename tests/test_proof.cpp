#include "sigil/parser.hpp"
#include "sigil/proof.hpp"

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
module arithmetic;

fn proof_seed(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = x + 1;
  assert y_defined: y == x + 1;
  assert still_non_negative: x >= 0;
  return y;
}
)";

  const auto module = sigil::parse_source(source, "proof.sigil");
  const auto obligations = sigil::build_obligations(module);
  expect(obligations.size() == 3, "two asserts plus ensure obligations");
  expect(obligations[0].name == "fn.proof_seed.assert.1.y_defined", "let assert obligation name");
  expect(obligations[0].location.line == 9, "assert obligation line");
  expect(obligations[0].location.column == 3, "assert obligation column");
  expect(obligations[0].range.display() == "proof.sigil:9:3-31", "assert obligation range");
  expect(obligations[2].location.line == 6, "ensures obligation line");
  expect(obligations[2].location.column == 1, "ensures obligation column");
  expect(obligations[2].range.display() == "proof.sigil:6:1-31", "ensures obligation range");

  const auto smt = sigil::emit_smt_lib(obligations[2]);
  expect(smt.find("(declare-const x Int)") != std::string::npos, "declares x");
  expect(smt.find("(declare-const y Int)") != std::string::npos, "declares y");
  expect(smt.find("(declare-const result Int)") != std::string::npos, "declares result");
  expect(smt.find("(assert (>= x 0))") != std::string::npos, "emits precondition");
  expect(smt.find("(assert (= y (+ x 1)))") != std::string::npos, "emits let equality");

  const auto results = sigil::verify_obligations(obligations, false);
  expect(results.size() == 3, "verification result count");
  expect(results[0].status == sigil::VerificationStatus::Proven, "let assert proven syntactically");
  expect(results[1].status == sigil::VerificationStatus::Proven, "precondition assert proven");
  expect(results[2].status == sigil::VerificationStatus::Unknown, "ensure needs SMT solver");
  expect(results[0].location.line == obligations[0].location.line, "result keeps source line");
  expect(results[0].location.column == obligations[0].location.column,
         "result keeps source column");
  expect(results[0].range.display() == obligations[0].range.display(), "result keeps source range");

  const char* conditional_source = R"(
module conditional;

fn abs_value(x: i64) -> i64
ensures non_negative: result >= 0;
{
  let y: i64 = if x >= 0 { x } else { -x };
  return y;
}
)";

  const auto conditional_module = sigil::parse_source(conditional_source, "conditional.sigil");
  const auto conditional_obligations = sigil::build_obligations(conditional_module);
  expect(conditional_obligations.size() == 1, "conditional ensure obligation");
  const auto conditional_smt = sigil::emit_smt_lib(conditional_obligations[0]);
  expect(conditional_smt.find("(assert (= y (ite (>= x 0) x (- x))))") != std::string::npos,
         "emits ite for if expression");
  const auto timeout_smt = sigil::emit_smt_lib(conditional_obligations[0], 250);
  expect(timeout_smt.find("(set-option :timeout 250)") != std::string::npos,
         "emits solver timeout");

  const char* branch_source = R"(
module branches;

fn branch_abs(x: i64) -> i64
ensures non_negative: result >= 0;
{
  if x >= 0 {
    assert then_guard: x >= 0;
    return x;
  } else {
    return -x;
  }
}
)";

  const auto branch_module = sigil::parse_source(branch_source, "branches.sigil");
  const auto branch_obligations = sigil::build_obligations(branch_module);
  expect(branch_obligations.size() == 2, "branch assert plus ensure obligations");
  expect(branch_obligations[0].name == "fn.branch_abs.assert.1.then_guard", "branch assert name");
  const auto branch_results = sigil::verify_obligations(branch_obligations, false);
  expect(branch_results[0].status == sigil::VerificationStatus::Proven,
         "then branch assertion proven by branch condition");
  const auto branch_smt = sigil::emit_smt_lib(branch_obligations[1]);
  expect(branch_smt.find("(assert (or (not (>= x 0)) (= result x)))") != std::string::npos,
         "emits guarded then return fact");
  expect(branch_smt.find("(assert (or (>= x 0) (= result (- x))))") != std::string::npos,
         "emits guarded else return fact");

  const char* assignment_source = R"(
module assignment;

fn increment_once(x: i64) -> i64
requires non_negative: x >= 0;
ensures advanced: result > x;
{
  let y: i64 = x;
  y = y + 1;
  return y;
}

fn branch_mutation(flag: bool, x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = 0;
  if flag {
    y = x;
  } else {
    y = 0;
  }
  return y;
}
)";

  const auto assignment_module = sigil::parse_source(assignment_source, "assignment.sigil");
  const auto assignment_obligations = sigil::build_obligations(assignment_module);
  expect(assignment_obligations.size() == 2, "assignment examples produce two ensures");
  const auto assignment_smt = sigil::emit_smt_lib(assignment_obligations[0]);
  expect(assignment_smt.find("y_assign_") != std::string::npos,
         "assignment creates versioned symbol");
  expect(assignment_smt.find("(assert (= y_assign_") != std::string::npos,
         "assignment emits version equality");
  const auto branch_assignment_smt = sigil::emit_smt_lib(assignment_obligations[1]);
  expect(branch_assignment_smt.find("y_join_") != std::string::npos,
         "branch assignment creates join symbol");
  expect(branch_assignment_smt.find("(ite flag") != std::string::npos,
         "branch assignment join uses ite");
  return 0;
}

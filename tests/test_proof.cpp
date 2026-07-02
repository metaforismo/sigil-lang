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
  const auto counterexample = sigil::render_source_counterexample(obligations[2], R"(
(
  (define-fun x () Int
    0)
  (define-fun result () Int
    (- 1))
)
)");
  expect(counterexample.find("x: i64 = 0") != std::string::npos,
         "renders source parameter counterexample");
  expect(counterexample.find("result: i64 = -1") != std::string::npos,
         "renders source result counterexample");

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

  const char* division_source = R"(
module division;

fn safe_div(x: i64, y: i64) -> i64
requires nonzero: y != 0;
ensures exact: result == x / y;
{
  return x / y;
}

fn unsafe_div(x: i64, y: i64) -> i64
{
  return x / y;
}

fn guarded_div(flag: bool, x: i64, y: i64) -> i64
requires safe_when_used: !flag || y != 0;
{
  let q: i64 = if flag { x / y } else { 0 };
  return q;
}

fn guarded_and(x: i64, y: i64) -> i64
requires safe_rhs: y != 0 && x / y >= 0;
{
  return x;
}

fn guarded_or(x: i64, y: i64) -> i64
requires zero_or_safe: y == 0 || x / y >= 0;
{
  return x;
}
)";

  const auto division_module = sigil::parse_source(division_source, "division.sigil");
  const auto division_obligations = sigil::build_obligations(division_module);
  expect(division_obligations.size() == 7, "division emits safety and ensure obligations");
  expect(division_obligations[0].name == "fn.safe_div.safety.1.divisor_nonzero",
         "safe return division safety name");
  expect(division_obligations[1].name == "fn.safe_div.safety.2.divisor_nonzero",
         "safe ensure division safety name");
  expect(division_obligations[2].name == "fn.safe_div.ensures.1.exact",
         "safe division ensure name");
  expect(division_obligations[3].name == "fn.unsafe_div.safety.1.divisor_nonzero",
         "unsafe division safety name");
  expect(division_obligations[4].name == "fn.guarded_div.safety.1.divisor_nonzero",
         "guarded branch division safety name");
  expect(division_obligations[5].name == "fn.guarded_and.safety.1.divisor_nonzero",
         "and short-circuit division safety name");
  expect(division_obligations[6].name == "fn.guarded_or.safety.1.divisor_nonzero",
         "or short-circuit division safety name");
  expect(division_obligations[0].range.display() == "division.sigil:8:14",
         "return division safety points to divisor");
  const auto division_results = sigil::verify_obligations(division_obligations, false);
  expect(division_results[0].status == sigil::VerificationStatus::Proven,
         "precondition proves return divisor safety locally");
  expect(division_results[1].status == sigil::VerificationStatus::Proven,
         "precondition proves ensure divisor safety locally");
  expect(division_results[2].status == sigil::VerificationStatus::Proven,
         "return equality proves exact ensure locally");
  expect(division_results[3].status == sigil::VerificationStatus::Unknown,
         "unsafe divisor needs solver counterexample");
  expect(division_results[5].status == sigil::VerificationStatus::Proven,
         "&& lhs proves rhs divisor safety locally");
  const auto guarded_smt = sigil::emit_smt_lib(division_obligations[4]);
  expect(guarded_smt.find("(assert (or (not flag) (distinct y 0)))") != std::string::npos,
         "guarded safety keeps precondition");
  expect(guarded_smt.find("(assert flag)") != std::string::npos,
         "guarded safety assumes selected branch");
  const auto and_guarded_smt = sigil::emit_smt_lib(division_obligations[5]);
  expect(and_guarded_smt.find("(assert (distinct y 0))") != std::string::npos,
         "&& rhs safety assumes lhs");
  const auto or_guarded_smt = sigil::emit_smt_lib(division_obligations[6]);
  expect(or_guarded_smt.find("(assert (not (= y 0)))") != std::string::npos,
         "|| rhs safety assumes negated lhs");

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
  expect(branch_obligations.size() == 3, "branch assert plus return-path ensure obligations");
  expect(branch_obligations[0].name == "fn.branch_abs.assert.1.then_guard", "branch assert name");
  const auto branch_results = sigil::verify_obligations(branch_obligations, false);
  expect(branch_results[0].status == sigil::VerificationStatus::Proven,
         "then branch assertion proven by branch condition");
  const auto branch_smt = sigil::emit_smt_lib(branch_obligations[1]);
  expect(branch_obligations[1].name == "fn.branch_abs.return.1.ensures.1.non_negative",
         "then return ensure name");
  expect(branch_smt.find("(assert (>= x 0))") != std::string::npos,
         "then return path assumes branch condition");
  expect(branch_smt.find("(assert (= result x))") != std::string::npos,
         "then return path binds result");
  const auto branch_else_smt = sigil::emit_smt_lib(branch_obligations[2]);
  expect(branch_obligations[2].name == "fn.branch_abs.return.2.ensures.1.non_negative",
         "else return ensure name");
  expect(branch_else_smt.find("(assert (not (>= x 0)))") != std::string::npos,
         "else return path assumes negated branch condition");
  expect(branch_else_smt.find("(assert (= result (- x)))") != std::string::npos,
         "else return path binds result");

  const char* early_return_source = R"(
module early;

fn early_then(flag: bool) -> i64
ensures zero: result == 0;
{
  if flag {
    return 1;
  } else {
    assume keep_going: true;
  }
  return 0;
}
)";

  const auto early_return_module = sigil::parse_source(early_return_source, "early.sigil");
  const auto early_return_obligations = sigil::build_obligations(early_return_module);
  expect(early_return_obligations.size() == 2, "early return creates two return-path ensures");
  expect(early_return_obligations[0].name == "fn.early_then.return.1.ensures.1.zero",
         "early return path ensure name");
  const auto early_then_smt = sigil::emit_smt_lib(early_return_obligations[0]);
  expect(early_then_smt.find("(assert flag)") != std::string::npos,
         "early return path keeps then guard");
  expect(early_then_smt.find("(assert (= result 1))") != std::string::npos,
         "early return path keeps original return value");
  expect(early_then_smt.find("(assert (= result 0))") == std::string::npos,
         "early return path is not overwritten by later return");
  const auto early_after_smt = sigil::emit_smt_lib(early_return_obligations[1]);
  expect(early_after_smt.find("(assert (not flag))") != std::string::npos,
         "continuing return path keeps else guard");
  expect(early_after_smt.find("(assert (= result 0))") != std::string::npos,
         "continuing return path binds later return value");

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
  expect(assignment_obligations[1].name == "fn.branch_mutation.ensures.1.preserved",
         "single return path keeps stable ensure name");
  expect(branch_assignment_smt.find("y_join_") != std::string::npos,
         "branch assignment creates join symbol");
  expect(branch_assignment_smt.find("(ite flag") != std::string::npos,
         "branch assignment join uses ite");

  const char* loop_source = R"(
module loops;

fn count_to(n: i64) -> i64
requires non_negative: n >= 0;
ensures exact: result == n;
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

  const auto loop_module = sigil::parse_source(loop_source, "loops.sigil");
  const auto loop_obligations = sigil::build_obligations(loop_module);
  expect(loop_obligations.size() == 5, "loop invariants plus ensure obligations");
  expect(loop_obligations[0].name == "fn.count_to.loop.1.invariant.1.lower.initial",
         "loop initial invariant name");
  expect(loop_obligations[2].name == "fn.count_to.loop.1.invariant.1.lower.preserved",
         "loop preserved invariant name");
  expect(loop_obligations[4].name == "fn.count_to.ensures.1.exact", "loop ensure name");
  const auto loop_preserve_smt = sigil::emit_smt_lib(loop_obligations[3]);
  expect(loop_preserve_smt.find("i_loop_") != std::string::npos,
         "loop preservation uses loop head symbol");
  expect(loop_preserve_smt.find("i_assign_") != std::string::npos,
         "loop preservation uses assigned symbol");
  const auto loop_ensure_smt = sigil::emit_smt_lib(loop_obligations[4]);
  expect(loop_ensure_smt.find("i_loop_exit_") != std::string::npos,
         "loop ensure uses loop exit symbol");
  expect(loop_ensure_smt.find("(not (< i_loop_exit_") != std::string::npos,
         "loop ensure assumes exit condition");
  return 0;
}

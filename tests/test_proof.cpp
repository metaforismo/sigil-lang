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

}  // namespace

int main() {
  const char* source = R"(
module arithmetic;

fn proof_seed(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  assert still_non_negative: x >= 0;
  return x;
}
)";

  const auto module = sigil::parse_source(source, "proof.sigil");
  const auto obligations = sigil::build_obligations(module);
  expect(obligations.size() == 2, "assert plus ensure obligations");
  expect(obligations[0].name == "fn.proof_seed.assert.1.still_non_negative", "assert obligation name");

  const auto smt = sigil::emit_smt_lib(obligations[1]);
  expect(smt.find("(declare-const x Int)") != std::string::npos, "declares x");
  expect(smt.find("(declare-const result Int)") != std::string::npos, "declares result");
  expect(smt.find("(assert (>= x 0))") != std::string::npos, "emits precondition");

  const auto results = sigil::verify_obligations(obligations, false);
  expect(results.size() == 2, "verification result count");
  expect(results[0].status == sigil::VerificationStatus::Proven, "assert proven syntactically");
  expect(results[1].status == sigil::VerificationStatus::Unknown, "ensure needs SMT solver");
  return 0;
}

#pragma once

#include "sigil/ast.hpp"

#include <string>
#include <vector>

namespace sigil {

enum class VerificationStatus {
  Proven,
  Refuted,
  Unknown,
  Error,
};

struct ProofObligation {
  std::string name;
  std::vector<NamedPredicate> assumptions;
  NamedPredicate goal;
  SymbolTable symbols;
};

struct VerificationResult {
  std::string obligation_name;
  VerificationStatus status = VerificationStatus::Unknown;
  std::string details;
  std::string smt_lib;
};

std::vector<ProofObligation> build_obligations(const Module& module);
std::string emit_smt_lib(const ProofObligation& obligation);
std::vector<VerificationResult> verify_obligations(
    const std::vector<ProofObligation>& obligations,
    bool use_z3);

const char* status_name(VerificationStatus status);

}  // namespace sigil

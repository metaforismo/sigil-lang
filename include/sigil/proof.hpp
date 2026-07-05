#pragma once

#include "sigil/ast.hpp"

#include <string>
#include <utility>
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
  SourceLocation location;
  SourceRange range;
  std::vector<NamedPredicate> assumptions;
  NamedPredicate goal;
  SymbolTable symbols;
};

struct VerificationResult {
  VerificationResult() = default;

  VerificationResult(std::string obligation_name, VerificationStatus status, std::string details,
                     std::string smt_lib, SourceLocation source_location = {})
      : obligation_name(std::move(obligation_name)), location(source_location),
        range(SourceRange{source_location, source_location}), status(status),
        details(std::move(details)), smt_lib(std::move(smt_lib)) {}

  VerificationResult(std::string obligation_name, VerificationStatus status, std::string details,
                     std::string smt_lib, SourceRange range)
      : obligation_name(std::move(obligation_name)), location(range.start), range(std::move(range)),
        status(status), details(std::move(details)), smt_lib(std::move(smt_lib)) {}

  std::string obligation_name;
  SourceLocation location;
  SourceRange range;
  VerificationStatus status = VerificationStatus::Unknown;
  std::string details;
  std::string smt_lib;
  std::string counterexample;
  std::string model;
  std::string smt_path;
};

struct ProofHintArtifact {
  std::string file_name;
  std::string text;
};

struct AgentHandoffArtifact {
  std::string label;
  std::string file_name;
  std::string text;
};

struct ProofOptions {
  bool use_z3 = true;
  bool include_models = false;
  int solver_timeout_ms = 0;
  std::string smt_output_dir;
};

std::vector<ProofObligation> build_obligations(const Module& module);
std::string emit_smt_lib(const ProofObligation& obligation);
std::string emit_smt_lib(const ProofObligation& obligation, int solver_timeout_ms);
std::string smt_file_name_for_obligation(const std::string& obligation_name);
std::string proof_hint_file_name_for_obligation(const std::string& obligation_name);
std::string agent_request_file_name_for_obligation(const std::string& obligation_name);
std::string theorem_candidate_file_name_for_obligation(const std::string& obligation_name);
std::string render_source_counterexample(const ProofObligation& obligation,
                                         const std::string& z3_model);
std::vector<ProofHintArtifact>
build_proof_hint_artifacts(const std::vector<ProofObligation>& obligations,
                           const std::vector<VerificationResult>& results);
std::vector<AgentHandoffArtifact>
build_agent_handoff_artifacts(const std::vector<ProofObligation>& obligations,
                              const std::vector<VerificationResult>& results);
std::vector<VerificationResult> verify_obligations(const std::vector<ProofObligation>& obligations,
                                                   const ProofOptions& options);
std::vector<VerificationResult> verify_obligations(const std::vector<ProofObligation>& obligations,
                                                   bool use_z3);

const char* status_name(VerificationStatus status);

} // namespace sigil

#pragma once

#include "sigil/proof.hpp"

#include <string>

namespace sigil {

struct AgentRefinementOptions {
  std::string agent_command;
  std::string trace_output_dir;
  int max_attempts = 3;
  int agent_timeout_ms = 30000;
  ProofOptions proof;
};

struct AgentRefinementResult {
  std::string module_name;
  bool accepted = false;
  int attempts_used = 0;
  std::string trace_path;
};

AgentRefinementResult run_agent_refinement(const std::string& source_path,
                                           const AgentRefinementOptions& options);

} // namespace sigil

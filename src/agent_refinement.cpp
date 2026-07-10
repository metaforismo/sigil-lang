#include "sigil/agent_refinement.hpp"

#include "sigil/parser.hpp"
#include "sigil/typecheck.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sigil {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("could not open " + path.string());
  }
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
  auto temporary = path;
  temporary += ".tmp";
  std::ofstream file(temporary, std::ios::trunc);
  if (!file || !(file << text)) {
    throw std::runtime_error("could not write " + path.string());
  }
  file.close();
  if (!file) {
    throw std::runtime_error("could not flush " + path.string());
  }

  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("could not publish " + path.string() + ": " + error.message());
  }
}

std::vector<std::string> assume_surface(const std::vector<Statement>& statements) {
  std::vector<std::string> surface;
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Assume) {
      surface.push_back(statement.name + ":" + display_expr(statement.expr));
    }
    const auto nested = assume_surface(statement.then_branch);
    surface.insert(surface.end(), nested.begin(), nested.end());
    const auto alternatives = assume_surface(statement.else_branch);
    surface.insert(surface.end(), alternatives.begin(), alternatives.end());
  }
  return surface;
}

std::vector<std::string> proof_surface(const std::vector<Statement>& statements) {
  std::vector<std::string> surface;
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Assert) {
      surface.push_back("assert:" + statement.name + ":" + display_expr(statement.expr));
    }
    if (statement.kind == StatementKind::While) {
      for (const auto& invariant : statement.loop_invariants) {
        surface.push_back("invariant:" + invariant.name + ":" + display_expr(invariant.expr));
      }
    }
    const auto nested = proof_surface(statement.then_branch);
    surface.insert(surface.end(), nested.begin(), nested.end());
    const auto alternatives = proof_surface(statement.else_branch);
    surface.insert(surface.end(), alternatives.begin(), alternatives.end());
  }
  return surface;
}

bool preserves_proof_surface(const std::vector<Statement>& original,
                             const std::vector<Statement>& candidate) {
  const auto required = proof_surface(original);
  const auto proposed = proof_surface(candidate);
  auto cursor = proposed.begin();
  for (const auto& proof_step : required) {
    cursor = std::find(cursor, proposed.end(), proof_step);
    if (cursor == proposed.end()) {
      return false;
    }
    ++cursor;
  }
  return true;
}

std::vector<std::string> module_assume_surface(const Module& module) {
  std::vector<std::string> surface;
  for (const auto& function : module.functions) {
    const auto assumptions = assume_surface(function.body);
    surface.insert(surface.end(), assumptions.begin(), assumptions.end());
  }
  for (const auto& theorem : module.theorems) {
    const auto assumptions = assume_surface(theorem.body);
    surface.insert(surface.end(), assumptions.begin(), assumptions.end());
  }
  return surface;
}

bool same_predicates(const std::vector<NamedPredicate>& left,
                     const std::vector<NamedPredicate>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].name != right[index].name ||
        display_expr(left[index].expr) != display_expr(right[index].expr)) {
      return false;
    }
  }
  return true;
}

bool same_params(const std::vector<ParamDecl>& left, const std::vector<ParamDecl>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].name != right[index].name ||
        left[index].type.display() != right[index].type.display()) {
      return false;
    }
  }
  return true;
}

bool preserves_contracts(const Module& original, const Module& candidate) {
  if (original.name != candidate.name || original.functions.size() != candidate.functions.size() ||
      original.structs.size() != candidate.structs.size() ||
      candidate.theorems.size() < original.theorems.size() ||
      module_assume_surface(original) != module_assume_surface(candidate)) {
    return false;
  }
  for (std::size_t index = 0; index < original.functions.size(); ++index) {
    const auto& before = original.functions[index];
    const auto& after = candidate.functions[index];
    if (before.name != after.name || before.return_type.display() != after.return_type.display() ||
        !same_params(before.params, after.params) ||
        !same_predicates(before.preconditions, after.preconditions) ||
        !same_predicates(before.ensures, after.ensures) ||
        !preserves_proof_surface(before.body, after.body)) {
      return false;
    }
  }
  for (std::size_t index = 0; index < original.structs.size(); ++index) {
    const auto& before = original.structs[index];
    const auto& after = candidate.structs[index];
    if (before.name != after.name || before.is_container != after.is_container ||
        before.type_params.size() != after.type_params.size() ||
        before.fields.size() != after.fields.size() ||
        !same_predicates(before.invariants, after.invariants)) {
      return false;
    }
    for (std::size_t param = 0; param < before.type_params.size(); ++param) {
      if (before.type_params[param].name != after.type_params[param].name) {
        return false;
      }
    }
    for (std::size_t field = 0; field < before.fields.size(); ++field) {
      if (before.fields[field].name != after.fields[field].name ||
          before.fields[field].type.display() != after.fields[field].type.display()) {
        return false;
      }
    }
  }
  for (std::size_t index = 0; index < original.theorems.size(); ++index) {
    const auto& before = original.theorems[index];
    const auto& after = candidate.theorems[index];
    if (before.name != after.name || !same_params(before.params, after.params) ||
        !same_predicates(before.preconditions, after.preconditions) ||
        !same_predicates(before.ensures, after.ensures) ||
        !preserves_proof_surface(before.body, after.body)) {
      return false;
    }
  }
  return true;
}

bool proof_rejected(const std::vector<VerificationResult>& results) {
  return std::any_of(results.begin(), results.end(), [](const auto& result) {
    return result.status != VerificationStatus::Proven;
  });
}

struct ProcessResult {
  bool launched = false;
  bool timed_out = false;
  int exit_code = -1;
};

ProcessResult run_process(const std::string& executable, const std::vector<std::string>& arguments,
                          const std::filesystem::path& log_path, int timeout_ms) {
#if defined(_WIN32)
  (void)executable;
  (void)arguments;
  (void)log_path;
  (void)timeout_ms;
  return {};
#else
  const pid_t child = fork();
  if (child < 0) {
    return {};
  }
  if (child == 0) {
    setpgid(0, 0);
    const int output = open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) {
      _exit(126);
    }
    close(output);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execv(executable.c_str(), argv.data());
    _exit(127);
  }
  setpgid(child, child);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      kill(-child, SIGKILL);
      return {true, false, WIFEXITED(status) ? WEXITSTATUS(status) : 128};
    }
    if (waited < 0) {
      kill(-child, SIGKILL);
      return {true, false, 125};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const auto waited = waitpid(child, &status, WNOHANG);
  if (waited == child) {
    kill(-child, SIGKILL);
    return {true, false, WIFEXITED(status) ? WEXITSTATUS(status) : 128};
  }
  if (waited < 0) {
    kill(-child, SIGKILL);
    return {true, false, 125};
  }
  kill(-child, SIGKILL);
  waitpid(child, &status, 0);
  return {true, true, 124};
#endif
}

void write_proof_artifacts(const std::filesystem::path& output_dir, const std::string& label,
                           const std::vector<ProofObligation>& obligations,
                           const std::vector<VerificationResult>& results) {
  if (obligations.size() != results.size()) {
    throw std::logic_error("proof result count does not match obligation count");
  }
  const auto smt_dir = output_dir / (label + "-smt");
  std::filesystem::create_directories(smt_dir);
  std::ostringstream ledger;
  ledger << "sigil-agent-refinement-proof-v1\nrun " << label << "\nobligation-count "
         << obligations.size() << "\n";
  for (std::size_t index = 0; index < obligations.size(); ++index) {
    const auto smt_name = smt_file_name_for_obligation(obligations[index].name);
    write_text_file(smt_dir / smt_name, results[index].smt_lib);
    ledger << "obligation " << obligations[index].name << "\n  status "
           << status_name(results[index].status) << "\n  details " << results[index].details
           << "\n  smt " << label << "-smt/" << smt_name << "\n";
  }
  write_text_file(output_dir / (label + ".proof.txt"), ledger.str());
}

} // namespace

AgentRefinementResult run_agent_refinement(const std::string& source_path,
                                           const AgentRefinementOptions& options) {
  if (options.agent_command.empty() || options.trace_output_dir.empty() ||
      options.max_attempts <= 0 || options.agent_timeout_ms <= 0) {
    throw std::invalid_argument("invalid agent refinement options");
  }
  const auto output_dir = std::filesystem::path(options.trace_output_dir);
  std::filesystem::create_directories(output_dir);
  const auto original_source = read_text_file(source_path);
  const auto original = parse_source(original_source, source_path);
  validate_module(original);
  auto current = original;
  auto current_path = std::filesystem::path(source_path);
  auto obligations = build_obligations(current);
  auto results = verify_obligations(obligations, options.proof);
  write_proof_artifacts(output_dir, "initial", obligations, results);

  const auto trace_path = output_dir / "agent-refinement.trace.txt";
  std::ostringstream trace;
  trace << "sigil-agent-refinement-trace-v1\nmodule " << original.name << "\nsource-path "
        << source_path << "\nagent-command " << options.agent_command << "\nmax-attempts "
        << options.max_attempts << "\nagent-timeout-ms " << options.agent_timeout_ms
        << "\nsolver-timeout-ms " << options.proof.solver_timeout_ms << "\nsolver "
        << (options.proof.use_z3 ? "z3" : "local-only") << "\ninitial-proof initial.proof.txt\n";
  const auto persist_trace = [&] { write_text_file(trace_path, trace.str()); };
  persist_trace();
  int attempts_used = 0;
  bool accepted = !proof_rejected(results);

  for (int attempt = 1; attempt <= options.max_attempts && !accepted; ++attempt) {
    attempts_used = attempt;
    const auto handoffs = build_agent_handoff_artifacts(obligations, results);
    const auto request = std::find_if(handoffs.begin(), handoffs.end(), [](const auto& artifact) {
      return artifact.label == "agent-request";
    });
    if (request == handoffs.end()) {
      trace << "attempt " << attempt << " rejected no-request\n";
      persist_trace();
      break;
    }
    const auto stem = "attempt-" + std::to_string(attempt);
    const auto request_path = output_dir / (stem + ".request.txt");
    const auto candidate_path = output_dir / (stem + ".candidate.sigil");
    const auto log_path = output_dir / (stem + ".agent.log");
    write_text_file(request_path, request->text);
    std::filesystem::remove(candidate_path);
    trace << "attempt-input " << attempt << " " << current_path.string() << "\n"
          << "attempt-request " << attempt << " " << request_path.filename().string() << "\n"
          << "attempt-candidate " << attempt << " " << candidate_path.filename().string() << "\n"
          << "attempt-log " << attempt << " " << log_path.filename().string() << "\n";
    persist_trace();
    const auto process = run_process(options.agent_command,
                                     {current_path.string(), request_path.string(),
                                      candidate_path.string(), std::to_string(attempt)},
                                     log_path, options.agent_timeout_ms);
    if (!process.launched) {
      trace << "attempt " << attempt << " launch-failed\n";
      persist_trace();
      continue;
    }
    if (process.timed_out) {
      trace << "attempt " << attempt << " timed-out\n";
      persist_trace();
      continue;
    }
    if (process.exit_code != 0 || !std::filesystem::exists(candidate_path)) {
      trace << "attempt " << attempt << " rejected agent-exit-" << process.exit_code << "\n";
      persist_trace();
      continue;
    }
    try {
      auto candidate = parse_source(read_text_file(candidate_path), candidate_path.string());
      validate_module(candidate);
      if (!preserves_contracts(original, candidate)) {
        trace << "attempt " << attempt << " rejected contract-changed\n";
        persist_trace();
        continue;
      }
      auto candidate_obligations = build_obligations(candidate);
      auto candidate_results = verify_obligations(candidate_obligations, options.proof);
      write_proof_artifacts(output_dir, stem, candidate_obligations, candidate_results);
      trace << "attempt-proof " << attempt << " " << stem << ".proof.txt\n";
      if (proof_rejected(candidate_results)) {
        trace << "attempt " << attempt << " rejected proof-failure\n";
        persist_trace();
        current = std::move(candidate);
        current_path = candidate_path;
        obligations = std::move(candidate_obligations);
        results = std::move(candidate_results);
        continue;
      }
      trace << "attempt " << attempt << " accepted\n";
      persist_trace();
      accepted = true;
    } catch (const std::exception&) {
      trace << "attempt " << attempt << " rejected invalid-candidate\n";
      persist_trace();
    }
  }
  trace << "final-status " << (accepted ? "accepted" : "exhausted") << "\n";
  persist_trace();
  return {original.name, accepted, attempts_used, trace_path.string()};
}

} // namespace sigil

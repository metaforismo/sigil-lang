#include "sigil/proof.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sigil {

namespace {

std::string shell_quote(const std::filesystem::path& path) {
  std::string quoted = "'";
  for (const char c : path.string()) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

std::string shell_quote_string(const std::string& value) {
  return shell_quote(std::filesystem::path(value));
}

bool expressions_equal(const Expr& lhs, const Expr& rhs) {
  return display_expr(lhs) == display_expr(rhs);
}

std::string sanitize_symbol(const std::string& name) {
  std::string symbol = name;
  std::replace(symbol.begin(), symbol.end(), '.', '_');
  return symbol;
}

std::vector<std::string> z3_command_candidates() {
  std::vector<std::string> commands;
  if (const char* env = std::getenv("SIGIL_Z3")) {
    commands.emplace_back(env);
  }
  commands.emplace_back("z3");
  return commands;
}

std::string run_command(const std::string& command, int& exit_code) {
  std::array<char, 256> buffer{};
  std::string output;
  FILE* pipe = popen((command + " 2>&1").c_str(), "r");
  if (!pipe) {
    exit_code = -1;
    return "failed to launch command";
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  exit_code = pclose(pipe);
  return output;
}

bool command_was_missing(int exit_code, const std::string& output) {
  return exit_code != 0 && (output.find("command not found") != std::string::npos ||
                            output.find("not found") != std::string::npos);
}

std::string first_solver_token(const std::string& output) {
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      return line;
    }
  }
  return "";
}

std::string solver_tail(const std::string& output) {
  std::istringstream lines(output);
  std::string line;
  std::ostringstream tail;
  bool skipped_status = false;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!skipped_status && !line.empty()) {
      skipped_status = true;
      continue;
    }
    if (skipped_status) {
      tail << line << "\n";
    }
  }
  return tail.str();
}

std::filesystem::path temporary_smt_path(const std::string& obligation_name) {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("sigil-" + smt_file_name_for_obligation(obligation_name) + "-" + std::to_string(tick));
}

struct SolverRun {
  bool launched = false;
  std::string output;
};

SolverRun run_z3_query(const ProofObligation& obligation, const std::string& smt) {
  const auto temp_path = temporary_smt_path(obligation.name);
  {
    std::ofstream file(temp_path);
    if (!file) {
      throw std::runtime_error("could not create temporary SMT file");
    }
    file << smt;
  }

  for (const auto& command : z3_command_candidates()) {
    int code = 0;
    const auto output =
        run_command(shell_quote_string(command) + " -smt2 " + shell_quote(temp_path), code);
    if (command_was_missing(code, output)) {
      continue;
    }
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    return SolverRun{true, output};
  }

  std::error_code ignored;
  std::filesystem::remove(temp_path, ignored);
  return SolverRun{false, ""};
}

VerificationResult verify_with_z3(const ProofObligation& obligation, const std::string& smt,
                                  const ProofOptions& options) {
  SolverRun run;
  try {
    run = run_z3_query(obligation, smt);
  } catch (const std::exception& error) {
    return {obligation.name, VerificationStatus::Error, error.what(), smt};
  }

  if (!run.launched) {
    return {obligation.name, VerificationStatus::Unknown,
            "z3 executable not found; syntactic checks only", smt};
  }

  const auto solver_token = first_solver_token(run.output);
  if (solver_token == "unsat") {
    return {obligation.name, VerificationStatus::Proven, "proved by z3", smt};
  }
  if (solver_token == "sat") {
    VerificationResult result{obligation.name, VerificationStatus::Refuted,
                              "z3 found a counterexample model violating the goal", smt};
    if (options.include_models) {
      try {
        const auto model_run = run_z3_query(obligation, smt + "(get-model)\n");
        if (model_run.launched && first_solver_token(model_run.output) == "sat") {
          result.model = solver_tail(model_run.output);
        }
      } catch (const std::exception& error) {
        result.details += std::string("; model query failed: ") + error.what();
      }
    }
    return result;
  }
  if (solver_token == "unknown") {
    return {obligation.name, VerificationStatus::Unknown, "z3 returned unknown", smt};
  }
  return {obligation.name, VerificationStatus::Unknown, "z3 returned: " + run.output, smt};
}

VerificationResult verify_syntactically(const ProofObligation& obligation, const std::string& smt) {
  for (const auto& assumption : obligation.assumptions) {
    if (expressions_equal(assumption.expr, obligation.goal.expr)) {
      return {obligation.name, VerificationStatus::Proven, "goal is an active assumption", smt};
    }
  }
  if (obligation.goal.expr && obligation.goal.expr->kind == ExprNode::Kind::Boolean &&
      obligation.goal.expr->boolean_value) {
    return {obligation.name, VerificationStatus::Proven, "goal is literal true", smt};
  }
  return {obligation.name, VerificationStatus::Unknown, "no local proof rule matched", smt};
}

} // namespace

std::vector<ProofObligation> build_obligations(const Module& module) {
  std::vector<ProofObligation> obligations;
  for (const auto& fn : module.functions) {
    SymbolTable symbols;
    for (const auto& param : fn.params) {
      symbols[param.name] = param.type;
    }
    if (fn.return_type.kind != TypeKind::Void) {
      symbols["result"] = fn.return_type;
    }

    std::vector<NamedPredicate> active = fn.preconditions;
    int assert_index = 0;
    int return_index = 0;
    for (const auto& statement : fn.body) {
      if (statement.kind == StatementKind::Let) {
        symbols[statement.name] = statement.type;
        auto equality =
            make_binary(BinaryOp::Equal, make_identifier(statement.name, statement.location),
                        statement.expr, statement.location);
        active.push_back(NamedPredicate{"let_" + statement.name, equality, statement.location});
      } else if (statement.kind == StatementKind::Assume) {
        active.push_back(NamedPredicate{statement.name, statement.expr, statement.location});
      } else if (statement.kind == StatementKind::Assert) {
        ++assert_index;
        ProofObligation obligation;
        obligation.name =
            "fn." + fn.name + ".assert." + std::to_string(assert_index) + "." + statement.name;
        obligation.assumptions = active;
        obligation.goal = NamedPredicate{statement.name, statement.expr, statement.location};
        obligation.symbols = symbols;
        obligations.push_back(std::move(obligation));
        active.push_back(NamedPredicate{statement.name, statement.expr, statement.location});
      } else if (statement.kind == StatementKind::Return && fn.return_type.kind != TypeKind::Void) {
        ++return_index;
        const auto return_name = "return_" + std::to_string(return_index);
        auto equality = make_binary(BinaryOp::Equal, make_identifier("result", statement.location),
                                    statement.expr, statement.location);
        active.push_back(NamedPredicate{return_name, equality, statement.location});
      }
    }

    int ensure_index = 0;
    for (const auto& ensure : fn.ensures) {
      ++ensure_index;
      ProofObligation obligation;
      obligation.name =
          "fn." + fn.name + ".ensures." + std::to_string(ensure_index) + "." + ensure.name;
      obligation.assumptions = active;
      obligation.goal = ensure;
      obligation.symbols = symbols;
      obligations.push_back(std::move(obligation));
    }
  }
  return obligations;
}

std::string emit_smt_lib(const ProofObligation& obligation) {
  SymbolTable symbols = obligation.symbols;
  std::vector<std::string> identifiers;
  for (const auto& assumption : obligation.assumptions) {
    collect_identifiers(assumption.expr, identifiers);
  }
  collect_identifiers(obligation.goal.expr, identifiers);
  for (const auto& identifier : identifiers) {
    if (symbols.find(identifier) == symbols.end()) {
      symbols[identifier] = Type{TypeKind::Unknown, "i64"};
    }
  }

  std::ostringstream out;
  out << "(set-logic ALL)\n";
  for (const auto& [name, type] : symbols) {
    out << "(declare-const " << sanitize_symbol(name) << " " << type.smt_sort() << ")\n";
  }
  for (const auto& assumption : obligation.assumptions) {
    out << "(assert " << emit_smt_expr(assumption.expr) << ")\n";
  }
  out << "(assert (not " << emit_smt_expr(obligation.goal.expr) << "))\n";
  out << "(check-sat)\n";
  return out.str();
}

std::string smt_file_name_for_obligation(const std::string& obligation_name) {
  std::string file_name;
  file_name.reserve(obligation_name.size() + 5);
  for (const char c : obligation_name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
        c == '-' || c == '_') {
      file_name += c;
    } else {
      file_name += '_';
    }
  }
  file_name += ".smt2";
  return file_name;
}

std::string write_smt_artifact(const ProofObligation& obligation, const std::string& smt,
                               const std::string& output_dir) {
  std::filesystem::create_directories(output_dir);
  const auto path =
      std::filesystem::path(output_dir) / smt_file_name_for_obligation(obligation.name);
  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("could not write SMT artifact: " + path.string());
  }
  file << smt;
  return path.string();
}

std::vector<VerificationResult> verify_obligations(const std::vector<ProofObligation>& obligations,
                                                   const ProofOptions& options) {
  std::vector<VerificationResult> results;
  for (const auto& obligation : obligations) {
    const auto smt = emit_smt_lib(obligation);
    std::string smt_path;
    if (!options.smt_output_dir.empty()) {
      try {
        smt_path = write_smt_artifact(obligation, smt, options.smt_output_dir);
      } catch (const std::exception& error) {
        results.push_back(
            VerificationResult{obligation.name, VerificationStatus::Error, error.what(), smt});
        continue;
      }
    }

    auto syntactic = verify_syntactically(obligation, smt);
    syntactic.smt_path = smt_path;
    if (syntactic.status == VerificationStatus::Proven || !options.use_z3) {
      results.push_back(std::move(syntactic));
      continue;
    }
    auto result = verify_with_z3(obligation, smt, options);
    result.smt_path = smt_path;
    results.push_back(std::move(result));
  }
  return results;
}

std::vector<VerificationResult> verify_obligations(const std::vector<ProofObligation>& obligations,
                                                   bool use_z3) {
  ProofOptions options;
  options.use_z3 = use_z3;
  return verify_obligations(obligations, options);
}

const char* status_name(VerificationStatus status) {
  switch (status) {
  case VerificationStatus::Proven:
    return "PROVEN";
  case VerificationStatus::Refuted:
    return "REFUTED";
  case VerificationStatus::Unknown:
    return "UNKNOWN";
  case VerificationStatus::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

} // namespace sigil

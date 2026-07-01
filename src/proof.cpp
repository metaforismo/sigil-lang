#include "sigil/proof.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

VerificationResult verify_with_z3(const ProofObligation& obligation, const std::string& smt) {
  const auto temp_path = std::filesystem::temp_directory_path() /
                         ("sigil-" + obligation.name + "-" + std::to_string(std::rand()) + ".smt2");
  {
    std::ofstream file(temp_path);
    if (!file) {
      return {obligation.name, VerificationStatus::Error, "could not create temporary SMT file",
              smt};
    }
    file << smt;
  }

  for (const auto& command : z3_command_candidates()) {
    int code = 0;
    const auto output = run_command(command + " -smt2 " + shell_quote(temp_path), code);
    if (output.find("command not found") != std::string::npos ||
        output.find("not found") != std::string::npos) {
      continue;
    }
    std::error_code ignored;
    std::filesystem::remove(temp_path, ignored);
    const auto solver_token = first_solver_token(output);
    if (solver_token == "unsat") {
      return {obligation.name, VerificationStatus::Proven, "proved by z3", smt};
    }
    if (solver_token == "sat") {
      return {obligation.name, VerificationStatus::Refuted, "z3 found a model violating the goal",
              smt};
    }
    if (solver_token == "unknown") {
      return {obligation.name, VerificationStatus::Unknown, "z3 returned unknown", smt};
    }
    return {obligation.name, VerificationStatus::Unknown, "z3 returned: " + output, smt};
  }

  std::error_code ignored;
  std::filesystem::remove(temp_path, ignored);
  return {obligation.name, VerificationStatus::Unknown,
          "z3 executable not found; syntactic checks only", smt};
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
      if (statement.kind == StatementKind::Assume) {
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

std::vector<VerificationResult> verify_obligations(const std::vector<ProofObligation>& obligations,
                                                   bool use_z3) {
  std::vector<VerificationResult> results;
  for (const auto& obligation : obligations) {
    const auto smt = emit_smt_lib(obligation);
    auto syntactic = verify_syntactically(obligation, smt);
    if (syntactic.status == VerificationStatus::Proven || !use_z3) {
      results.push_back(std::move(syntactic));
      continue;
    }
    results.push_back(verify_with_z3(obligation, smt));
  }
  return results;
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

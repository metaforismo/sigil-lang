#include "sigil/proof.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

std::string trim(std::string value) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string remove_define_fun_close(std::string value) {
  value = trim(std::move(value));
  if (!value.empty() && value.back() == ')') {
    value.pop_back();
  }
  return trim(std::move(value));
}

std::string normalize_model_value(std::string value) {
  value = remove_define_fun_close(std::move(value));
  if (value.rfind("(- ", 0) == 0 && value.size() > 4 && value.back() == ')') {
    return "-" + value.substr(3, value.size() - 4);
  }
  return value;
}

std::unordered_map<std::string, std::string> parse_model_values(const std::string& model) {
  std::unordered_map<std::string, std::string> values;
  std::istringstream lines(model);
  std::string line;
  std::string active_name;
  while (std::getline(lines, line)) {
    line = trim(std::move(line));
    if (line.empty() || line == "(" || line == ")") {
      continue;
    }

    constexpr std::string_view prefix = "(define-fun ";
    if (line.rfind(prefix, 0) == 0) {
      const auto name_start = prefix.size();
      const auto name_end = line.find(' ', name_start);
      active_name =
          name_end == std::string::npos ? "" : line.substr(name_start, name_end - name_start);
      continue;
    }

    if (!active_name.empty()) {
      values[active_name] = normalize_model_value(std::move(line));
      active_name.clear();
    }
  }
  return values;
}

std::vector<std::string> source_symbol_order(const ProofObligation& obligation) {
  std::vector<std::string> names;
  for (const auto& assumption : obligation.assumptions) {
    collect_identifiers(assumption.expr, names);
  }
  collect_identifiers(obligation.goal.expr, names);

  std::vector<std::string> remaining;
  for (const auto& [name, _] : obligation.symbols) {
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      remaining.push_back(name);
    }
  }
  std::sort(remaining.begin(), remaining.end());
  names.insert(names.end(), remaining.begin(), remaining.end());
  return names;
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

VerificationResult make_result(const ProofObligation& obligation, VerificationStatus status,
                               std::string details, const std::string& smt) {
  return {obligation.name, status, std::move(details), smt, obligation.range};
}

VerificationResult verify_with_z3(const ProofObligation& obligation, const std::string& smt,
                                  const ProofOptions& options) {
  SolverRun run;
  try {
    run = run_z3_query(obligation, smt);
  } catch (const std::exception& error) {
    return make_result(obligation, VerificationStatus::Error, error.what(), smt);
  }

  if (!run.launched) {
    return make_result(obligation, VerificationStatus::Unknown,
                       "z3 executable not found; syntactic checks only", smt);
  }

  const auto solver_token = first_solver_token(run.output);
  if (solver_token == "unsat") {
    return make_result(obligation, VerificationStatus::Proven, "proved by z3", smt);
  }
  if (solver_token == "sat") {
    auto result = make_result(obligation, VerificationStatus::Refuted,
                              "z3 found a counterexample model violating the goal", smt);
    if (options.include_models) {
      try {
        const auto model_run = run_z3_query(obligation, smt + "(get-model)\n");
        if (model_run.launched && first_solver_token(model_run.output) == "sat") {
          result.model = solver_tail(model_run.output);
          result.counterexample = render_source_counterexample(obligation, result.model);
        }
      } catch (const std::exception& error) {
        result.details += std::string("; model query failed: ") + error.what();
      }
    }
    return result;
  }
  if (solver_token == "unknown") {
    return make_result(obligation, VerificationStatus::Unknown, "z3 returned unknown", smt);
  }
  return make_result(obligation, VerificationStatus::Unknown, "z3 returned: " + run.output, smt);
}

using ExprSubstitutions = std::unordered_map<std::string, Expr>;

Expr substitute_for_wp(const Expr& expr, const ExprSubstitutions& substitutions) {
  if (!expr) {
    return expr;
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return make_integer(expr->integer_value, expr->range);
  case ExprNode::Kind::Boolean:
    return make_boolean(expr->boolean_value, expr->range);
  case ExprNode::Kind::Identifier: {
    const auto found = substitutions.find(expr->name);
    if (found != substitutions.end()) {
      return found->second;
    }
    return make_identifier(expr->name, expr->range);
  }
  case ExprNode::Kind::Call: {
    std::vector<Expr> arguments;
    arguments.reserve(expr->arguments.size());
    for (const auto& argument : expr->arguments) {
      arguments.push_back(substitute_for_wp(argument, substitutions));
    }
    return make_call(expr->name, std::move(arguments), expr->range);
  }
  case ExprNode::Kind::StructLiteral: {
    std::vector<FieldInitializer> fields;
    fields.reserve(expr->field_initializers.size());
    for (const auto& field : expr->field_initializers) {
      fields.push_back(FieldInitializer{field.name, substitute_for_wp(field.expr, substitutions),
                                        field.location, field.range});
    }
    return make_struct_literal(expr->name, std::move(fields), expr->range);
  }
  case ExprNode::Kind::FieldAccess:
    return make_field_access(substitute_for_wp(expr->lhs, substitutions), expr->name, expr->range);
  case ExprNode::Kind::Unary:
    return make_unary(expr->unary_op, substitute_for_wp(expr->lhs, substitutions), expr->range);
  case ExprNode::Kind::Binary:
    return make_binary(expr->binary_op, substitute_for_wp(expr->lhs, substitutions),
                       substitute_for_wp(expr->rhs, substitutions), expr->range);
  case ExprNode::Kind::If:
    return make_if(substitute_for_wp(expr->condition, substitutions),
                   substitute_for_wp(expr->lhs, substitutions),
                   substitute_for_wp(expr->rhs, substitutions), expr->range);
  }

  return expr;
}

ExprSubstitutions equality_substitutions(const std::vector<NamedPredicate>& assumptions) {
  ExprSubstitutions substitutions;
  for (const auto& assumption : assumptions) {
    const auto& expr = assumption.expr;
    if (!expr || expr->kind != ExprNode::Kind::Binary || expr->binary_op != BinaryOp::Equal ||
        !expr->lhs || expr->lhs->kind != ExprNode::Kind::Identifier) {
      continue;
    }
    substitutions[expr->lhs->name] = expr->rhs;
  }
  return substitutions;
}

Expr rewrite_with_equalities(const Expr& expr, const ExprSubstitutions& substitutions) {
  auto current = expr;
  for (int step = 0; step < 32; ++step) {
    auto next = substitute_for_wp(current, substitutions);
    if (display_expr(next) == display_expr(current)) {
      return next;
    }
    current = std::move(next);
  }
  return current;
}

bool is_literal_true(const Expr& expr) {
  if (!expr) {
    return false;
  }
  if (expr->kind == ExprNode::Kind::Boolean) {
    return expr->boolean_value;
  }
  if (expr->kind != ExprNode::Kind::Call || expr->name != "__sigil_select" ||
      expr->arguments.size() != 2) {
    return false;
  }

  const auto& array = expr->arguments[0];
  if (array && array->kind == ExprNode::Kind::Call && array->name == "__sigil_const_bool_array" &&
      array->arguments.size() == 1) {
    return is_literal_true(array->arguments[0]);
  }
  if (array && array->kind == ExprNode::Kind::Call && array->name == "__sigil_store" &&
      array->arguments.size() == 3 &&
      display_expr(array->arguments[1]) == display_expr(expr->arguments[1])) {
    return is_literal_true(array->arguments[2]);
  }
  return false;
}

bool is_reflexive_equality(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Binary && expr->binary_op == BinaryOp::Equal &&
         display_expr(expr->lhs) == display_expr(expr->rhs);
}

Expr first_conditional_condition(const Expr& expr) {
  if (!expr) {
    return nullptr;
  }
  if (expr->kind == ExprNode::Kind::If) {
    return expr->condition;
  }
  if (expr->kind == ExprNode::Kind::Call) {
    for (const auto& argument : expr->arguments) {
      if (const auto condition = first_conditional_condition(argument)) {
        return condition;
      }
    }
    return nullptr;
  }
  if (expr->kind == ExprNode::Kind::StructLiteral) {
    for (const auto& field : expr->field_initializers) {
      if (const auto condition = first_conditional_condition(field.expr)) {
        return condition;
      }
    }
    return nullptr;
  }
  if (expr->kind == ExprNode::Kind::FieldAccess || expr->kind == ExprNode::Kind::Unary) {
    return first_conditional_condition(expr->lhs);
  }
  if (expr->kind == ExprNode::Kind::Binary) {
    if (const auto condition = first_conditional_condition(expr->lhs)) {
      return condition;
    }
    return first_conditional_condition(expr->rhs);
  }
  return nullptr;
}

Expr select_conditional_branch(const Expr& expr, const Expr& condition, bool then_branch) {
  if (!expr) {
    return expr;
  }
  if (expr->kind == ExprNode::Kind::If && expressions_equal(expr->condition, condition)) {
    return select_conditional_branch(then_branch ? expr->lhs : expr->rhs, condition, then_branch);
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
  case ExprNode::Kind::Boolean:
  case ExprNode::Kind::Identifier:
    return expr;
  case ExprNode::Kind::Call: {
    std::vector<Expr> arguments;
    arguments.reserve(expr->arguments.size());
    for (const auto& argument : expr->arguments) {
      arguments.push_back(select_conditional_branch(argument, condition, then_branch));
    }
    return make_call(expr->name, std::move(arguments), expr->range);
  }
  case ExprNode::Kind::StructLiteral: {
    std::vector<FieldInitializer> fields;
    fields.reserve(expr->field_initializers.size());
    for (const auto& field : expr->field_initializers) {
      fields.push_back(FieldInitializer{
          field.name, select_conditional_branch(field.expr, condition, then_branch), field.location,
          field.range});
    }
    return make_struct_literal(expr->name, std::move(fields), expr->range);
  }
  case ExprNode::Kind::FieldAccess:
    return make_field_access(select_conditional_branch(expr->lhs, condition, then_branch),
                             expr->name, expr->range);
  case ExprNode::Kind::Unary:
    return make_unary(expr->unary_op, select_conditional_branch(expr->lhs, condition, then_branch),
                      expr->range);
  case ExprNode::Kind::Binary:
    return make_binary(expr->binary_op,
                       select_conditional_branch(expr->lhs, condition, then_branch),
                       select_conditional_branch(expr->rhs, condition, then_branch), expr->range);
  case ExprNode::Kind::If:
    return make_if(select_conditional_branch(expr->condition, condition, then_branch),
                   select_conditional_branch(expr->lhs, condition, then_branch),
                   select_conditional_branch(expr->rhs, condition, then_branch), expr->range);
  }
  return expr;
}

bool is_literal_false(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Boolean && !expr->boolean_value;
}

Expr specialize_branch_assumption(const Expr& expr, const Expr& condition, bool then_branch) {
  if (!expr) {
    return expr;
  }
  if (expressions_equal(expr, condition)) {
    return make_boolean(then_branch, expr->range);
  }
  if (expr->kind == ExprNode::Kind::Unary && expr->unary_op == UnaryOp::Not &&
      expressions_equal(expr->lhs, condition)) {
    return make_boolean(!then_branch, expr->range);
  }
  if (expr->kind == ExprNode::Kind::Unary) {
    auto operand = specialize_branch_assumption(expr->lhs, condition, then_branch);
    if (is_literal_true(operand)) {
      return make_boolean(false, expr->range);
    }
    if (is_literal_false(operand)) {
      return make_boolean(true, expr->range);
    }
    return make_unary(expr->unary_op, operand, expr->range);
  }
  if (expr->kind == ExprNode::Kind::Binary) {
    auto lhs = specialize_branch_assumption(expr->lhs, condition, then_branch);
    auto rhs = specialize_branch_assumption(expr->rhs, condition, then_branch);
    if (expr->binary_op == BinaryOp::And) {
      if (is_literal_false(lhs) || is_literal_false(rhs)) {
        return make_boolean(false, expr->range);
      }
      if (is_literal_true(lhs)) {
        return rhs;
      }
      if (is_literal_true(rhs)) {
        return lhs;
      }
    }
    if (expr->binary_op == BinaryOp::Or) {
      if (is_literal_true(lhs) || is_literal_true(rhs)) {
        return make_boolean(true, expr->range);
      }
      if (is_literal_false(lhs)) {
        return rhs;
      }
      if (is_literal_false(rhs)) {
        return lhs;
      }
    }
    return make_binary(expr->binary_op, lhs, rhs, expr->range);
  }
  return select_conditional_branch(expr, condition, then_branch);
}

ExprSubstitutions expression_equalities(const std::vector<Expr>& assumptions) {
  ExprSubstitutions substitutions;
  for (const auto& expr : assumptions) {
    if (expr && expr->kind == ExprNode::Kind::Binary && expr->binary_op == BinaryOp::Equal &&
        expr->lhs && expr->lhs->kind == ExprNode::Kind::Identifier &&
        display_expr(expr->lhs) != display_expr(expr->rhs)) {
      substitutions[expr->lhs->name] = expr->rhs;
    }
  }
  return substitutions;
}

void apply_branch_equalities(Expr& goal, std::vector<Expr>& assumptions) {
  const auto substitutions = expression_equalities(assumptions);
  goal = rewrite_with_equalities(goal, substitutions);
  for (auto& assumption : assumptions) {
    assumption = rewrite_with_equalities(assumption, substitutions);
  }
}

bool prove_control_flow_goal(const Expr& goal, const std::vector<Expr>& assumptions, int budget) {
  if (!goal || budget <= 0) {
    return false;
  }
  if (is_literal_true(goal) || is_reflexive_equality(goal)) {
    return true;
  }
  for (const auto& assumption : assumptions) {
    if (expressions_equal(assumption, goal)) {
      return true;
    }
  }

  if (goal->kind == ExprNode::Kind::Binary && goal->binary_op == BinaryOp::And) {
    return prove_control_flow_goal(goal->lhs, assumptions, budget - 1) &&
           prove_control_flow_goal(goal->rhs, assumptions, budget - 1);
  }
  if (goal->kind == ExprNode::Kind::Binary && goal->binary_op == BinaryOp::Or) {
    return prove_control_flow_goal(goal->lhs, assumptions, budget - 1) ||
           prove_control_flow_goal(goal->rhs, assumptions, budget - 1);
  }

  auto condition = first_conditional_condition(goal);
  if (!condition) {
    for (const auto& assumption : assumptions) {
      condition = first_conditional_condition(assumption);
      if (condition) {
        break;
      }
    }
  }
  if (!condition) {
    return false;
  }

  std::vector<Expr> then_assumptions;
  std::vector<Expr> else_assumptions;
  then_assumptions.reserve(assumptions.size() + 1);
  else_assumptions.reserve(assumptions.size() + 1);
  for (const auto& assumption : assumptions) {
    then_assumptions.push_back(specialize_branch_assumption(assumption, condition, true));
    else_assumptions.push_back(specialize_branch_assumption(assumption, condition, false));
  }
  then_assumptions.push_back(condition);
  else_assumptions.push_back(make_unary(UnaryOp::Not, condition, condition->range));

  auto then_goal = select_conditional_branch(goal, condition, true);
  auto else_goal = select_conditional_branch(goal, condition, false);
  apply_branch_equalities(then_goal, then_assumptions);
  apply_branch_equalities(else_goal, else_assumptions);
  return prove_control_flow_goal(then_goal, then_assumptions, budget - 1) &&
         prove_control_flow_goal(else_goal, else_assumptions, budget - 1);
}

VerificationResult verify_syntactically(const ProofObligation& obligation, const std::string& smt) {
  for (const auto& assumption : obligation.assumptions) {
    if (expressions_equal(assumption.expr, obligation.goal.expr)) {
      return make_result(obligation, VerificationStatus::Proven, "goal is an active assumption",
                         smt);
    }
  }
  if (is_literal_true(obligation.goal.expr)) {
    return make_result(obligation, VerificationStatus::Proven, "goal is literal true", smt);
  }

  const auto substitutions = equality_substitutions(obligation.assumptions);
  const auto rewritten_goal = rewrite_with_equalities(obligation.goal.expr, substitutions);
  if (is_literal_true(rewritten_goal) || is_reflexive_equality(rewritten_goal)) {
    return make_result(obligation, VerificationStatus::Proven,
                       "proved by weakest-precondition substitution", smt);
  }
  for (const auto& assumption : obligation.assumptions) {
    if (expressions_equal(rewrite_with_equalities(assumption.expr, substitutions),
                          rewritten_goal)) {
      return make_result(obligation, VerificationStatus::Proven,
                         "rewritten goal is an active assumption", smt);
    }
  }
  std::vector<Expr> rewritten_assumptions;
  rewritten_assumptions.reserve(obligation.assumptions.size());
  for (const auto& assumption : obligation.assumptions) {
    rewritten_assumptions.push_back(rewrite_with_equalities(assumption.expr, substitutions));
  }
  const auto has_loop_summary = std::any_of(
      obligation.assumptions.begin(), obligation.assumptions.end(),
      [](const auto& assumption) { return assumption.name.rfind("loop_summary.", 0) == 0; });
  const auto has_conditional_assumption =
      std::any_of(rewritten_assumptions.begin(), rewritten_assumptions.end(),
                  [](const auto& expr) { return first_conditional_condition(expr) != nullptr; });
  if ((first_conditional_condition(rewritten_goal) || has_conditional_assumption ||
       has_loop_summary) &&
      prove_control_flow_goal(rewritten_goal, rewritten_assumptions, 16)) {
    return make_result(obligation, VerificationStatus::Proven,
                       "proved by control-flow weakest-precondition", smt);
  }
  return make_result(obligation, VerificationStatus::Unknown, "no local proof rule matched", smt);
}

struct ProofContext {
  SymbolTable symbols;
  SymbolTable ref_symbols;
  SymbolTable model_symbols;
  std::unordered_set<std::string> allocation_symbols;
  std::unordered_map<std::string, std::string> bindings;
  std::unordered_map<std::string, std::string> struct_types;
  std::vector<NamedPredicate> active;
  struct ReturnPath {
    int index = 0;
    SymbolTable symbols;
    std::unordered_map<std::string, std::string> bindings;
    std::vector<NamedPredicate> assumptions;
  };
  std::vector<ReturnPath> returns;
  bool terminated = false;
  int scope_depth = 0;
};

using FunctionTable = std::unordered_map<std::string, const FunctionDecl*>;
using TheoremTable = std::unordered_map<std::string, const FunctionDecl*>;
using StructTable = std::unordered_map<std::string, const StructDecl*>;
using TypeSubstitutions = std::unordered_map<std::string, Type>;

constexpr std::string_view kTheoremProofPrefix = "theorem.";
constexpr std::string_view kModelSelectCall = "__sigil_select";
constexpr std::string_view kModelStoreCall = "__sigil_store";
constexpr std::string_view kConstI64ArrayCall = "__sigil_const_i64_array";
constexpr std::string_view kConstBoolArrayCall = "__sigil_const_bool_array";
constexpr std::string_view kEntryEpochSymbol = "__sigil_entry_epoch";

bool is_borrow_transition_name(const std::string& name) {
  return name == "borrow_shared" || name == "release_shared" || name == "borrow_mut" ||
         name == "release_mut";
}

bool is_consuming_transition_name(const std::string& name) {
  return name == "move_owner" || name == "deallocate";
}

bool is_allocation_name(const std::string& name) {
  return name == "allocate_array" || name == "allocate_slice" || name == "allocate_ref" ||
         name == "allocate_uninit_array" || name == "allocate_uninit_slice";
}

bool is_theorem_proof_subject(const FunctionDecl& fn) {
  return fn.name.rfind(std::string(kTheoremProofPrefix), 0) == 0;
}

std::string proof_subject_name(const FunctionDecl& fn) {
  if (is_theorem_proof_subject(fn)) {
    return fn.name;
  }
  return "fn." + fn.name;
}

std::string scoped_symbol(const std::string& name, const SourceLocation& location,
                          const std::string& purpose) {
  return name + "." + purpose + "." + std::to_string(location.line) + "." +
         std::to_string(location.column);
}

bool is_struct_type(const Type& type, const StructTable& structs) {
  return type.kind == TypeKind::Unknown && structs.find(type.spelling) != structs.end();
}

bool is_model_container_type(const Type& type) {
  return type.kind == TypeKind::Unknown && (type.spelling == "Array" || type.spelling == "Slice") &&
         type.arguments.size() == 1;
}

bool is_slice_model_type(const Type& type) {
  return type.kind == TypeKind::Unknown && type.spelling == "Slice" && type.arguments.size() == 1;
}

bool is_ref_model_type(const Type& type) {
  return type.kind == TypeKind::Unknown && type.spelling == "Ref" && type.arguments.size() == 1;
}

bool is_model_type(const Type& type) {
  return is_model_container_type(type) || is_ref_model_type(type);
}

void register_model_symbol(const std::string& symbol, const Type& type, ProofContext& context) {
  context.model_symbols[symbol] = type;
  context.allocation_symbols.insert(symbol);
}

bool same_type(const Type& lhs, const Type& rhs) {
  if (lhs.kind != rhs.kind || lhs.spelling != rhs.spelling ||
      lhs.arguments.size() != rhs.arguments.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.arguments.size(); ++index) {
    if (!same_type(lhs.arguments[index], rhs.arguments[index])) {
      return false;
    }
  }
  return true;
}

Type model_data_type(const Type& container_type) {
  return Type{TypeKind::Unknown, "__sigil_model_data", {container_type.arguments.front()}};
}

std::string model_len_symbol(const std::string& symbol) {
  return symbol + ".len";
}

std::string model_data_symbol(const std::string& symbol) {
  return symbol + ".data";
}

std::string model_init_symbol(const std::string& symbol) {
  return symbol + ".init";
}

Type model_init_type() {
  return Type{TypeKind::Unknown, "__sigil_model_data", {Type{TypeKind::Bool, "bool", {}}}};
}

std::string model_offset_symbol(const std::string& symbol) {
  return symbol + ".offset";
}

std::string allocation_symbol(const std::string& symbol) {
  return symbol + ".alloc";
}

std::string allocation_live_symbol(const std::string& symbol) {
  return symbol + ".live";
}

std::string owner_symbol(const std::string& symbol) {
  return symbol + ".owner";
}

std::string has_owner_symbol(const std::string& symbol) {
  return symbol + ".has_owner";
}

std::string shared_borrows_symbol(const std::string& symbol) {
  return symbol + ".shared";
}

std::string mut_borrow_symbol(const std::string& symbol) {
  return symbol + ".mut_borrow";
}

std::string ref_addr_symbol(const std::string& symbol) {
  return symbol + ".addr";
}

std::string ref_valid_symbol(const std::string& symbol) {
  return symbol + ".valid";
}

std::string ref_value_symbol(const std::string& symbol) {
  return symbol + ".value";
}

std::string ref_write_symbol(const std::string& symbol) {
  return symbol + ".write";
}

std::string ref_epoch_symbol(const std::string& symbol) {
  return symbol + ".epoch";
}

Type substitute_type(const Type& type, const TypeSubstitutions& substitutions) {
  const auto found = substitutions.find(type.spelling);
  if (type.kind == TypeKind::Unknown && found != substitutions.end()) {
    return found->second;
  }

  auto substituted = type;
  for (auto& argument : substituted.arguments) {
    argument = substitute_type(argument, substitutions);
  }
  return substituted;
}

TypeSubstitutions build_type_substitutions(const StructDecl& decl, const Type& concrete_type) {
  TypeSubstitutions substitutions;
  const auto count = std::min(decl.type_params.size(), concrete_type.arguments.size());
  for (std::size_t index = 0; index < count; ++index) {
    substitutions[decl.type_params[index].name] = concrete_type.arguments[index];
  }
  return substitutions;
}

const FieldDecl* find_field(const StructDecl& decl, const std::string& name) {
  for (const auto& field : decl.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

std::string field_symbol(const std::string& base_symbol, const std::string& field_name) {
  return base_symbol + "." + field_name;
}

const char* aggregate_proof_label(const StructDecl& decl) {
  return decl.is_container ? "container" : "struct";
}

Expr lower_model_intrinsic_call(std::string name, std::vector<Expr> arguments,
                                const SourceRange& range) {
  if (name == "len" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(model_len_symbol(arguments[0]->name), range);
  }
  if (name == "at" && arguments.size() == 2 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    auto physical_index =
        make_binary(BinaryOp::Add,
                    make_identifier(model_offset_symbol(arguments[0]->name), arguments[0]->range),
                    arguments[1], arguments[1]->range);
    return make_call(std::string(kModelSelectCall),
                     {make_identifier(model_data_symbol(arguments[0]->name), arguments[0]->range),
                      physical_index},
                     range);
  }
  if (name == "is_initialized" && arguments.size() == 2 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    auto physical_index =
        make_binary(BinaryOp::Add,
                    make_identifier(model_offset_symbol(arguments[0]->name), arguments[0]->range),
                    arguments[1], arguments[1]->range);
    return make_call(std::string(kModelSelectCall),
                     {make_identifier(model_init_symbol(arguments[0]->name), arguments[0]->range),
                      physical_index},
                     range);
  }
  if (name == "slice_offset" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(model_offset_symbol(arguments[0]->name), range);
  }
  if (name == "load" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(ref_value_symbol(arguments[0]->name), range);
  }
  if (name == "is_valid" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(ref_valid_symbol(arguments[0]->name), range);
  }
  if (name == "addr" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(ref_addr_symbol(arguments[0]->name), range);
  }
  if (name == "epoch" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(ref_epoch_symbol(arguments[0]->name), range);
  }
  if (name == "can_write" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(ref_write_symbol(arguments[0]->name), range);
  }
  if (name == "allocation_id" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(allocation_symbol(arguments[0]->name), range);
  }
  if (name == "is_live" && arguments.size() == 1 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier) {
    return make_identifier(allocation_live_symbol(arguments[0]->name), range);
  }
  if (arguments.size() == 1 && arguments[0] && arguments[0]->kind == ExprNode::Kind::Identifier) {
    if (name == "owner_id") {
      return make_identifier(owner_symbol(arguments[0]->name), range);
    }
    if (name == "has_owner") {
      return make_identifier(has_owner_symbol(arguments[0]->name), range);
    }
    if (name == "shared_borrows") {
      return make_identifier(shared_borrows_symbol(arguments[0]->name), range);
    }
    if (name == "has_mut_borrow") {
      return make_identifier(mut_borrow_symbol(arguments[0]->name), range);
    }
  }
  if ((name == "same_allocation" || name == "disjoint_allocation") && arguments.size() == 2 &&
      arguments[0] && arguments[0]->kind == ExprNode::Kind::Identifier && arguments[1] &&
      arguments[1]->kind == ExprNode::Kind::Identifier) {
    auto lhs = make_identifier(allocation_symbol(arguments[0]->name), arguments[0]->range);
    auto rhs = make_identifier(allocation_symbol(arguments[1]->name), arguments[1]->range);
    return make_binary(name == "same_allocation" ? BinaryOp::Equal : BinaryOp::NotEqual, lhs, rhs,
                       range);
  }
  if ((name == "same_view" || name == "overlaps") && arguments.size() == 2 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier && arguments[1] &&
      arguments[1]->kind == ExprNode::Kind::Identifier) {
    auto same_allocation = make_binary(
        BinaryOp::Equal,
        make_identifier(allocation_symbol(arguments[0]->name), arguments[0]->range),
        make_identifier(allocation_symbol(arguments[1]->name), arguments[1]->range), range);
    auto same_offset = make_binary(
        BinaryOp::Equal,
        make_identifier(model_offset_symbol(arguments[0]->name), arguments[0]->range),
        make_identifier(model_offset_symbol(arguments[1]->name), arguments[1]->range), range);
    auto same_length = make_binary(
        BinaryOp::Equal, make_identifier(model_len_symbol(arguments[0]->name), arguments[0]->range),
        make_identifier(model_len_symbol(arguments[1]->name), arguments[1]->range), range);
    if (name == "same_view") {
      return make_binary(BinaryOp::And, same_allocation,
                         make_binary(BinaryOp::And, same_offset, same_length, range), range);
    }

    auto left_length = make_identifier(model_len_symbol(arguments[0]->name), arguments[0]->range);
    auto right_length = make_identifier(model_len_symbol(arguments[1]->name), arguments[1]->range);
    auto left_end =
        make_binary(BinaryOp::Add,
                    make_identifier(model_offset_symbol(arguments[0]->name), arguments[0]->range),
                    left_length, range);
    auto right_end =
        make_binary(BinaryOp::Add,
                    make_identifier(model_offset_symbol(arguments[1]->name), arguments[1]->range),
                    right_length, range);
    auto left_before_right_end =
        make_binary(BinaryOp::Less,
                    make_identifier(model_offset_symbol(arguments[0]->name), arguments[0]->range),
                    right_end, range);
    auto right_before_left_end =
        make_binary(BinaryOp::Less,
                    make_identifier(model_offset_symbol(arguments[1]->name), arguments[1]->range),
                    left_end, range);
    auto left_nonempty = make_binary(BinaryOp::Greater, left_length, make_integer(0, range), range);
    auto right_nonempty =
        make_binary(BinaryOp::Greater, right_length, make_integer(0, range), range);
    auto ranges_intersect =
        make_binary(BinaryOp::And, left_before_right_end, right_before_left_end, range);
    return make_binary(
        BinaryOp::And, same_allocation,
        make_binary(BinaryOp::And, left_nonempty,
                    make_binary(BinaryOp::And, right_nonempty, ranges_intersect, range), range),
        range);
  }
  if ((name == "same_ref" || name == "disjoint") && arguments.size() == 2 && arguments[0] &&
      arguments[0]->kind == ExprNode::Kind::Identifier && arguments[1] &&
      arguments[1]->kind == ExprNode::Kind::Identifier) {
    auto lhs = make_identifier(ref_addr_symbol(arguments[0]->name), arguments[0]->range);
    auto rhs = make_identifier(ref_addr_symbol(arguments[1]->name), arguments[1]->range);
    return make_binary(name == "same_ref" ? BinaryOp::Equal : BinaryOp::NotEqual, lhs, rhs, range);
  }
  return make_call(std::move(name), std::move(arguments), range);
}

Expr rewrite_expr(const Expr& expr, const std::unordered_map<std::string, std::string>& bindings) {
  if (!expr) {
    return expr;
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return make_integer(expr->integer_value, expr->range);
  case ExprNode::Kind::Boolean:
    return make_boolean(expr->boolean_value, expr->range);
  case ExprNode::Kind::Identifier: {
    const auto found = bindings.find(expr->name);
    return make_identifier(found == bindings.end() ? expr->name : found->second, expr->range);
  }
  case ExprNode::Kind::Call: {
    std::vector<Expr> arguments;
    arguments.reserve(expr->arguments.size());
    for (const auto& argument : expr->arguments) {
      arguments.push_back(rewrite_expr(argument, bindings));
    }
    return lower_model_intrinsic_call(expr->name, std::move(arguments), expr->range);
  }
  case ExprNode::Kind::StructLiteral: {
    std::vector<FieldInitializer> fields;
    fields.reserve(expr->field_initializers.size());
    for (const auto& field : expr->field_initializers) {
      fields.push_back(FieldInitializer{field.name, rewrite_expr(field.expr, bindings),
                                        field.location, field.range});
    }
    return make_struct_literal(expr->name, std::move(fields), expr->range);
  }
  case ExprNode::Kind::FieldAccess:
    return make_field_access(rewrite_expr(expr->lhs, bindings), expr->name, expr->range);
  case ExprNode::Kind::Unary:
    return make_unary(expr->unary_op, rewrite_expr(expr->lhs, bindings), expr->range);
  case ExprNode::Kind::Binary:
    return make_binary(expr->binary_op, rewrite_expr(expr->lhs, bindings),
                       rewrite_expr(expr->rhs, bindings), expr->range);
  case ExprNode::Kind::If:
    return make_if(rewrite_expr(expr->condition, bindings), rewrite_expr(expr->lhs, bindings),
                   rewrite_expr(expr->rhs, bindings), expr->range);
  }

  return expr;
}

NamedPredicate rewrite_predicate(const NamedPredicate& predicate,
                                 const std::unordered_map<std::string, std::string>& bindings) {
  return NamedPredicate{predicate.name, rewrite_expr(predicate.expr, bindings), predicate.location,
                        predicate.range};
}

Expr substitute_expr(const Expr& expr, const ExprSubstitutions& substitutions) {
  if (!expr) {
    return expr;
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return make_integer(expr->integer_value, expr->range);
  case ExprNode::Kind::Boolean:
    return make_boolean(expr->boolean_value, expr->range);
  case ExprNode::Kind::Identifier: {
    const auto found = substitutions.find(expr->name);
    if (found != substitutions.end()) {
      return found->second;
    }
    return make_identifier(expr->name, expr->range);
  }
  case ExprNode::Kind::Call: {
    std::vector<Expr> arguments;
    arguments.reserve(expr->arguments.size());
    for (const auto& argument : expr->arguments) {
      arguments.push_back(substitute_expr(argument, substitutions));
    }
    return make_call(expr->name, std::move(arguments), expr->range);
  }
  case ExprNode::Kind::StructLiteral: {
    std::vector<FieldInitializer> fields;
    fields.reserve(expr->field_initializers.size());
    for (const auto& field : expr->field_initializers) {
      fields.push_back(FieldInitializer{field.name, substitute_expr(field.expr, substitutions),
                                        field.location, field.range});
    }
    return make_struct_literal(expr->name, std::move(fields), expr->range);
  }
  case ExprNode::Kind::FieldAccess:
    return make_field_access(substitute_expr(expr->lhs, substitutions), expr->name, expr->range);
  case ExprNode::Kind::Unary:
    return make_unary(expr->unary_op, substitute_expr(expr->lhs, substitutions), expr->range);
  case ExprNode::Kind::Binary:
    return make_binary(expr->binary_op, substitute_expr(expr->lhs, substitutions),
                       substitute_expr(expr->rhs, substitutions), expr->range);
  case ExprNode::Kind::If:
    return make_if(substitute_expr(expr->condition, substitutions),
                   substitute_expr(expr->lhs, substitutions),
                   substitute_expr(expr->rhs, substitutions), expr->range);
  }

  return expr;
}

NamedPredicate substitute_predicate(const NamedPredicate& predicate,
                                    const ExprSubstitutions& substitutions, SourceLocation location,
                                    SourceRange range, std::string name) {
  return NamedPredicate{std::move(name), substitute_expr(predicate.expr, substitutions),
                        std::move(location), std::move(range)};
}

void copy_symbols_for_expr(const Expr& expr, const ProofContext& from, ProofContext& to) {
  std::vector<std::string> identifiers;
  collect_identifiers(expr, identifiers);
  for (const auto& identifier : identifiers) {
    const auto found = from.symbols.find(identifier);
    if (found != from.symbols.end()) {
      to.symbols[identifier] = found->second;
    }
  }
}

NamedPredicate make_branch_condition(const Expr& condition, bool then_branch) {
  if (then_branch) {
    return NamedPredicate{"if_then", condition, condition->location, condition->range};
  }
  auto negated = make_unary(UnaryOp::Not, condition, condition->range);
  return NamedPredicate{"if_else", negated, condition->location, condition->range};
}

bool needs_nonzero_divisor(BinaryOp op) {
  return op == BinaryOp::Divide || op == BinaryOp::Modulo;
}

ProofObligation make_safety_obligation(const FunctionDecl& fn, int safety_index,
                                       const Expr& divisor, const ProofContext& context) {
  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".divisor_nonzero";
  obligation.location = divisor ? divisor->location : SourceLocation{};
  obligation.range = divisor && divisor->range.start.line != 0
                         ? divisor->range
                         : SourceRange{obligation.location, obligation.location};
  obligation.assumptions = context.active;
  auto zero = make_integer(0, obligation.range);
  auto goal = make_binary(BinaryOp::NotEqual, divisor, zero, obligation.range);
  obligation.goal = NamedPredicate{"divisor_nonzero", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_index_bounds_obligation(const FunctionDecl& fn, int safety_index,
                                             const Expr& access, const ProofContext& context) {
  const auto operation = access && !access->name.empty() ? access->name : std::string("model");
  if (!access || access->arguments.size() < 2) {
    throw Diagnostic(access ? access->range : SourceRange{},
                     operation + " access requires a container and an index");
  }

  const auto container = rewrite_expr(access->arguments[0], context.bindings);
  if (!container || container->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(access->arguments[0]->range,
                     operation + " access requires a materialized Array or Slice value");
  }

  const auto index = rewrite_expr(access->arguments[1], context.bindings);
  const auto len = make_identifier(model_len_symbol(container->name), access->arguments[0]->range);
  auto zero = make_integer(0, access->arguments[1]->range);
  auto lower = make_binary(BinaryOp::GreaterEqual, index, zero, access->arguments[1]->range);
  auto upper = make_binary(BinaryOp::Less, index, len, access->arguments[1]->range);
  auto goal = make_binary(BinaryOp::And, lower, upper, access->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".index_in_bounds";
  obligation.location = access->arguments[1]->location;
  obligation.range = access->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{"index_in_bounds", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_memory_initialized_obligation(const FunctionDecl& fn, int safety_index,
                                                   const Expr& access,
                                                   const ProofContext& context) {
  if (!access || access->arguments.size() < 2) {
    throw Diagnostic(access ? access->range : SourceRange{},
                     "initialization check requires a container and an index");
  }
  const auto container = rewrite_expr(access->arguments[0], context.bindings);
  if (!container || container->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(access->arguments[0]->range,
                     "initialization check requires a materialized Array or Slice value");
  }
  const auto index = rewrite_expr(access->arguments[1], context.bindings);
  auto physical_index = make_binary(
      BinaryOp::Add,
      make_identifier(model_offset_symbol(container->name), access->arguments[0]->range), index,
      access->arguments[1]->range);
  auto goal =
      make_call(std::string(kModelSelectCall),
                {make_identifier(model_init_symbol(container->name), access->arguments[0]->range),
                 physical_index},
                access->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".memory_initialized";
  obligation.location = access->arguments[1]->location;
  obligation.range = access->range;
  obligation.assumptions = context.active;
  obligation.goal =
      NamedPredicate{"memory_initialized", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_slice_view_bounds_obligation(const FunctionDecl& fn, int safety_index,
                                                  const Expr& view, const ProofContext& context) {
  if (!view || view->arguments.size() != 3) {
    throw Diagnostic(view ? view->range : SourceRange{},
                     "slice_view requires a source, start, and length");
  }
  const auto source = rewrite_expr(view->arguments[0], context.bindings);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(view->arguments[0]->range, "slice_view requires a materialized Slice value");
  }
  const auto start = rewrite_expr(view->arguments[1], context.bindings);
  const auto length = rewrite_expr(view->arguments[2], context.bindings);
  auto zero = make_integer(0, view->range);
  auto start_non_negative =
      make_binary(BinaryOp::GreaterEqual, start, zero, view->arguments[1]->range);
  auto length_non_negative = make_binary(BinaryOp::GreaterEqual, length,
                                         make_integer(0, view->range), view->arguments[2]->range);
  auto selected_end = make_binary(BinaryOp::Add, start, length, view->range);
  auto within_source = make_binary(
      BinaryOp::LessEqual, selected_end,
      make_identifier(model_len_symbol(source->name), view->arguments[0]->range), view->range);
  auto goal = make_binary(
      BinaryOp::And, start_non_negative,
      make_binary(BinaryOp::And, length_non_negative, within_source, view->range), view->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".view_in_bounds";
  obligation.location = view->location;
  obligation.range = view->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{"view_in_bounds", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_memory_valid_obligation(const FunctionDecl& fn, int safety_index,
                                             const Expr& access, const ProofContext& context) {
  const auto operation = access && !access->name.empty() ? access->name : std::string("ref");
  if (!access || access->arguments.empty()) {
    throw Diagnostic(access ? access->range : SourceRange{}, operation + " requires a reference");
  }

  const auto ref = rewrite_expr(access->arguments[0], context.bindings);
  if (!ref || ref->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(access->arguments[0]->range, operation + " requires a materialized Ref value");
  }

  auto goal = make_identifier(ref_valid_symbol(ref->name), access->arguments[0]->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".memory_valid";
  obligation.location = access->arguments[0]->location;
  obligation.range = access->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{"memory_valid", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_memory_live_obligation(const FunctionDecl& fn, int safety_index,
                                            const Expr& access, const ProofContext& context) {
  const auto operation = access && !access->name.empty() ? access->name : std::string("memory");
  if (!access || access->arguments.empty()) {
    throw Diagnostic(access ? access->range : SourceRange{}, operation + " requires a value");
  }

  const auto model = rewrite_expr(access->arguments[0], context.bindings);
  if (!model || model->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(access->arguments[0]->range,
                     operation + " requires a materialized memory model value");
  }

  auto goal = make_identifier(allocation_live_symbol(model->name), access->arguments[0]->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".memory_live";
  obligation.location = access->arguments[0]->location;
  obligation.range = access->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{"memory_live", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_memory_state_obligation(const FunctionDecl& fn, int safety_index,
                                             const Expr& operation, const ProofContext& context,
                                             const std::string& kind) {
  if (!operation || operation->arguments.empty()) {
    throw Diagnostic(operation ? operation->range : SourceRange{},
                     "memory-state operation requires a model value");
  }
  const auto model = rewrite_expr(operation->arguments[0], context.bindings);
  if (!model || model->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(operation->arguments[0]->range,
                     "memory-state operation requires a materialized memory model value");
  }

  Expr goal;
  if (kind == "ownership_present") {
    goal = make_identifier(has_owner_symbol(model->name), operation->arguments[0]->range);
  } else if (kind == "shared_borrow_available") {
    goal =
        make_unary(UnaryOp::Not, make_identifier(mut_borrow_symbol(model->name), operation->range),
                   operation->range);
  } else if (kind == "shared_borrow_active") {
    goal = make_binary(BinaryOp::Greater,
                       make_identifier(shared_borrows_symbol(model->name), operation->range),
                       make_integer(0, operation->range), operation->range);
  } else if (kind == "mutable_borrow_available") {
    auto no_shared = make_binary(
        BinaryOp::Equal, make_identifier(shared_borrows_symbol(model->name), operation->range),
        make_integer(0, operation->range), operation->range);
    auto no_mut =
        make_unary(UnaryOp::Not, make_identifier(mut_borrow_symbol(model->name), operation->range),
                   operation->range);
    goal = make_binary(BinaryOp::And, no_shared, no_mut, operation->range);
  } else if (kind == "mutable_borrow_active") {
    goal = make_identifier(mut_borrow_symbol(model->name), operation->arguments[0]->range);
  } else if (kind == "borrow_free") {
    auto no_shared = make_binary(
        BinaryOp::Equal, make_identifier(shared_borrows_symbol(model->name), operation->range),
        make_integer(0, operation->range), operation->range);
    auto no_mut =
        make_unary(UnaryOp::Not, make_identifier(mut_borrow_symbol(model->name), operation->range),
                   operation->range);
    goal = make_binary(BinaryOp::And, no_shared, no_mut, operation->range);
  } else {
    throw Diagnostic(operation->range, "unknown memory-state obligation '" + kind + "'");
  }

  ProofObligation obligation;
  obligation.name = proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + "." + kind;
  obligation.location = operation->arguments[0]->location;
  obligation.range = operation->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{kind, goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_allocation_unique_obligation(const FunctionDecl& fn, int safety_index,
                                                  const Expr& operation,
                                                  const ProofContext& context) {
  if (!operation || operation->arguments.empty()) {
    throw Diagnostic(operation ? operation->range : SourceRange{},
                     "consuming operation requires a model value");
  }
  const auto source = rewrite_expr(operation->arguments[0], context.bindings);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(operation->arguments[0]->range,
                     "consuming operation requires a materialized memory model value");
  }

  std::vector<std::string> others;
  for (const auto& [symbol, _] : context.model_symbols) {
    if (symbol != source->name) {
      others.push_back(symbol);
    }
  }
  std::sort(others.begin(), others.end());

  Expr goal = make_boolean(true, operation->range);
  bool has_comparison = false;
  for (const auto& other : others) {
    auto disjoint = make_binary(
        BinaryOp::NotEqual, make_identifier(allocation_symbol(source->name), operation->range),
        make_identifier(allocation_symbol(other), operation->range), operation->range);
    if (!has_comparison) {
      goal = disjoint;
      has_comparison = true;
    } else {
      goal = make_binary(BinaryOp::And, goal, disjoint, operation->range);
    }
  }

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".allocation_unique";
  obligation.location = operation->arguments[0]->location;
  obligation.range = operation->range;
  obligation.assumptions = context.active;
  obligation.goal =
      NamedPredicate{"allocation_unique", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_allocation_size_obligation(const FunctionDecl& fn, int safety_index,
                                                const Expr& allocation,
                                                const ProofContext& context) {
  if (!allocation || allocation->arguments.empty()) {
    throw Diagnostic(allocation ? allocation->range : SourceRange{},
                     "allocation requires a length");
  }
  const auto length = rewrite_expr(allocation->arguments[0], context.bindings);
  auto goal = make_binary(BinaryOp::GreaterEqual, length, make_integer(0, allocation->range),
                          allocation->range);

  ProofObligation obligation;
  obligation.name = proof_subject_name(fn) + ".safety." + std::to_string(safety_index) +
                    ".allocation_size_nonnegative";
  obligation.location = allocation->arguments[0]->location;
  obligation.range = allocation->range;
  obligation.assumptions = context.active;
  obligation.goal =
      NamedPredicate{"allocation_size_nonnegative", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

ProofObligation make_memory_write_obligation(const FunctionDecl& fn, int safety_index,
                                             const Expr& access, const ProofContext& context) {
  const auto operation = access && !access->name.empty() ? access->name : std::string("ref");
  if (!access || access->arguments.empty()) {
    throw Diagnostic(access ? access->range : SourceRange{}, operation + " requires a reference");
  }

  const auto ref = rewrite_expr(access->arguments[0], context.bindings);
  if (!ref || ref->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(access->arguments[0]->range, operation + " requires a materialized Ref value");
  }

  auto goal = make_identifier(ref_write_symbol(ref->name), access->arguments[0]->range);

  ProofObligation obligation;
  obligation.name =
      proof_subject_name(fn) + ".safety." + std::to_string(safety_index) + ".memory_write";
  obligation.location = access->arguments[0]->location;
  obligation.range = access->range;
  obligation.assumptions = context.active;
  obligation.goal = NamedPredicate{"memory_write", goal, obligation.location, obligation.range};
  obligation.symbols = context.symbols;
  return obligation;
}

void append_expression_safety_obligations(const Expr& expr, const FunctionDecl& fn,
                                          const ProofContext& context, int& safety_index,
                                          std::vector<ProofObligation>& obligations) {
  if (!expr) {
    return;
  }

  if (expr->kind == ExprNode::Kind::Call) {
    for (const auto& argument : expr->arguments) {
      append_expression_safety_obligations(argument, fn, context, safety_index, obligations);
    }
    if (expr->name == "at" || expr->name == "load" || expr->name == "store" ||
        expr->name == "slice_view" || is_borrow_transition_name(expr->name) ||
        is_consuming_transition_name(expr->name)) {
      ++safety_index;
      obligations.push_back(make_memory_live_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "store") {
      ++safety_index;
      obligations.push_back(
          make_memory_state_obligation(fn, safety_index, expr, context, "ownership_present"));
      ++safety_index;
      obligations.push_back(
          make_memory_state_obligation(fn, safety_index, expr, context, "mutable_borrow_active"));
    }
    if (is_borrow_transition_name(expr->name)) {
      ++safety_index;
      obligations.push_back(
          make_memory_state_obligation(fn, safety_index, expr, context, "ownership_present"));
      ++safety_index;
      const auto kind = expr->name == "borrow_shared"    ? "shared_borrow_available"
                        : expr->name == "release_shared" ? "shared_borrow_active"
                        : expr->name == "borrow_mut"     ? "mutable_borrow_available"
                                                         : "mutable_borrow_active";
      obligations.push_back(make_memory_state_obligation(fn, safety_index, expr, context, kind));
    }
    if (is_consuming_transition_name(expr->name)) {
      ++safety_index;
      obligations.push_back(
          make_memory_state_obligation(fn, safety_index, expr, context, "ownership_present"));
      ++safety_index;
      obligations.push_back(
          make_memory_state_obligation(fn, safety_index, expr, context, "borrow_free"));
      ++safety_index;
      obligations.push_back(make_allocation_unique_obligation(fn, safety_index, expr, context));
    }
    if (is_allocation_name(expr->name) && expr->name != "allocate_ref") {
      ++safety_index;
      obligations.push_back(make_allocation_size_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "at" || (expr->name == "store" && expr->arguments.size() == 3)) {
      ++safety_index;
      obligations.push_back(make_index_bounds_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "at") {
      ++safety_index;
      obligations.push_back(make_memory_initialized_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "slice_view") {
      ++safety_index;
      obligations.push_back(make_slice_view_bounds_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "load" || (expr->name == "store" && expr->arguments.size() == 2)) {
      ++safety_index;
      obligations.push_back(make_memory_valid_obligation(fn, safety_index, expr, context));
    }
    if (expr->name == "store" && expr->arguments.size() == 2) {
      ++safety_index;
      obligations.push_back(make_memory_write_obligation(fn, safety_index, expr, context));
    }
    return;
  }

  if (expr->kind == ExprNode::Kind::If) {
    append_expression_safety_obligations(expr->condition, fn, context, safety_index, obligations);

    const auto condition = rewrite_expr(expr->condition, context.bindings);
    auto then_context = context;
    then_context.active.push_back(make_branch_condition(condition, true));
    append_expression_safety_obligations(expr->lhs, fn, then_context, safety_index, obligations);

    auto else_context = context;
    else_context.active.push_back(make_branch_condition(condition, false));
    append_expression_safety_obligations(expr->rhs, fn, else_context, safety_index, obligations);
    return;
  }

  if (expr->kind == ExprNode::Kind::Binary) {
    append_expression_safety_obligations(expr->lhs, fn, context, safety_index, obligations);
    if (expr->binary_op == BinaryOp::And || expr->binary_op == BinaryOp::Or) {
      const auto lhs = rewrite_expr(expr->lhs, context.bindings);
      auto rhs_context = context;
      rhs_context.active.push_back(make_branch_condition(lhs, expr->binary_op == BinaryOp::And));
      append_expression_safety_obligations(expr->rhs, fn, rhs_context, safety_index, obligations);
      return;
    }
    append_expression_safety_obligations(expr->rhs, fn, context, safety_index, obligations);
  } else {
    append_expression_safety_obligations(expr->condition, fn, context, safety_index, obligations);
    append_expression_safety_obligations(expr->lhs, fn, context, safety_index, obligations);
    append_expression_safety_obligations(expr->rhs, fn, context, safety_index, obligations);
  }

  if (expr->kind == ExprNode::Kind::Binary && needs_nonzero_divisor(expr->binary_op)) {
    ++safety_index;
    obligations.push_back(make_safety_obligation(
        fn, safety_index, rewrite_expr(expr->rhs, context.bindings), context));
  }
}

NamedPredicate make_guarded_fact(const Expr& condition, const NamedPredicate& fact,
                                 bool then_branch) {
  Expr guard_condition = condition;
  if (then_branch) {
    guard_condition = make_unary(UnaryOp::Not, condition, condition->range);
  }
  auto guarded = make_binary(BinaryOp::Or, guard_condition, fact.expr, fact.range);
  return NamedPredicate{std::string(then_branch ? "if_then_" : "if_else_") + fact.name, guarded,
                        fact.location, fact.range};
}

Expr materialize_expr(const Expr& expr, const FunctionDecl& fn, ProofContext& context,
                      int& call_index, const StructTable& structs, const FunctionTable& functions,
                      const TheoremTable& theorems, std::vector<ProofObligation>& obligations);

ProofObligation make_call_precondition_obligation(const FunctionDecl& caller, int call_index,
                                                  int precondition_index, const Expr& call,
                                                  const NamedPredicate& goal,
                                                  const ProofContext& context) {
  ProofObligation obligation;
  obligation.name = proof_subject_name(caller) + ".call." + std::to_string(call_index) +
                    ".requires." + std::to_string(precondition_index) + "." + goal.name;
  obligation.location = call ? call->location : SourceLocation{};
  obligation.range = call && call->range.start.line != 0
                         ? call->range
                         : SourceRange{obligation.location, obligation.location};
  obligation.assumptions = context.active;
  obligation.goal = goal;
  obligation.symbols = context.symbols;
  return obligation;
}

Expr materialize_resolved_call_expr(const Expr& expr, const FunctionDecl& fn,
                                    const FunctionDecl& callee, ProofContext& context,
                                    int& call_index, const StructTable& structs,
                                    const FunctionTable& functions, const TheoremTable& theorems,
                                    std::vector<ProofObligation>& obligations) {
  std::vector<Expr> arguments;
  arguments.reserve(expr->arguments.size());
  for (const auto& argument : expr->arguments) {
    arguments.push_back(materialize_expr(argument, fn, context, call_index, structs, functions,
                                         theorems, obligations));
  }

  ExprSubstitutions substitutions;
  for (std::size_t index = 0; index < callee.params.size() && index < arguments.size(); ++index) {
    substitutions[callee.params[index].name] = arguments[index];
  }

  ++call_index;
  int precondition_index = 0;
  for (const auto& precondition : callee.preconditions) {
    ++precondition_index;
    auto goal = substitute_predicate(precondition, substitutions, expr->location, expr->range,
                                     precondition.name);
    goal = rewrite_predicate(goal, {});
    obligations.push_back(
        make_call_precondition_obligation(fn, call_index, precondition_index, expr, goal, context));
  }

  if (callee.return_type.kind == TypeKind::Void) {
    throw Diagnostic(expr->range,
                     "function '" + expr->name + "' returns void and cannot be used as a value");
  }

  const auto result_symbol =
      scoped_symbol(expr->name, expr->location, "call." + std::to_string(call_index));
  context.symbols[result_symbol] = callee.return_type;
  auto result_expr = make_identifier(result_symbol, expr->range);
  substitutions["result"] = result_expr;

  for (const auto& ensure : callee.ensures) {
    auto fact = substitute_predicate(ensure, substitutions, expr->location, expr->range,
                                     "call_" + expr->name + "_" + std::to_string(call_index) + "_" +
                                         ensure.name);
    fact = rewrite_predicate(fact, {});
    context.active.push_back(std::move(fact));
  }

  return result_expr;
}

Expr materialize_call_expr(const Expr& expr, const FunctionDecl& fn, ProofContext& context,
                           int& call_index, const StructTable& structs,
                           const FunctionTable& functions, const TheoremTable& theorems,
                           std::vector<ProofObligation>& obligations) {
  const auto found = functions.find(expr->name);
  if (found != functions.end()) {
    return materialize_resolved_call_expr(expr, fn, *found->second, context, call_index, structs,
                                          functions, theorems, obligations);
  }

  const auto theorem_found = theorems.find(expr->name);
  if (theorem_found != theorems.end()) {
    return materialize_resolved_call_expr(expr, fn, *theorem_found->second, context, call_index,
                                          structs, functions, theorems, obligations);
  }

  if (expr->name == "len" || expr->name == "at" || expr->name == "load" ||
      expr->name == "is_valid" || expr->name == "addr" || expr->name == "epoch" ||
      expr->name == "can_write" || expr->name == "same_ref" || expr->name == "disjoint" ||
      expr->name == "allocation_id" || expr->name == "same_allocation" ||
      expr->name == "disjoint_allocation" || expr->name == "is_live" || expr->name == "owner_id" ||
      expr->name == "has_owner" || expr->name == "shared_borrows" ||
      expr->name == "has_mut_borrow" || expr->name == "slice_offset" || expr->name == "same_view" ||
      expr->name == "overlaps" || expr->name == "is_initialized") {
    std::vector<Expr> arguments;
    arguments.reserve(expr->arguments.size());
    for (const auto& argument : expr->arguments) {
      arguments.push_back(materialize_expr(argument, fn, context, call_index, structs, functions,
                                           theorems, obligations));
    }
    return lower_model_intrinsic_call(expr->name, std::move(arguments), expr->range);
  }

  if (expr->name == "store") {
    throw Diagnostic(expr->range,
                     "store returns a model value and must be bound with let before proof use");
  }

  if (expr->name == "slice_view") {
    throw Diagnostic(expr->range,
                     "slice_view returns a model value and must be bound with let before use");
  }

  if (is_allocation_name(expr->name)) {
    throw Diagnostic(expr->range,
                     expr->name + " returns a model value and must be bound before use");
  }

  if (is_consuming_transition_name(expr->name)) {
    throw Diagnostic(expr->range,
                     expr->name + " returns a consumed model value and must be bound with let");
  }

  if (is_borrow_transition_name(expr->name)) {
    throw Diagnostic(expr->range,
                     expr->name + " returns a model value and must be bound with let before use");
  }

  throw Diagnostic(expr->range, "unknown function '" + expr->name + "'");
}

ExprSubstitutions struct_field_substitutions(const StructDecl& decl, const std::string& symbol,
                                             const SourceRange& range) {
  ExprSubstitutions substitutions;
  for (const auto& field : decl.fields) {
    substitutions[field.name] = make_identifier(field_symbol(symbol, field.name), range);
  }
  return substitutions;
}

void append_struct_invariant_obligations(const Expr& expr, const std::string& target_symbol,
                                         const StructDecl& decl, const FunctionDecl& fn,
                                         ProofContext& context, int& call_index,
                                         const StructTable& structs, const FunctionTable& functions,
                                         const TheoremTable& theorems,
                                         std::vector<ProofObligation>& obligations) {
  const auto substitutions = struct_field_substitutions(decl, target_symbol, expr->range);
  int invariant_index = 0;
  for (const auto& invariant : decl.invariants) {
    ++invariant_index;
    auto goal =
        substitute_predicate(invariant, substitutions, expr->location, expr->range, invariant.name);
    goal.expr = materialize_expr(goal.expr, fn, context, call_index, structs, functions, theorems,
                                 obligations);

    ProofObligation obligation;
    obligation.name = proof_subject_name(fn) + "." + aggregate_proof_label(decl) + "." +
                      target_symbol + ".invariant." + std::to_string(invariant_index) + "." +
                      invariant.name;
    obligation.location = expr->location;
    obligation.range = expr->range;
    obligation.assumptions = context.active;
    obligation.goal = goal;
    obligation.symbols = context.symbols;
    obligations.push_back(std::move(obligation));

    goal.name = "struct_" + target_symbol + "_" + invariant.name;
    context.active.push_back(std::move(goal));
  }
}

void add_symbol_equality_fact(const std::string& name, const std::string& target_symbol,
                              const std::string& source_symbol, const SourceRange& range,
                              const SourceLocation& location, ProofContext& context) {
  auto equality = make_binary(BinaryOp::Equal, make_identifier(target_symbol, range),
                              make_identifier(source_symbol, range), range);
  context.active.push_back(NamedPredicate{name, equality, location, range});
}

void register_ownership_state(const std::string& symbol, ProofContext& context) {
  const auto owner = owner_symbol(symbol);
  const auto has_owner = has_owner_symbol(symbol);
  const auto shared = shared_borrows_symbol(symbol);
  const auto mut_borrow = mut_borrow_symbol(symbol);
  context.symbols[owner] = Type{TypeKind::I64, "i64", {}};
  context.symbols[has_owner] = Type{TypeKind::Bool, "bool", {}};
  context.symbols[shared] = Type{TypeKind::I64, "i64", {}};
  context.symbols[mut_borrow] = Type{TypeKind::Bool, "bool", {}};

  context.active.push_back(
      NamedPredicate{"ownership_" + sanitize_symbol(symbol) + "_shared_nonnegative",
                     make_binary(BinaryOp::GreaterEqual, make_identifier(shared), make_integer(0)),
                     SourceLocation{}, SourceRange{}});
  context.active.push_back(NamedPredicate{
      "ownership_" + sanitize_symbol(symbol) + "_borrow_exclusion",
      make_binary(BinaryOp::Or, make_unary(UnaryOp::Not, make_identifier(mut_borrow)),
                  make_binary(BinaryOp::Equal, make_identifier(shared), make_integer(0))),
      SourceLocation{}, SourceRange{}});
  context.active.push_back(NamedPredicate{
      "ownership_" + sanitize_symbol(symbol) + "_owner_consistency",
      make_binary(BinaryOp::Or, make_unary(UnaryOp::Not, make_identifier(has_owner)),
                  make_binary(BinaryOp::NotEqual, make_identifier(owner), make_integer(0))),
      SourceLocation{}, SourceRange{}});
}

void register_model_offset(const std::string& symbol, const Type& type, ProofContext& context) {
  const auto offset = model_offset_symbol(symbol);
  context.symbols[offset] = Type{TypeKind::I64, "i64", {}};
  Expr constraint;
  std::string suffix;
  if (is_slice_model_type(type)) {
    constraint = make_binary(BinaryOp::GreaterEqual, make_identifier(offset), make_integer(0));
    suffix = "offset_nonnegative";
  } else {
    constraint = make_binary(BinaryOp::Equal, make_identifier(offset), make_integer(0));
    suffix = "array_offset_zero";
  }
  context.active.push_back(NamedPredicate{"model_" + sanitize_symbol(symbol) + "_" + suffix,
                                          constraint, SourceLocation{}, SourceRange{}});
}

void copy_ownership_state(const std::string& prefix, const std::string& target,
                          const std::string& source, const SourceRange& range,
                          const SourceLocation& location, ProofContext& context) {
  add_symbol_equality_fact(prefix + owner_symbol(target), owner_symbol(target),
                           owner_symbol(source), range, location, context);
  add_symbol_equality_fact(prefix + has_owner_symbol(target), has_owner_symbol(target),
                           has_owner_symbol(source), range, location, context);
  add_symbol_equality_fact(prefix + shared_borrows_symbol(target), shared_borrows_symbol(target),
                           shared_borrows_symbol(source), range, location, context);
  add_symbol_equality_fact(prefix + mut_borrow_symbol(target), mut_borrow_symbol(target),
                           mut_borrow_symbol(source), range, location, context);
}

void add_ref_alias_consistency_facts(const std::string& symbol, const Type& type,
                                     const SourceRange& range, const SourceLocation& location,
                                     ProofContext& context) {
  if (!is_ref_model_type(type)) {
    return;
  }

  std::vector<std::string> refs;
  for (const auto& [candidate, candidate_type] : context.ref_symbols) {
    if (candidate != symbol && is_ref_model_type(candidate_type) &&
        same_type(candidate_type, type)) {
      refs.push_back(candidate);
    }
  }
  std::sort(refs.begin(), refs.end());

  for (const auto& existing : refs) {
    auto existing_invalid =
        make_unary(UnaryOp::Not, make_identifier(ref_valid_symbol(existing), range), range);
    auto current_invalid =
        make_unary(UnaryOp::Not, make_identifier(ref_valid_symbol(symbol), range), range);
    auto either_invalid = make_binary(BinaryOp::Or, existing_invalid, current_invalid, range);
    auto epochs_differ =
        make_binary(BinaryOp::NotEqual, make_identifier(ref_epoch_symbol(existing), range),
                    make_identifier(ref_epoch_symbol(symbol), range), range);
    auto invalid_or_epoch = make_binary(BinaryOp::Or, either_invalid, epochs_differ, range);
    auto addresses_differ =
        make_binary(BinaryOp::NotEqual, make_identifier(ref_addr_symbol(existing), range),
                    make_identifier(ref_addr_symbol(symbol), range), range);
    auto values_match =
        make_binary(BinaryOp::Equal, make_identifier(ref_value_symbol(existing), range),
                    make_identifier(ref_value_symbol(symbol), range), range);
    auto address_or_value = make_binary(BinaryOp::Or, addresses_differ, values_match, range);
    auto consistency = make_binary(BinaryOp::Or, invalid_or_epoch, address_or_value, range);

    context.active.push_back(
        NamedPredicate{"ref_alias_" + sanitize_symbol(existing) + "_" + sanitize_symbol(symbol),
                       consistency, location, range});
  }
}

void register_model_alias(const std::string& target_symbol, const Type& type,
                          const Expr& source_expr, const SourceRange& range,
                          const SourceLocation& location, ProofContext& context) {
  if (!source_expr || source_expr->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(range, "model field initializer could not be materialized");
  }
  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);

  if (is_model_container_type(type)) {
    const auto target_len = model_len_symbol(target_symbol);
    const auto target_data = model_data_symbol(target_symbol);
    const auto target_init = model_init_symbol(target_symbol);
    const auto target_offset = model_offset_symbol(target_symbol);
    const auto target_allocation = allocation_symbol(target_symbol);
    const auto target_live = allocation_live_symbol(target_symbol);
    const auto source_len = model_len_symbol(source_expr->name);
    const auto source_data = model_data_symbol(source_expr->name);
    const auto source_init = model_init_symbol(source_expr->name);
    const auto source_offset = model_offset_symbol(source_expr->name);
    const auto source_allocation = allocation_symbol(source_expr->name);
    const auto source_live = allocation_live_symbol(source_expr->name);
    context.symbols[target_len] = Type{TypeKind::I64, "i64", {}};
    context.symbols[target_data] = model_data_type(type);
    context.symbols[target_init] = model_init_type();
    register_model_offset(target_symbol, type, context);
    context.symbols[target_allocation] = Type{TypeKind::I64, "i64", {}};
    context.symbols[target_live] = Type{TypeKind::Bool, "bool", {}};
    register_ownership_state(target_symbol, context);

    auto len_non_negative =
        make_binary(BinaryOp::GreaterEqual, make_identifier(target_len, SourceLocation{}),
                    make_integer(0, SourceLocation{}), SourceLocation{});
    context.active.push_back(
        NamedPredicate{"model_" + sanitize_symbol(target_symbol) + "_len_non_negative",
                       len_non_negative, SourceLocation{}, SourceRange{}});
    add_symbol_equality_fact("field_" + target_len, target_len, source_len, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_data, target_data, source_data, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_init, target_init, source_init, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_offset, target_offset, source_offset, range,
                             location, context);
    add_symbol_equality_fact("field_" + target_allocation, target_allocation, source_allocation,
                             range, location, context);
    add_symbol_equality_fact("field_" + target_live, target_live, source_live, range, location,
                             context);
    copy_ownership_state("field_", target_symbol, source_expr->name, range, location, context);
    return;
  }

  if (is_ref_model_type(type)) {
    const auto target_addr = ref_addr_symbol(target_symbol);
    const auto target_valid = ref_valid_symbol(target_symbol);
    const auto target_value = ref_value_symbol(target_symbol);
    const auto target_write = ref_write_symbol(target_symbol);
    const auto target_epoch = ref_epoch_symbol(target_symbol);
    const auto target_allocation = allocation_symbol(target_symbol);
    const auto target_live = allocation_live_symbol(target_symbol);
    const auto source_addr = ref_addr_symbol(source_expr->name);
    const auto source_valid = ref_valid_symbol(source_expr->name);
    const auto source_value = ref_value_symbol(source_expr->name);
    const auto source_write = ref_write_symbol(source_expr->name);
    const auto source_epoch = ref_epoch_symbol(source_expr->name);
    const auto source_allocation = allocation_symbol(source_expr->name);
    const auto source_live = allocation_live_symbol(source_expr->name);
    context.ref_symbols[target_symbol] = type;
    context.symbols[target_addr] = Type{TypeKind::I64, "i64", {}};
    context.symbols[target_valid] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[target_value] = type.arguments.front();
    context.symbols[target_write] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[target_epoch] = Type{TypeKind::I64, "i64", {}};
    context.symbols[target_allocation] = Type{TypeKind::I64, "i64", {}};
    context.symbols[target_live] = Type{TypeKind::Bool, "bool", {}};
    register_ownership_state(target_symbol, context);
    add_symbol_equality_fact("field_" + target_addr, target_addr, source_addr, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_valid, target_valid, source_valid, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_value, target_value, source_value, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_write, target_write, source_write, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_epoch, target_epoch, source_epoch, range, location,
                             context);
    add_symbol_equality_fact("field_" + target_allocation, target_allocation, source_allocation,
                             range, location, context);
    add_symbol_equality_fact("field_" + target_live, target_live, source_live, range, location,
                             context);
    copy_ownership_state("field_", target_symbol, source_expr->name, range, location, context);
    add_ref_alias_consistency_facts(target_symbol, type, range, location, context);
  }
}

bool is_model_store_call(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Call && expr->name == "store";
}

bool is_model_container_store_call(const Expr& expr) {
  return is_model_store_call(expr) && expr->arguments.size() == 3;
}

bool is_ref_store_call(const Expr& expr) {
  return is_model_store_call(expr) && expr->arguments.size() == 2;
}

bool is_slice_view_call(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Call && expr->name == "slice_view" &&
         expr->arguments.size() == 3;
}

bool is_allocation_call(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Call && is_allocation_name(expr->name);
}

bool is_consuming_transition_call(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Call && is_consuming_transition_name(expr->name) &&
         expr->arguments.size() == 1;
}

bool is_borrow_transition_call(const Expr& expr) {
  return expr && expr->kind == ExprNode::Kind::Call && is_borrow_transition_name(expr->name) &&
         expr->arguments.size() == 1;
}

void add_transition_fact(const std::string& name, const std::string& target, Expr value,
                         const Expr& transition, ProofContext& context) {
  auto equality = make_binary(BinaryOp::Equal, make_identifier(target, transition->range),
                              std::move(value), transition->range);
  context.active.push_back(NamedPredicate{name, equality, transition->location, transition->range});
}

void add_distinct_symbol_fact(const std::string& name, const std::string& left,
                              const std::string& right, const Expr& allocation,
                              ProofContext& context) {
  auto distinct = make_binary(BinaryOp::NotEqual, make_identifier(left, allocation->range),
                              make_identifier(right, allocation->range), allocation->range);
  context.active.push_back(NamedPredicate{name, distinct, allocation->location, allocation->range});
}

Expr make_constant_model_array(const Type& model_type, Expr initial, const SourceRange& range) {
  const auto& element_type = model_type.arguments.front();
  const auto call = element_type.kind == TypeKind::Bool ? kConstBoolArrayCall : kConstI64ArrayCall;
  return make_call(std::string(call), {std::move(initial)}, range);
}

void register_allocation_binding(const Expr& allocation, const std::string& target_symbol,
                                 const Type& type, const FunctionDecl& fn, ProofContext& context,
                                 int& call_index, const StructTable& structs,
                                 const FunctionTable& functions, const TheoremTable& theorems,
                                 std::vector<ProofObligation>& obligations) {
  if (!is_model_type(type) || !is_allocation_call(allocation)) {
    throw Diagnostic(allocation ? allocation->range : SourceRange{},
                     "allocation binding requires a memory model target");
  }

  const bool is_ref = is_ref_model_type(type);
  const std::size_t expected_arguments = is_ref ? 1 : 2;
  if (allocation->arguments.size() != expected_arguments) {
    throw Diagnostic(allocation->range, "invalid allocation constructor arity");
  }

  Expr length;
  if (!is_ref) {
    length = materialize_expr(allocation->arguments[0], fn, context, call_index, structs, functions,
                              theorems, obligations);
  }
  auto initial = materialize_expr(allocation->arguments[is_ref ? 0 : 1], fn, context, call_index,
                                  structs, functions, theorems, obligations);

  std::vector<std::string> previous_allocations(context.allocation_symbols.begin(),
                                                context.allocation_symbols.end());
  std::sort(previous_allocations.begin(), previous_allocations.end());
  std::vector<std::string> previous_references;
  previous_references.reserve(context.ref_symbols.size());
  for (const auto& [symbol, _] : context.ref_symbols) {
    previous_references.push_back(symbol);
  }
  std::sort(previous_references.begin(), previous_references.end());

  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);
  context.symbols[allocation_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
  context.symbols[allocation_live_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
  register_ownership_state(target_symbol, context);

  for (const auto& previous : previous_allocations) {
    add_distinct_symbol_fact(
        "allocate_fresh_" + sanitize_symbol(target_symbol) + "_" + sanitize_symbol(previous),
        allocation_symbol(target_symbol), allocation_symbol(previous), allocation, context);
  }

  add_transition_fact("allocate_" + allocation_live_symbol(target_symbol),
                      allocation_live_symbol(target_symbol), make_boolean(true, allocation->range),
                      allocation, context);
  add_transition_fact("allocate_" + has_owner_symbol(target_symbol),
                      has_owner_symbol(target_symbol), make_boolean(true, allocation->range),
                      allocation, context);
  add_transition_fact("allocate_" + shared_borrows_symbol(target_symbol),
                      shared_borrows_symbol(target_symbol), make_integer(0, allocation->range),
                      allocation, context);
  add_transition_fact("allocate_" + mut_borrow_symbol(target_symbol),
                      mut_borrow_symbol(target_symbol), make_boolean(false, allocation->range),
                      allocation, context);

  if (!is_ref) {
    const auto target_len = model_len_symbol(target_symbol);
    context.symbols[target_len] = Type{TypeKind::I64, "i64", {}};
    context.symbols[model_data_symbol(target_symbol)] = model_data_type(type);
    context.symbols[model_init_symbol(target_symbol)] = model_init_type();
    register_model_offset(target_symbol, type, context);
    auto len_non_negative =
        make_binary(BinaryOp::GreaterEqual, make_identifier(target_len, allocation->range),
                    make_integer(0, allocation->range), allocation->range);
    context.active.push_back(
        NamedPredicate{"model_" + sanitize_symbol(target_symbol) + "_len_non_negative",
                       len_non_negative, allocation->location, allocation->range});
    add_transition_fact("allocate_" + model_len_symbol(target_symbol),
                        model_len_symbol(target_symbol), length, allocation, context);
    add_transition_fact("allocate_" + model_offset_symbol(target_symbol),
                        model_offset_symbol(target_symbol), make_integer(0, allocation->range),
                        allocation, context);
    add_transition_fact("allocate_" + model_data_symbol(target_symbol),
                        model_data_symbol(target_symbol),
                        make_constant_model_array(type, std::move(initial), allocation->range),
                        allocation, context);
    const bool starts_initialized =
        allocation->name == "allocate_array" || allocation->name == "allocate_slice";
    add_transition_fact(
        "allocate_" + model_init_symbol(target_symbol), model_init_symbol(target_symbol),
        make_call(std::string(kConstBoolArrayCall),
                  {make_boolean(starts_initialized, allocation->range)}, allocation->range),
        allocation, context);
    return;
  }

  context.ref_symbols[target_symbol] = type;
  context.symbols[ref_addr_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
  context.symbols[ref_valid_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
  context.symbols[ref_value_symbol(target_symbol)] = type.arguments.front();
  context.symbols[ref_write_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
  context.symbols[ref_epoch_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
  for (const auto& previous : previous_references) {
    add_distinct_symbol_fact("allocate_address_fresh_" + sanitize_symbol(target_symbol) + "_" +
                                 sanitize_symbol(previous),
                             ref_addr_symbol(target_symbol), ref_addr_symbol(previous), allocation,
                             context);
  }
  add_transition_fact("allocate_" + ref_valid_symbol(target_symbol),
                      ref_valid_symbol(target_symbol), make_boolean(true, allocation->range),
                      allocation, context);
  add_transition_fact("allocate_" + ref_write_symbol(target_symbol),
                      ref_write_symbol(target_symbol), make_boolean(true, allocation->range),
                      allocation, context);
  add_transition_fact("allocate_" + ref_value_symbol(target_symbol),
                      ref_value_symbol(target_symbol), std::move(initial), allocation, context);
  add_transition_fact("allocate_" + ref_epoch_symbol(target_symbol),
                      ref_epoch_symbol(target_symbol), make_integer(0, allocation->range),
                      allocation, context);
  add_ref_alias_consistency_facts(target_symbol, type, allocation->range, allocation->location,
                                  context);
}

void register_slice_view_binding(const Expr& view, const std::string& target_symbol,
                                 const Type& type, const FunctionDecl& fn, ProofContext& context,
                                 int& call_index, const StructTable& structs,
                                 const FunctionTable& functions, const TheoremTable& theorems,
                                 std::vector<ProofObligation>& obligations) {
  if (!is_slice_model_type(type) || !is_slice_view_call(view)) {
    throw Diagnostic(view ? view->range : SourceRange{},
                     "slice_view binding requires a Slice[T] target");
  }
  const auto source = materialize_expr(view->arguments[0], fn, context, call_index, structs,
                                       functions, theorems, obligations);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(view->arguments[0]->range,
                     "slice_view source requires a materialized Slice value");
  }
  const auto start = materialize_expr(view->arguments[1], fn, context, call_index, structs,
                                      functions, theorems, obligations);
  const auto length = materialize_expr(view->arguments[2], fn, context, call_index, structs,
                                       functions, theorems, obligations);

  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);
  const auto target_len = model_len_symbol(target_symbol);
  const auto target_data = model_data_symbol(target_symbol);
  const auto target_init = model_init_symbol(target_symbol);
  const auto target_offset = model_offset_symbol(target_symbol);
  const auto target_allocation = allocation_symbol(target_symbol);
  const auto target_live = allocation_live_symbol(target_symbol);
  context.symbols[target_len] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_data] = model_data_type(type);
  context.symbols[target_init] = model_init_type();
  register_model_offset(target_symbol, type, context);
  context.symbols[target_allocation] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_live] = Type{TypeKind::Bool, "bool", {}};
  register_ownership_state(target_symbol, context);

  context.active.push_back(NamedPredicate{
      "model_" + sanitize_symbol(target_symbol) + "_len_non_negative",
      make_binary(BinaryOp::GreaterEqual, make_identifier(target_len), make_integer(0)),
      SourceLocation{}, SourceRange{}});
  add_transition_fact("view_" + target_len, target_len, length, view, context);
  add_symbol_equality_fact("view_" + target_data, target_data, model_data_symbol(source->name),
                           view->range, view->location, context);
  add_symbol_equality_fact("view_" + target_init, target_init, model_init_symbol(source->name),
                           view->range, view->location, context);
  auto absolute_offset =
      make_binary(BinaryOp::Add, make_identifier(model_offset_symbol(source->name), view->range),
                  start, view->range);
  add_transition_fact("view_" + target_offset, target_offset, absolute_offset, view, context);
  add_symbol_equality_fact("view_" + target_allocation, target_allocation,
                           allocation_symbol(source->name), view->range, view->location, context);
  add_symbol_equality_fact("view_" + target_live, target_live, allocation_live_symbol(source->name),
                           view->range, view->location, context);
  copy_ownership_state("view_", target_symbol, source->name, view->range, view->location, context);
}

void register_consuming_transition_binding(const Expr& transition, const std::string& target_symbol,
                                           const Type& type, const FunctionDecl& fn,
                                           ProofContext& context, int& call_index,
                                           const StructTable& structs,
                                           const FunctionTable& functions,
                                           const TheoremTable& theorems,
                                           std::vector<ProofObligation>& obligations) {
  if (!is_model_type(type) || !is_consuming_transition_call(transition)) {
    throw Diagnostic(transition ? transition->range : SourceRange{},
                     "consuming transition requires a memory model target");
  }
  const auto source = materialize_expr(transition->arguments[0], fn, context, call_index, structs,
                                       functions, theorems, obligations);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(transition->arguments[0]->range,
                     "consuming transition source requires a materialized memory model value");
  }

  if (transition->name == "move_owner") {
    register_model_alias(target_symbol, type, source, transition->range, transition->location,
                         context);
  } else {
    context.symbols[target_symbol] = type;
    context.allocation_symbols.insert(target_symbol);
    if (is_model_container_type(type)) {
      context.symbols[model_len_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
      context.symbols[model_data_symbol(target_symbol)] = model_data_type(type);
      context.symbols[model_init_symbol(target_symbol)] = model_init_type();
      register_model_offset(target_symbol, type, context);
      add_symbol_equality_fact("deallocate_" + model_len_symbol(target_symbol),
                               model_len_symbol(target_symbol), model_len_symbol(source->name),
                               transition->range, transition->location, context);
      add_symbol_equality_fact("deallocate_" + model_data_symbol(target_symbol),
                               model_data_symbol(target_symbol), model_data_symbol(source->name),
                               transition->range, transition->location, context);
      add_symbol_equality_fact("deallocate_" + model_init_symbol(target_symbol),
                               model_init_symbol(target_symbol), model_init_symbol(source->name),
                               transition->range, transition->location, context);
      add_symbol_equality_fact(
          "deallocate_" + model_offset_symbol(target_symbol), model_offset_symbol(target_symbol),
          model_offset_symbol(source->name), transition->range, transition->location, context);
    } else {
      context.symbols[ref_addr_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
      context.symbols[ref_valid_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
      context.symbols[ref_value_symbol(target_symbol)] = type.arguments.front();
      context.symbols[ref_write_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
      context.symbols[ref_epoch_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
      add_symbol_equality_fact("deallocate_" + ref_addr_symbol(target_symbol),
                               ref_addr_symbol(target_symbol), ref_addr_symbol(source->name),
                               transition->range, transition->location, context);
      add_symbol_equality_fact("deallocate_" + ref_value_symbol(target_symbol),
                               ref_value_symbol(target_symbol), ref_value_symbol(source->name),
                               transition->range, transition->location, context);
      add_transition_fact("deallocate_" + ref_valid_symbol(target_symbol),
                          ref_valid_symbol(target_symbol), make_boolean(false, transition->range),
                          transition, context);
      add_transition_fact("deallocate_" + ref_write_symbol(target_symbol),
                          ref_write_symbol(target_symbol), make_boolean(false, transition->range),
                          transition, context);
      auto next_epoch = make_binary(
          BinaryOp::Add, make_identifier(ref_epoch_symbol(source->name), transition->range),
          make_integer(1, transition->range), transition->range);
      add_transition_fact("deallocate_" + ref_epoch_symbol(target_symbol),
                          ref_epoch_symbol(target_symbol), next_epoch, transition, context);
    }

    context.symbols[allocation_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_live_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
    add_symbol_equality_fact("deallocate_" + allocation_symbol(target_symbol),
                             allocation_symbol(target_symbol), allocation_symbol(source->name),
                             transition->range, transition->location, context);
    add_transition_fact("deallocate_" + allocation_live_symbol(target_symbol),
                        allocation_live_symbol(target_symbol),
                        make_boolean(false, transition->range), transition, context);
    register_ownership_state(target_symbol, context);
    add_transition_fact("deallocate_" + owner_symbol(target_symbol), owner_symbol(target_symbol),
                        make_integer(0, transition->range), transition, context);
    add_transition_fact("deallocate_" + has_owner_symbol(target_symbol),
                        has_owner_symbol(target_symbol), make_boolean(false, transition->range),
                        transition, context);
    add_transition_fact("deallocate_" + shared_borrows_symbol(target_symbol),
                        shared_borrows_symbol(target_symbol), make_integer(0, transition->range),
                        transition, context);
    add_transition_fact("deallocate_" + mut_borrow_symbol(target_symbol),
                        mut_borrow_symbol(target_symbol), make_boolean(false, transition->range),
                        transition, context);
  }

  context.model_symbols.erase(source->name);
  context.ref_symbols.erase(source->name);
  if (transition->arguments[0]->kind == ExprNode::Kind::Identifier) {
    context.bindings.erase(transition->arguments[0]->name);
  }
}

void register_borrow_transition_binding(const Expr& transition, const std::string& target_symbol,
                                        const Type& type, const FunctionDecl& fn,
                                        ProofContext& context, int& call_index,
                                        const StructTable& structs, const FunctionTable& functions,
                                        const TheoremTable& theorems,
                                        std::vector<ProofObligation>& obligations) {
  if (!is_model_type(type) || !is_borrow_transition_call(transition)) {
    throw Diagnostic(transition ? transition->range : SourceRange{},
                     "borrow transition binding requires a memory model target");
  }
  const auto source = materialize_expr(transition->arguments[0], fn, context, call_index, structs,
                                       functions, theorems, obligations);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(transition->arguments[0]->range,
                     "borrow transition source requires a materialized memory model value");
  }

  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);
  if (is_model_container_type(type)) {
    context.symbols[model_len_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[model_data_symbol(target_symbol)] = model_data_type(type);
    context.symbols[model_init_symbol(target_symbol)] = model_init_type();
    register_model_offset(target_symbol, type, context);
    context.symbols[allocation_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_live_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
    add_symbol_equality_fact("transition_" + model_len_symbol(target_symbol),
                             model_len_symbol(target_symbol), model_len_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + model_data_symbol(target_symbol),
                             model_data_symbol(target_symbol), model_data_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + model_init_symbol(target_symbol),
                             model_init_symbol(target_symbol), model_init_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + model_offset_symbol(target_symbol),
                             model_offset_symbol(target_symbol), model_offset_symbol(source->name),
                             transition->range, transition->location, context);
  } else {
    context.ref_symbols[target_symbol] = type;
    context.symbols[ref_addr_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[ref_valid_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[ref_value_symbol(target_symbol)] = type.arguments.front();
    context.symbols[ref_write_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[ref_epoch_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_symbol(target_symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_live_symbol(target_symbol)] = Type{TypeKind::Bool, "bool", {}};
    add_symbol_equality_fact("transition_" + ref_addr_symbol(target_symbol),
                             ref_addr_symbol(target_symbol), ref_addr_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + ref_valid_symbol(target_symbol),
                             ref_valid_symbol(target_symbol), ref_valid_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + ref_value_symbol(target_symbol),
                             ref_value_symbol(target_symbol), ref_value_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + ref_write_symbol(target_symbol),
                             ref_write_symbol(target_symbol), ref_write_symbol(source->name),
                             transition->range, transition->location, context);
    add_symbol_equality_fact("transition_" + ref_epoch_symbol(target_symbol),
                             ref_epoch_symbol(target_symbol), ref_epoch_symbol(source->name),
                             transition->range, transition->location, context);
  }

  add_symbol_equality_fact("transition_" + allocation_symbol(target_symbol),
                           allocation_symbol(target_symbol), allocation_symbol(source->name),
                           transition->range, transition->location, context);
  add_symbol_equality_fact(
      "transition_" + allocation_live_symbol(target_symbol), allocation_live_symbol(target_symbol),
      allocation_live_symbol(source->name), transition->range, transition->location, context);
  register_ownership_state(target_symbol, context);
  add_symbol_equality_fact("transition_" + owner_symbol(target_symbol), owner_symbol(target_symbol),
                           owner_symbol(source->name), transition->range, transition->location,
                           context);
  add_symbol_equality_fact("transition_" + has_owner_symbol(target_symbol),
                           has_owner_symbol(target_symbol), has_owner_symbol(source->name),
                           transition->range, transition->location, context);

  Expr next_shared = make_identifier(shared_borrows_symbol(source->name), transition->range);
  if (transition->name == "borrow_shared") {
    next_shared = make_binary(BinaryOp::Add, next_shared, make_integer(1, transition->range),
                              transition->range);
  } else if (transition->name == "release_shared") {
    next_shared = make_binary(BinaryOp::Subtract, next_shared, make_integer(1, transition->range),
                              transition->range);
  }
  add_transition_fact("transition_" + shared_borrows_symbol(target_symbol),
                      shared_borrows_symbol(target_symbol), next_shared, transition, context);

  Expr next_mut = make_identifier(mut_borrow_symbol(source->name), transition->range);
  if (transition->name == "borrow_mut") {
    next_mut = make_boolean(true, transition->range);
  } else if (transition->name == "release_mut") {
    next_mut = make_boolean(false, transition->range);
  }
  add_transition_fact("transition_" + mut_borrow_symbol(target_symbol),
                      mut_borrow_symbol(target_symbol), next_mut, transition, context);
  if (is_ref_model_type(type)) {
    add_ref_alias_consistency_facts(target_symbol, type, transition->range, transition->location,
                                    context);
  }
}

void register_model_store_binding(const Expr& expr, const std::string& target_symbol,
                                  const Type& type, const FunctionDecl& fn, ProofContext& context,
                                  int& call_index, const StructTable& structs,
                                  const FunctionTable& functions, const TheoremTable& theorems,
                                  std::vector<ProofObligation>& obligations) {
  if (!is_model_container_type(type) || !is_model_container_store_call(expr)) {
    throw Diagnostic(expr ? expr->range : SourceRange{},
                     "store binding requires an Array[T] or Slice[T] target");
  }

  const auto source = materialize_expr(expr->arguments[0], fn, context, call_index, structs,
                                       functions, theorems, obligations);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(expr->arguments[0]->range,
                     "store source requires a materialized Array or Slice value");
  }

  const auto index = materialize_expr(expr->arguments[1], fn, context, call_index, structs,
                                      functions, theorems, obligations);
  const auto value = materialize_expr(expr->arguments[2], fn, context, call_index, structs,
                                      functions, theorems, obligations);

  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);
  const auto target_len = model_len_symbol(target_symbol);
  const auto target_data = model_data_symbol(target_symbol);
  const auto target_init = model_init_symbol(target_symbol);
  const auto target_offset = model_offset_symbol(target_symbol);
  const auto target_allocation = allocation_symbol(target_symbol);
  const auto target_live = allocation_live_symbol(target_symbol);
  const auto source_len = model_len_symbol(source->name);
  const auto source_data = model_data_symbol(source->name);
  const auto source_init = model_init_symbol(source->name);
  const auto source_offset = model_offset_symbol(source->name);
  const auto source_allocation = allocation_symbol(source->name);
  const auto source_live = allocation_live_symbol(source->name);

  context.symbols[target_len] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_data] = model_data_type(type);
  context.symbols[target_init] = model_init_type();
  register_model_offset(target_symbol, type, context);
  context.symbols[target_allocation] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_live] = Type{TypeKind::Bool, "bool", {}};
  register_ownership_state(target_symbol, context);

  auto len_non_negative =
      make_binary(BinaryOp::GreaterEqual, make_identifier(target_len, SourceLocation{}),
                  make_integer(0, SourceLocation{}), SourceLocation{});
  context.active.push_back(
      NamedPredicate{"model_" + sanitize_symbol(target_symbol) + "_len_non_negative",
                     len_non_negative, SourceLocation{}, SourceRange{}});
  add_symbol_equality_fact("store_" + target_len, target_len, source_len, expr->range,
                           expr->location, context);
  add_symbol_equality_fact("store_" + target_offset, target_offset, source_offset, expr->range,
                           expr->location, context);
  add_symbol_equality_fact("store_" + target_allocation, target_allocation, source_allocation,
                           expr->range, expr->location, context);
  add_symbol_equality_fact("store_" + target_live, target_live, source_live, expr->range,
                           expr->location, context);
  copy_ownership_state("store_", target_symbol, source->name, expr->range, expr->location, context);

  auto physical_index = make_binary(BinaryOp::Add, make_identifier(source_offset, expr->range),
                                    index, expr->arguments[1]->range);
  auto store_expr =
      make_call(std::string(kModelStoreCall),
                {make_identifier(source_data, expr->arguments[0]->range), physical_index, value},
                expr->range);
  auto data_equality = make_binary(BinaryOp::Equal, make_identifier(target_data, expr->range),
                                   store_expr, expr->range);
  context.active.push_back(
      NamedPredicate{"store_" + target_data, data_equality, expr->location, expr->range});
  auto init_store = make_call(std::string(kModelStoreCall),
                              {make_identifier(source_init, expr->arguments[0]->range),
                               physical_index, make_boolean(true, expr->arguments[1]->range)},
                              expr->range);
  auto init_equality = make_binary(BinaryOp::Equal, make_identifier(target_init, expr->range),
                                   init_store, expr->range);
  context.active.push_back(
      NamedPredicate{"store_" + target_init, init_equality, expr->location, expr->range});
}

void register_ref_store_binding(const Expr& expr, const std::string& target_symbol,
                                const Type& type, const FunctionDecl& fn, ProofContext& context,
                                int& call_index, const StructTable& structs,
                                const FunctionTable& functions, const TheoremTable& theorems,
                                std::vector<ProofObligation>& obligations) {
  if (!is_ref_model_type(type) || !is_ref_store_call(expr)) {
    throw Diagnostic(expr ? expr->range : SourceRange{}, "store binding requires a Ref[T] target");
  }

  const auto source = materialize_expr(expr->arguments[0], fn, context, call_index, structs,
                                       functions, theorems, obligations);
  if (!source || source->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(expr->arguments[0]->range, "store source requires a materialized Ref value");
  }

  const auto value = materialize_expr(expr->arguments[1], fn, context, call_index, structs,
                                      functions, theorems, obligations);

  context.symbols[target_symbol] = type;
  register_model_symbol(target_symbol, type, context);
  const auto target_addr = ref_addr_symbol(target_symbol);
  const auto target_valid = ref_valid_symbol(target_symbol);
  const auto target_value = ref_value_symbol(target_symbol);
  const auto target_write = ref_write_symbol(target_symbol);
  const auto target_epoch = ref_epoch_symbol(target_symbol);
  const auto target_allocation = allocation_symbol(target_symbol);
  const auto target_live = allocation_live_symbol(target_symbol);
  const auto source_addr = ref_addr_symbol(source->name);
  const auto source_valid = ref_valid_symbol(source->name);
  const auto source_write = ref_write_symbol(source->name);
  const auto source_epoch = ref_epoch_symbol(source->name);
  const auto source_allocation = allocation_symbol(source->name);
  const auto source_live = allocation_live_symbol(source->name);

  context.ref_symbols[target_symbol] = type;
  context.symbols[target_addr] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_valid] = Type{TypeKind::Bool, "bool", {}};
  context.symbols[target_value] = type.arguments.front();
  context.symbols[target_write] = Type{TypeKind::Bool, "bool", {}};
  context.symbols[target_epoch] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_allocation] = Type{TypeKind::I64, "i64", {}};
  context.symbols[target_live] = Type{TypeKind::Bool, "bool", {}};
  register_ownership_state(target_symbol, context);

  add_symbol_equality_fact("store_" + target_addr, target_addr, source_addr, expr->range,
                           expr->location, context);
  add_symbol_equality_fact("store_" + target_valid, target_valid, source_valid, expr->range,
                           expr->location, context);
  add_symbol_equality_fact("store_" + target_write, target_write, source_write, expr->range,
                           expr->location, context);
  add_symbol_equality_fact("store_" + target_allocation, target_allocation, source_allocation,
                           expr->range, expr->location, context);
  add_symbol_equality_fact("store_" + target_live, target_live, source_live, expr->range,
                           expr->location, context);
  copy_ownership_state("store_", target_symbol, source->name, expr->range, expr->location, context);

  auto value_equality =
      make_binary(BinaryOp::Equal, make_identifier(target_value, expr->range), value, expr->range);
  context.active.push_back(
      NamedPredicate{"store_" + target_value, value_equality, expr->location, expr->range});

  auto next_epoch = make_binary(BinaryOp::Add, make_identifier(source_epoch, expr->range),
                                make_integer(1, expr->range), expr->range);
  auto epoch_equality = make_binary(BinaryOp::Equal, make_identifier(target_epoch, expr->range),
                                    next_epoch, expr->range);
  context.active.push_back(
      NamedPredicate{"store_" + target_epoch, epoch_equality, expr->location, expr->range});
  add_ref_alias_consistency_facts(target_symbol, type, expr->range, expr->location, context);
}

void materialize_struct_binding(const Expr& expr, const std::string& target_symbol,
                                const FunctionDecl& fn, ProofContext& context, int& call_index,
                                const StructTable& structs, const FunctionTable& functions,
                                const TheoremTable& theorems,
                                std::vector<ProofObligation>& obligations) {
  const auto found = structs.find(expr->literal_type.spelling);
  if (found == structs.end()) {
    throw Diagnostic(expr->range, "unknown struct '" + expr->literal_type.spelling + "'");
  }
  const StructDecl& decl = *found->second;
  const auto type_substitutions = build_type_substitutions(decl, expr->literal_type);
  context.struct_types[target_symbol] = decl.name;

  for (const auto& initializer : expr->field_initializers) {
    const auto* field = find_field(decl, initializer.name);
    if (!field) {
      throw Diagnostic(initializer.range,
                       "struct '" + decl.name + "' has no field '" + initializer.name + "'");
    }

    const auto target_field = field_symbol(target_symbol, initializer.name);
    const auto expected_type = substitute_type(field->type, type_substitutions);
    if (is_struct_type(expected_type, structs) &&
        initializer.expr->kind == ExprNode::Kind::StructLiteral) {
      materialize_struct_binding(initializer.expr, target_field, fn, context, call_index, structs,
                                 functions, theorems, obligations);
      continue;
    }

    if (is_model_type(expected_type)) {
      if (is_allocation_call(initializer.expr)) {
        register_allocation_binding(initializer.expr, target_field, expected_type, fn, context,
                                    call_index, structs, functions, theorems, obligations);
        continue;
      }
      if (is_slice_model_type(expected_type) && is_slice_view_call(initializer.expr)) {
        register_slice_view_binding(initializer.expr, target_field, expected_type, fn, context,
                                    call_index, structs, functions, theorems, obligations);
        continue;
      }
      if (is_borrow_transition_call(initializer.expr)) {
        register_borrow_transition_binding(initializer.expr, target_field, expected_type, fn,
                                           context, call_index, structs, functions, theorems,
                                           obligations);
        continue;
      }
      if (is_model_container_type(expected_type) &&
          is_model_container_store_call(initializer.expr)) {
        register_model_store_binding(initializer.expr, target_field, expected_type, fn, context,
                                     call_index, structs, functions, theorems, obligations);
        continue;
      }
      if (is_ref_model_type(expected_type) && is_ref_store_call(initializer.expr)) {
        register_ref_store_binding(initializer.expr, target_field, expected_type, fn, context,
                                   call_index, structs, functions, theorems, obligations);
        continue;
      }
      const auto value = materialize_expr(initializer.expr, fn, context, call_index, structs,
                                          functions, theorems, obligations);
      register_model_alias(target_field, expected_type, value, initializer.range,
                           initializer.location, context);
      continue;
    }

    const auto value = materialize_expr(initializer.expr, fn, context, call_index, structs,
                                        functions, theorems, obligations);
    context.symbols[target_field] = expected_type;
    auto equality = make_binary(BinaryOp::Equal, make_identifier(target_field, initializer.range),
                                value, initializer.range);
    context.active.push_back(
        NamedPredicate{"field_" + target_field, equality, initializer.location, initializer.range});
  }

  append_struct_invariant_obligations(expr, target_symbol, decl, fn, context, call_index, structs,
                                      functions, theorems, obligations);
}

Expr materialize_struct_literal_expr(const Expr& expr, const FunctionDecl& fn,
                                     ProofContext& context, int& call_index,
                                     const StructTable& structs, const FunctionTable& functions,
                                     const TheoremTable& theorems,
                                     std::vector<ProofObligation>& obligations) {
  const auto symbol = scoped_symbol(expr->name, expr->location, "struct");
  materialize_struct_binding(expr, symbol, fn, context, call_index, structs, functions, theorems,
                             obligations);
  return make_identifier(symbol, expr->range);
}

Expr materialize_field_access_expr(const Expr& expr, const FunctionDecl& fn, ProofContext& context,
                                   int& call_index, const StructTable& structs,
                                   const FunctionTable& functions, const TheoremTable& theorems,
                                   std::vector<ProofObligation>& obligations) {
  const auto base = materialize_expr(expr->lhs, fn, context, call_index, structs, functions,
                                     theorems, obligations);
  if (!base || base->kind != ExprNode::Kind::Identifier) {
    throw Diagnostic(expr->range, "field access base could not be materialized");
  }

  const auto symbol = field_symbol(base->name, expr->name);
  if (context.symbols.find(symbol) != context.symbols.end() ||
      context.struct_types.find(symbol) != context.struct_types.end()) {
    return make_identifier(symbol, expr->range);
  }

  throw Diagnostic(expr->range, "unknown materialized field '" + display_expr(expr) + "'");
}

Expr materialize_expr(const Expr& expr, const FunctionDecl& fn, ProofContext& context,
                      int& call_index, const StructTable& structs, const FunctionTable& functions,
                      const TheoremTable& theorems, std::vector<ProofObligation>& obligations) {
  if (!expr) {
    return expr;
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return make_integer(expr->integer_value, expr->range);
  case ExprNode::Kind::Boolean:
    return make_boolean(expr->boolean_value, expr->range);
  case ExprNode::Kind::Identifier: {
    const auto found = context.bindings.find(expr->name);
    return make_identifier(found == context.bindings.end() ? expr->name : found->second,
                           expr->range);
  }
  case ExprNode::Kind::Call:
    return materialize_call_expr(expr, fn, context, call_index, structs, functions, theorems,
                                 obligations);
  case ExprNode::Kind::StructLiteral:
    return materialize_struct_literal_expr(expr, fn, context, call_index, structs, functions,
                                           theorems, obligations);
  case ExprNode::Kind::FieldAccess:
    return materialize_field_access_expr(expr, fn, context, call_index, structs, functions,
                                         theorems, obligations);
  case ExprNode::Kind::Unary:
    return make_unary(expr->unary_op,
                      materialize_expr(expr->lhs, fn, context, call_index, structs, functions,
                                       theorems, obligations),
                      expr->range);
  case ExprNode::Kind::Binary:
    return make_binary(expr->binary_op,
                       materialize_expr(expr->lhs, fn, context, call_index, structs, functions,
                                        theorems, obligations),
                       materialize_expr(expr->rhs, fn, context, call_index, structs, functions,
                                        theorems, obligations),
                       expr->range);
  case ExprNode::Kind::If:
    return make_if(materialize_expr(expr->condition, fn, context, call_index, structs, functions,
                                    theorems, obligations),
                   materialize_expr(expr->lhs, fn, context, call_index, structs, functions,
                                    theorems, obligations),
                   materialize_expr(expr->rhs, fn, context, call_index, structs, functions,
                                    theorems, obligations),
                   expr->range);
  }

  return expr;
}

void process_statements(const std::vector<Statement>& statements, const FunctionDecl& fn,
                        ProofContext& context, int& assert_index, int& return_index,
                        int& loop_index, int& safety_index, int& call_index,
                        const StructTable& structs, const FunctionTable& functions,
                        const TheoremTable& theorems, std::vector<ProofObligation>& obligations);

void preserve_branch_facts(const Expr& condition, const ProofContext& branch,
                           std::size_t fact_start, bool then_branch, ProofContext& target) {
  for (auto index = fact_start; index < branch.active.size(); ++index) {
    auto guarded = make_guarded_fact(condition, branch.active[index], then_branch);
    copy_symbols_for_expr(guarded.expr, branch, target);
    target.active.push_back(std::move(guarded));
  }
}

void append_new_returns(const ProofContext& from, std::size_t start,
                        std::vector<ProofContext::ReturnPath>& target) {
  for (auto index = start; index < from.returns.size(); ++index) {
    target.push_back(from.returns[index]);
  }
}

void merge_branch_bindings(const Statement& statement, const Expr& condition,
                           const ProofContext& then_context, const ProofContext& else_context,
                           ProofContext& context) {
  const auto before_bindings = context.bindings;
  for (const auto& [source_name, before_symbol] : before_bindings) {
    const auto then_found = then_context.bindings.find(source_name);
    const auto else_found = else_context.bindings.find(source_name);
    const auto then_symbol =
        then_found == then_context.bindings.end() ? before_symbol : then_found->second;
    const auto else_symbol =
        else_found == else_context.bindings.end() ? before_symbol : else_found->second;
    if (then_symbol == else_symbol) {
      context.bindings[source_name] = then_symbol;
      continue;
    }

    const auto join_symbol = scoped_symbol(source_name, statement.location, "join");
    copy_symbols_for_expr(make_identifier(then_symbol, statement.range), then_context, context);
    copy_symbols_for_expr(make_identifier(else_symbol, statement.range), else_context, context);
    const auto type = then_context.symbols.at(then_symbol);
    context.symbols[join_symbol] = type;
    context.bindings[source_name] = join_symbol;

    auto joined_value = make_if(condition, make_identifier(then_symbol, statement.range),
                                make_identifier(else_symbol, statement.range), statement.range);
    auto equality = make_binary(BinaryOp::Equal, make_identifier(join_symbol, statement.location),
                                joined_value, statement.location);
    context.active.push_back(
        NamedPredicate{"if_join_" + source_name, equality, statement.location, statement.range});
  }
}

void collect_assigned_names(const std::vector<Statement>& statements,
                            std::unordered_set<std::string>& names) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Assign) {
      names.insert(statement.name);
    } else if (statement.kind == StatementKind::If) {
      collect_assigned_names(statement.then_branch, names);
      collect_assigned_names(statement.else_branch, names);
    } else if (statement.kind == StatementKind::While) {
      collect_assigned_names(statement.then_branch, names);
    }
  }
}

std::unordered_set<std::string> assigned_outer_names(const Statement& statement,
                                                     const ProofContext& context) {
  std::unordered_set<std::string> assigned;
  collect_assigned_names(statement.then_branch, assigned);

  std::unordered_set<std::string> outer_names;
  for (const auto& name : assigned) {
    if (context.bindings.find(name) != context.bindings.end()) {
      outer_names.insert(name);
    }
  }
  return outer_names;
}

bool predicate_mentions_any_symbol(const NamedPredicate& predicate,
                                   const std::unordered_set<std::string>& symbols) {
  std::vector<std::string> identifiers;
  collect_identifiers(predicate.expr, identifiers);
  for (const auto& identifier : identifiers) {
    if (symbols.find(identifier) != symbols.end()) {
      return true;
    }
  }
  return false;
}

std::unordered_set<std::string>
current_symbols_for_names(const ProofContext& context,
                          const std::unordered_set<std::string>& names) {
  std::unordered_set<std::string> symbols;
  for (const auto& name : names) {
    const auto found = context.bindings.find(name);
    if (found != context.bindings.end()) {
      symbols.insert(found->second);
    }
  }
  return symbols;
}

std::vector<NamedPredicate>
filter_stale_mutable_facts(const ProofContext& context,
                           const std::unordered_set<std::string>& assigned_names) {
  const auto stale_symbols = current_symbols_for_names(context, assigned_names);
  std::vector<NamedPredicate> facts;
  facts.reserve(context.active.size());
  for (const auto& predicate : context.active) {
    if (!predicate_mentions_any_symbol(predicate, stale_symbols)) {
      facts.push_back(predicate);
    }
  }
  return facts;
}

void rebind_loop_state(const Statement& statement, ProofContext& context,
                       const std::unordered_set<std::string>& assigned_names,
                       const std::string& purpose) {
  for (const auto& name : assigned_names) {
    const auto current = context.bindings.at(name);
    const auto symbol = scoped_symbol(name, statement.location, purpose);
    context.symbols[symbol] = context.symbols.at(current);
    context.bindings[name] = symbol;
  }
}

ProofObligation make_loop_obligation(const FunctionDecl& fn, const Statement& statement,
                                     int loop_index, int invariant_index, const std::string& phase,
                                     const NamedPredicate& invariant, const ProofContext& context) {
  ProofObligation obligation;
  obligation.name = proof_subject_name(fn) + ".loop." + std::to_string(loop_index) + ".invariant." +
                    std::to_string(invariant_index) + "." + invariant.name + "." + phase;
  obligation.location = invariant.location;
  obligation.range = invariant.range.start.line == 0 ? statement.range : invariant.range;
  obligation.assumptions = context.active;
  obligation.goal = rewrite_predicate(invariant, context.bindings);
  obligation.symbols = context.symbols;
  return obligation;
}

void process_if_statement(const Statement& statement, const FunctionDecl& fn, ProofContext& context,
                          int& assert_index, int& return_index, int& loop_index, int& safety_index,
                          int& call_index, const StructTable& structs,
                          const FunctionTable& functions, const TheoremTable& theorems,
                          std::vector<ProofObligation>& obligations) {
  append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
  const auto condition = materialize_expr(statement.expr, fn, context, call_index, structs,
                                          functions, theorems, obligations);
  const auto return_start = context.returns.size();
  const auto parent_scope_depth = context.scope_depth;

  auto then_context = context;
  then_context.scope_depth = context.scope_depth + 1;
  then_context.active.push_back(make_branch_condition(condition, true));
  const auto then_fact_start = then_context.active.size();
  process_statements(statement.then_branch, fn, then_context, assert_index, return_index,
                     loop_index, safety_index, call_index, structs, functions, theorems,
                     obligations);

  auto else_context = context;
  else_context.scope_depth = context.scope_depth + 1;
  else_context.active.push_back(make_branch_condition(condition, false));
  const auto else_fact_start = else_context.active.size();
  process_statements(statement.else_branch, fn, else_context, assert_index, return_index,
                     loop_index, safety_index, call_index, structs, functions, theorems,
                     obligations);

  std::vector<ProofContext::ReturnPath> merged_returns = context.returns;
  append_new_returns(then_context, return_start, merged_returns);
  append_new_returns(else_context, return_start, merged_returns);

  if (!then_context.terminated && !else_context.terminated) {
    preserve_branch_facts(condition, then_context, then_fact_start, true, context);
    preserve_branch_facts(condition, else_context, else_fact_start, false, context);
    merge_branch_bindings(statement, condition, then_context, else_context, context);
    context.returns = std::move(merged_returns);
    return;
  }

  if (!then_context.terminated) {
    context = std::move(then_context);
    context.scope_depth = parent_scope_depth;
    context.returns = std::move(merged_returns);
    return;
  }

  if (!else_context.terminated) {
    context = std::move(else_context);
    context.scope_depth = parent_scope_depth;
    context.returns = std::move(merged_returns);
    return;
  }

  context.returns = std::move(merged_returns);
  context.terminated = true;
}

void process_while_statement(const Statement& statement, const FunctionDecl& fn,
                             ProofContext& context, int& assert_index, int& return_index,
                             int& loop_index, int& safety_index, int& call_index,
                             const StructTable& structs, const FunctionTable& functions,
                             const TheoremTable& theorems,
                             std::vector<ProofObligation>& obligations) {
  ++loop_index;
  const auto current_loop = loop_index;

  append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
  int invariant_index = 0;
  for (const auto& invariant : statement.loop_invariants) {
    append_expression_safety_obligations(invariant.expr, fn, context, safety_index, obligations);
    ++invariant_index;
    obligations.push_back(make_loop_obligation(fn, statement, current_loop, invariant_index,
                                               "initial", invariant, context));
  }

  const auto assigned_names = assigned_outer_names(statement, context);
  const auto return_start = context.returns.size();

  ProofContext head_context = context;
  head_context.active = filter_stale_mutable_facts(context, assigned_names);
  rebind_loop_state(statement, head_context, assigned_names, "loop");
  for (const auto& invariant : statement.loop_invariants) {
    head_context.active.push_back(rewrite_predicate(invariant, head_context.bindings));
  }
  append_expression_safety_obligations(statement.expr, fn, head_context, safety_index, obligations);
  head_context.active.push_back(
      NamedPredicate{"while_condition",
                     materialize_expr(statement.expr, fn, head_context, call_index, structs,
                                      functions, theorems, obligations),
                     statement.location, statement.expr ? statement.expr->range : statement.range});

  auto body_context = head_context;
  body_context.scope_depth = context.scope_depth + 1;
  process_statements(statement.then_branch, fn, body_context, assert_index, return_index,
                     loop_index, safety_index, call_index, structs, functions, theorems,
                     obligations);

  if (!body_context.terminated) {
    invariant_index = 0;
    for (const auto& invariant : statement.loop_invariants) {
      append_expression_safety_obligations(invariant.expr, fn, body_context, safety_index,
                                           obligations);
      ++invariant_index;
      obligations.push_back(make_loop_obligation(fn, statement, current_loop, invariant_index,
                                                 "preserved", invariant, body_context));
    }
  }

  context.active = filter_stale_mutable_facts(context, assigned_names);
  append_new_returns(body_context, return_start, context.returns);
  rebind_loop_state(statement, context, assigned_names, "loop_exit");
  for (const auto& invariant : statement.loop_invariants) {
    auto summary = rewrite_predicate(invariant, context.bindings);
    summary.name = "loop_summary." + invariant.name;
    context.active.push_back(std::move(summary));
  }
  auto exit_condition = make_unary(UnaryOp::Not,
                                   materialize_expr(statement.expr, fn, context, call_index,
                                                    structs, functions, theorems, obligations),
                                   statement.range);
  context.active.push_back(
      NamedPredicate{"while_exit", exit_condition, statement.location, statement.range});
}

void process_statement(const Statement& statement, const FunctionDecl& fn, ProofContext& context,
                       int& assert_index, int& return_index, int& loop_index, int& safety_index,
                       int& call_index, const StructTable& structs, const FunctionTable& functions,
                       const TheoremTable& theorems, std::vector<ProofObligation>& obligations) {
  if (statement.kind == StatementKind::Let) {
    append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
    const auto symbol = context.scope_depth == 0
                            ? statement.name
                            : scoped_symbol(statement.name, statement.location, "local");
    context.bindings[statement.name] = symbol;
    if (is_struct_type(statement.type, structs) &&
        statement.expr->kind == ExprNode::Kind::StructLiteral) {
      materialize_struct_binding(statement.expr, symbol, fn, context, call_index, structs,
                                 functions, theorems, obligations);
      return;
    }

    if (is_model_type(statement.type) && is_allocation_call(statement.expr)) {
      register_allocation_binding(statement.expr, symbol, statement.type, fn, context, call_index,
                                  structs, functions, theorems, obligations);
      return;
    }

    if (is_model_type(statement.type) && is_consuming_transition_call(statement.expr)) {
      register_consuming_transition_binding(statement.expr, symbol, statement.type, fn, context,
                                            call_index, structs, functions, theorems, obligations);
      return;
    }

    if (is_model_type(statement.type) && is_borrow_transition_call(statement.expr)) {
      register_borrow_transition_binding(statement.expr, symbol, statement.type, fn, context,
                                         call_index, structs, functions, theorems, obligations);
      return;
    }

    if (is_slice_model_type(statement.type) && is_slice_view_call(statement.expr)) {
      register_slice_view_binding(statement.expr, symbol, statement.type, fn, context, call_index,
                                  structs, functions, theorems, obligations);
      return;
    }

    if (is_model_container_type(statement.type) && is_model_container_store_call(statement.expr)) {
      register_model_store_binding(statement.expr, symbol, statement.type, fn, context, call_index,
                                   structs, functions, theorems, obligations);
      return;
    }

    if (is_ref_model_type(statement.type) && is_ref_store_call(statement.expr)) {
      register_ref_store_binding(statement.expr, symbol, statement.type, fn, context, call_index,
                                 structs, functions, theorems, obligations);
      return;
    }

    const auto value = materialize_expr(statement.expr, fn, context, call_index, structs, functions,
                                        theorems, obligations);
    if (is_model_type(statement.type)) {
      register_model_alias(symbol, statement.type, value, statement.range, statement.location,
                           context);
      return;
    }

    context.symbols[symbol] = statement.type;
    auto equality = make_binary(BinaryOp::Equal, make_identifier(symbol, statement.location), value,
                                statement.location);
    context.active.push_back(
        NamedPredicate{"let_" + statement.name, equality, statement.location, statement.range});
  } else if (statement.kind == StatementKind::Assign) {
    append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
    const auto current = context.bindings.at(statement.name);
    const auto value = materialize_expr(statement.expr, fn, context, call_index, structs, functions,
                                        theorems, obligations);
    const auto symbol = scoped_symbol(statement.name, statement.location, "assign");
    context.symbols[symbol] = context.symbols.at(current);
    context.bindings[statement.name] = symbol;
    auto equality = make_binary(BinaryOp::Equal, make_identifier(symbol, statement.location), value,
                                statement.location);
    context.active.push_back(
        NamedPredicate{"assign_" + statement.name, equality, statement.location, statement.range});
  } else if (statement.kind == StatementKind::If) {
    process_if_statement(statement, fn, context, assert_index, return_index, loop_index,
                         safety_index, call_index, structs, functions, theorems, obligations);
  } else if (statement.kind == StatementKind::While) {
    process_while_statement(statement, fn, context, assert_index, return_index, loop_index,
                            safety_index, call_index, structs, functions, theorems, obligations);
  } else if (statement.kind == StatementKind::Assume) {
    append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
    const auto value = materialize_expr(statement.expr, fn, context, call_index, structs, functions,
                                        theorems, obligations);
    context.active.push_back(
        NamedPredicate{statement.name, value, statement.location, statement.range});
  } else if (statement.kind == StatementKind::Assert) {
    append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
    const auto value = materialize_expr(statement.expr, fn, context, call_index, structs, functions,
                                        theorems, obligations);
    ++assert_index;
    ProofObligation obligation;
    obligation.name =
        proof_subject_name(fn) + ".assert." + std::to_string(assert_index) + "." + statement.name;
    obligation.location = statement.location;
    obligation.range = statement.range;
    obligation.assumptions = context.active;
    obligation.goal = NamedPredicate{statement.name, value, statement.location, statement.range};
    obligation.symbols = context.symbols;
    obligations.push_back(std::move(obligation));
    context.active.push_back(
        NamedPredicate{statement.name, value, statement.location, statement.range});
  } else if (statement.kind == StatementKind::Return) {
    Expr value;
    if (statement.expr) {
      append_expression_safety_obligations(statement.expr, fn, context, safety_index, obligations);
      value = materialize_expr(statement.expr, fn, context, call_index, structs, functions,
                               theorems, obligations);
    }
    ++return_index;
    const auto return_name = "return_" + std::to_string(return_index);
    auto assumptions = context.active;
    if (fn.return_type.kind != TypeKind::Void && statement.expr) {
      auto equality = make_binary(
          BinaryOp::Equal, make_identifier(context.bindings.at("result"), statement.location),
          value, statement.location);
      assumptions.push_back(
          NamedPredicate{return_name, equality, statement.location, statement.range});
    }
    context.returns.push_back(ProofContext::ReturnPath{return_index, context.symbols,
                                                       context.bindings, std::move(assumptions)});
    context.terminated = true;
  }
}

void process_statements(const std::vector<Statement>& statements, const FunctionDecl& fn,
                        ProofContext& context, int& assert_index, int& return_index,
                        int& loop_index, int& safety_index, int& call_index,
                        const StructTable& structs, const FunctionTable& functions,
                        const TheoremTable& theorems, std::vector<ProofObligation>& obligations) {
  for (const auto& statement : statements) {
    if (context.terminated) {
      break;
    }
    process_statement(statement, fn, context, assert_index, return_index, loop_index, safety_index,
                      call_index, structs, functions, theorems, obligations);
  }
}

std::string ensure_obligation_name(const FunctionDecl& fn, int ensure_index,
                                   const NamedPredicate& ensure,
                                   const ProofContext::ReturnPath* return_path,
                                   std::size_t path_count, bool fallthrough_path) {
  if (path_count <= 1) {
    return proof_subject_name(fn) + ".ensures." + std::to_string(ensure_index) + "." + ensure.name;
  }
  if (fallthrough_path) {
    return proof_subject_name(fn) + ".fallthrough.ensures." + std::to_string(ensure_index) + "." +
           ensure.name;
  }
  if (return_path == nullptr) {
    return proof_subject_name(fn) + ".ensures." + std::to_string(ensure_index) + "." + ensure.name;
  }
  return proof_subject_name(fn) + ".return." + std::to_string(return_path->index) + ".ensures." +
         std::to_string(ensure_index) + "." + ensure.name;
}

void append_ensure_obligation(const FunctionDecl& fn, const NamedPredicate& ensure,
                              int ensure_index, const std::vector<NamedPredicate>& assumptions,
                              const SymbolTable& symbols,
                              const std::unordered_map<std::string, std::string>& bindings,
                              const ProofContext::ReturnPath* return_path, std::size_t path_count,
                              bool fallthrough_path, int& safety_index,
                              std::vector<ProofObligation>& obligations) {
  ProofContext ensure_context;
  ensure_context.symbols = symbols;
  ensure_context.bindings = bindings;
  ensure_context.active = assumptions;
  append_expression_safety_obligations(ensure.expr, fn, ensure_context, safety_index, obligations);

  ProofObligation obligation;
  obligation.name =
      ensure_obligation_name(fn, ensure_index, ensure, return_path, path_count, fallthrough_path);
  obligation.location = ensure.location;
  obligation.range = ensure.range;
  obligation.assumptions = assumptions;
  obligation.goal = rewrite_predicate(ensure, bindings);
  obligation.symbols = symbols;
  obligations.push_back(std::move(obligation));
}

FunctionTable build_function_table(const Module& module) {
  FunctionTable functions;
  for (const auto& fn : module.functions) {
    functions[fn.name] = &fn;
  }
  return functions;
}

NamedPredicate theorem_holds_predicate(const TheoremDecl& theorem) {
  auto result = make_identifier("result", theorem.range);
  auto truth = make_boolean(true, theorem.range);
  auto expr = make_binary(BinaryOp::Equal, result, truth, theorem.range);
  return NamedPredicate{"holds", expr, theorem.location, theorem.range};
}

std::vector<FunctionDecl> build_theorem_proof_functions(const Module& module) {
  std::vector<FunctionDecl> proof_functions;
  proof_functions.reserve(module.theorems.size());
  for (const auto& theorem : module.theorems) {
    FunctionDecl fn;
    fn.name = std::string(kTheoremProofPrefix) + theorem.name;
    fn.params = theorem.params;
    fn.return_type = Type{TypeKind::Bool, "bool", {}};
    fn.preconditions = theorem.preconditions;
    fn.ensures = theorem.ensures;
    fn.ensures.push_back(theorem_holds_predicate(theorem));
    fn.body = theorem.body;
    fn.location = theorem.location;
    fn.range = theorem.range;
    proof_functions.push_back(std::move(fn));
  }
  return proof_functions;
}

TheoremTable build_theorem_table(const Module& module,
                                 const std::vector<FunctionDecl>& proof_functions) {
  TheoremTable theorems;
  for (std::size_t index = 0; index < module.theorems.size() && index < proof_functions.size();
       ++index) {
    theorems[module.theorems[index].name] = &proof_functions[index];
  }
  return theorems;
}

StructTable build_struct_table(const Module& module) {
  StructTable structs;
  for (const auto& decl : module.structs) {
    structs[decl.name] = &decl;
  }
  return structs;
}

void register_struct_value(const std::string& symbol, const Type& type, const StructTable& structs,
                           ProofContext& context) {
  if (is_model_container_type(type)) {
    register_model_symbol(symbol, type, context);
    const auto len_symbol = model_len_symbol(symbol);
    context.symbols[len_symbol] = Type{TypeKind::I64, "i64", {}};
    context.symbols[model_data_symbol(symbol)] = model_data_type(type);
    context.symbols[model_init_symbol(symbol)] = model_init_type();
    register_model_offset(symbol, type, context);
    context.symbols[allocation_symbol(symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_live_symbol(symbol)] = Type{TypeKind::Bool, "bool", {}};
    register_ownership_state(symbol, context);

    auto len_non_negative =
        make_binary(BinaryOp::GreaterEqual, make_identifier(len_symbol, SourceLocation{}),
                    make_integer(0, SourceLocation{}), SourceLocation{});
    context.active.push_back(
        NamedPredicate{"model_" + sanitize_symbol(symbol) + "_len_non_negative", len_non_negative,
                       SourceLocation{}, SourceRange{}});
    return;
  }

  if (is_ref_model_type(type)) {
    register_model_symbol(symbol, type, context);
    context.ref_symbols[symbol] = type;
    context.symbols[ref_addr_symbol(symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[ref_valid_symbol(symbol)] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[ref_value_symbol(symbol)] = type.arguments.front();
    context.symbols[ref_write_symbol(symbol)] = Type{TypeKind::Bool, "bool", {}};
    context.symbols[allocation_symbol(symbol)] = Type{TypeKind::I64, "i64", {}};
    context.symbols[allocation_live_symbol(symbol)] = Type{TypeKind::Bool, "bool", {}};
    register_ownership_state(symbol, context);
    const auto epoch_symbol = ref_epoch_symbol(symbol);
    context.symbols[epoch_symbol] = Type{TypeKind::I64, "i64", {}};
    add_symbol_equality_fact("ref_" + sanitize_symbol(symbol) + "_entry_epoch", epoch_symbol,
                             std::string(kEntryEpochSymbol), SourceRange{}, SourceLocation{},
                             context);
    add_ref_alias_consistency_facts(symbol, type, SourceRange{}, SourceLocation{}, context);
    return;
  }

  if (!is_struct_type(type, structs)) {
    context.symbols[symbol] = type;
    return;
  }

  const StructDecl& decl = *structs.at(type.spelling);
  const auto type_substitutions = build_type_substitutions(decl, type);
  context.struct_types[symbol] = decl.name;
  for (const auto& field : decl.fields) {
    register_struct_value(field_symbol(symbol, field.name),
                          substitute_type(field.type, type_substitutions), structs, context);
  }
}

void append_function_obligations(const FunctionDecl& fn, const StructTable& structs,
                                 const FunctionTable& functions, const TheoremTable& theorems,
                                 std::vector<ProofObligation>& obligations) {
  ProofContext context;
  for (const auto& param : fn.params) {
    context.bindings[param.name] = param.name;
    register_struct_value(param.name, param.type, structs, context);
  }
  if (fn.return_type.kind != TypeKind::Void) {
    context.bindings["result"] = "result";
    register_struct_value("result", fn.return_type, structs, context);
  }

  int safety_index = 0;
  for (const auto& precondition : fn.preconditions) {
    append_expression_safety_obligations(precondition.expr, fn, context, safety_index, obligations);
    context.active.push_back(rewrite_predicate(precondition, context.bindings));
  }
  int assert_index = 0;
  int return_index = 0;
  int loop_index = 0;
  int call_index = 0;
  process_statements(fn.body, fn, context, assert_index, return_index, loop_index, safety_index,
                     call_index, structs, functions, theorems, obligations);

  const bool has_void_fallthrough = fn.return_type.kind == TypeKind::Void && !context.terminated;
  const auto path_count = context.returns.size() + (has_void_fallthrough ? 1U : 0U);

  if (!context.returns.empty()) {
    for (const auto& return_path : context.returns) {
      int ensure_index = 0;
      for (const auto& ensure : fn.ensures) {
        ++ensure_index;
        append_ensure_obligation(fn, ensure, ensure_index, return_path.assumptions,
                                 return_path.symbols, return_path.bindings, &return_path,
                                 path_count, false, safety_index, obligations);
      }
    }
  }
  if (context.returns.empty() || has_void_fallthrough) {
    int ensure_index = 0;
    for (const auto& ensure : fn.ensures) {
      ++ensure_index;
      append_ensure_obligation(fn, ensure, ensure_index, context.active, context.symbols,
                               context.bindings, nullptr, path_count, has_void_fallthrough,
                               safety_index, obligations);
    }
  }
}

} // namespace

std::vector<ProofObligation> build_obligations(const Module& module) {
  std::vector<ProofObligation> obligations;
  const auto functions = build_function_table(module);
  const auto structs = build_struct_table(module);
  const auto theorem_functions = build_theorem_proof_functions(module);
  const auto theorems = build_theorem_table(module, theorem_functions);
  for (const auto& theorem : theorem_functions) {
    append_function_obligations(theorem, structs, functions, theorems, obligations);
  }
  for (const auto& fn : module.functions) {
    append_function_obligations(fn, structs, functions, theorems, obligations);
  }
  return obligations;
}

std::string emit_smt_lib(const ProofObligation& obligation, int solver_timeout_ms) {
  SymbolTable symbols = obligation.symbols;
  std::vector<std::string> identifiers;
  for (const auto& assumption : obligation.assumptions) {
    collect_identifiers(assumption.expr, identifiers);
  }
  collect_identifiers(obligation.goal.expr, identifiers);
  for (const auto& identifier : identifiers) {
    if (symbols.find(identifier) == symbols.end()) {
      symbols[identifier] = Type{TypeKind::Unknown, "i64", {}};
    }
  }

  std::ostringstream out;
  if (solver_timeout_ms > 0) {
    out << "(set-option :timeout " << solver_timeout_ms << ")\n";
  }
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

std::string emit_smt_lib(const ProofObligation& obligation) {
  return emit_smt_lib(obligation, 0);
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

std::string proof_hint_file_name_for_obligation(const std::string& obligation_name) {
  auto file_name = smt_file_name_for_obligation(obligation_name);
  constexpr std::string_view smt_suffix = ".smt2";
  if (file_name.size() >= smt_suffix.size()) {
    file_name.resize(file_name.size() - smt_suffix.size());
  }
  file_name += ".proof-hint.txt";
  return file_name;
}

std::string base_file_name_for_obligation(const std::string& obligation_name) {
  auto file_name = smt_file_name_for_obligation(obligation_name);
  constexpr std::string_view smt_suffix = ".smt2";
  if (file_name.size() >= smt_suffix.size()) {
    file_name.resize(file_name.size() - smt_suffix.size());
  }
  return file_name;
}

std::string agent_request_file_name_for_obligation(const std::string& obligation_name) {
  return base_file_name_for_obligation(obligation_name) + ".agent-request.txt";
}

std::string theorem_candidate_file_name_for_obligation(const std::string& obligation_name) {
  return base_file_name_for_obligation(obligation_name) + ".candidate.sigil";
}

std::string render_source_counterexample(const ProofObligation& obligation,
                                         const std::string& z3_model) {
  const auto values = parse_model_values(z3_model);
  if (values.empty()) {
    return "";
  }

  std::ostringstream out;
  for (const auto& name : source_symbol_order(obligation)) {
    const auto symbol = sanitize_symbol(name);
    const auto value = values.find(symbol);
    const auto type = obligation.symbols.find(name);
    if (value == values.end() || type == obligation.symbols.end()) {
      continue;
    }
    out << name << ": " << type->second.display() << " = " << value->second << "\n";
  }
  return out.str();
}

namespace {

void append_indented_block(std::ostringstream& out, const std::string& block,
                           const std::string& indent) {
  std::istringstream lines(block);
  std::string line;
  while (std::getline(lines, line)) {
    out << indent << line << "\n";
  }
}

std::string theorem_candidate_identifier(const std::string& obligation_name) {
  std::string identifier = "candidate_";
  for (const char c : obligation_name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      identifier += c;
    } else {
      identifier += "_";
    }
  }
  return identifier;
}

std::string render_proof_hint_artifact(const ProofObligation& obligation,
                                       const VerificationResult& result) {
  std::ostringstream out;
  out << "sigil-proof-hint-v1\n";
  out << "obligation: " << obligation.name << "\n";
  out << "status: " << status_name(result.status) << "\n";
  out << "details: " << result.details << "\n";
  out << "range: " << obligation.range.display() << "\n";
  if (!result.smt_path.empty()) {
    out << "smt-path: " << result.smt_path << "\n";
  }
  out << "\n";

  out << "goal:\n";
  out << "  " << obligation.goal.name << ": " << display_expr(obligation.goal.expr) << "\n";
  out << "\n";

  out << "assumptions:\n";
  if (obligation.assumptions.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto& assumption : obligation.assumptions) {
      out << "  - " << assumption.name << ": " << display_expr(assumption.expr) << "\n";
    }
  }
  out << "\n";

  out << "symbols:\n";
  bool wrote_symbol = false;
  for (const auto& name : source_symbol_order(obligation)) {
    const auto found = obligation.symbols.find(name);
    if (found == obligation.symbols.end()) {
      continue;
    }
    wrote_symbol = true;
    out << "  - " << name << ": " << found->second.display() << "\n";
  }
  if (!wrote_symbol) {
    out << "  (none)\n";
  }
  out << "\n";

  if (!result.counterexample.empty()) {
    out << "counterexample:\n";
    append_indented_block(out, result.counterexample, "  ");
    out << "\n";
  }

  out << "agent-contract:\n";
  out << "  - propose helper predicates in Sigil source syntax only\n";
  out << "  - every proposal must be rechecked by Sigil and Z3 before it can affect compilation\n";
  out << "  - do not treat this artifact as a proof certificate\n";
  out << "\n";

  out << "smt-lib:\n";
  append_indented_block(out, result.smt_lib, "  ");
  return out.str();
}

std::string render_agent_request_artifact(const ProofObligation& obligation,
                                          const VerificationResult& result) {
  std::ostringstream out;
  out << "sigil-agent-request-v1\n";
  out << "obligation: " << obligation.name << "\n";
  out << "status: " << status_name(result.status) << "\n";
  out << "details: " << result.details << "\n";
  out << "range: " << obligation.range.display() << "\n";
  out << "proof-hint-file: " << proof_hint_file_name_for_obligation(obligation.name) << "\n";
  out << "candidate-file: " << theorem_candidate_file_name_for_obligation(obligation.name) << "\n";
  out << "\n";

  out << "objective:\n";
  out << "  Propose one or more Sigil theorem declarations, assumes, or stronger source\n";
  out << "  preconditions that could discharge this obligation after deterministic\n";
  out << "  checking. Do not claim success unless Sigil and Z3 prove the edited source.\n";
  out << "\n";

  out << "acceptance-gate:\n";
  out << "  - Candidate text must be valid Sigil source or a patch to valid Sigil source.\n";
  out << "  - Candidate lemmas must be represented as theorem declarations.\n";
  out << "  - The original module must be rechecked with sigil check --strict.\n";
  out << "  - This request is not a proof certificate.\n";
  out << "\n";

  out << "goal:\n";
  out << "  " << obligation.goal.name << ": " << display_expr(obligation.goal.expr) << "\n";
  out << "\n";

  out << "assumptions:\n";
  if (obligation.assumptions.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto& assumption : obligation.assumptions) {
      out << "  - " << assumption.name << ": " << display_expr(assumption.expr) << "\n";
    }
  }
  out << "\n";

  out << "symbols:\n";
  bool wrote_symbol = false;
  for (const auto& name : source_symbol_order(obligation)) {
    const auto found = obligation.symbols.find(name);
    if (found == obligation.symbols.end()) {
      continue;
    }
    wrote_symbol = true;
    out << "  - " << name << ": " << found->second.display() << "\n";
  }
  if (!wrote_symbol) {
    out << "  (none)\n";
  }
  out << "\n";

  out << "smt-lib:\n";
  append_indented_block(out, result.smt_lib, "  ");
  return out.str();
}

std::string render_theorem_candidate_artifact(const ProofObligation& obligation) {
  const auto identifier = theorem_candidate_identifier(obligation.name);
  std::ostringstream out;
  out << "// sigil-theorem-candidate-v1\n";
  out << "// source-obligation: " << obligation.name << "\n";
  out << "// This file is a scaffold for an agent or human. It is intentionally\n";
  out << "// not a valid proof of the source obligation until the placeholder\n";
  out << "// theorem is replaced with a checked lemma and wired back into source.\n";
  out << "module " << identifier << ";\n\n";
  out << "theorem " << identifier << " for ()\n";
  out << "ensures placeholder: false;\n";
  out << "{\n";
  out << "  return false;\n";
  out << "}\n";
  return out.str();
}

} // namespace

std::vector<ProofHintArtifact>
build_proof_hint_artifacts(const std::vector<ProofObligation>& obligations,
                           const std::vector<VerificationResult>& results) {
  if (obligations.size() != results.size()) {
    throw std::invalid_argument("proof hint artifacts require matching obligations and results");
  }

  std::vector<ProofHintArtifact> artifacts;
  for (std::size_t index = 0; index < obligations.size(); ++index) {
    const auto& result = results[index];
    if (result.status == VerificationStatus::Proven) {
      continue;
    }
    artifacts.push_back(
        ProofHintArtifact{proof_hint_file_name_for_obligation(obligations[index].name),
                          render_proof_hint_artifact(obligations[index], result)});
  }
  return artifacts;
}

std::vector<AgentHandoffArtifact>
build_agent_handoff_artifacts(const std::vector<ProofObligation>& obligations,
                              const std::vector<VerificationResult>& results) {
  if (obligations.size() != results.size()) {
    throw std::invalid_argument("agent handoff artifacts require matching obligations and results");
  }

  std::vector<AgentHandoffArtifact> artifacts;
  for (std::size_t index = 0; index < obligations.size(); ++index) {
    const auto& result = results[index];
    if (result.status == VerificationStatus::Proven) {
      continue;
    }
    artifacts.push_back(AgentHandoffArtifact{
        "agent-request", agent_request_file_name_for_obligation(obligations[index].name),
        render_agent_request_artifact(obligations[index], result)});
    artifacts.push_back(AgentHandoffArtifact{
        "theorem-candidate", theorem_candidate_file_name_for_obligation(obligations[index].name),
        render_theorem_candidate_artifact(obligations[index])});
  }
  return artifacts;
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
    const auto smt = emit_smt_lib(obligation, options.solver_timeout_ms);
    std::string smt_path;
    if (!options.smt_output_dir.empty()) {
      try {
        smt_path = write_smt_artifact(obligation, smt, options.smt_output_dir);
      } catch (const std::exception& error) {
        results.push_back(make_result(obligation, VerificationStatus::Error, error.what(), smt));
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

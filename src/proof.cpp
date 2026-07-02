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

VerificationResult verify_syntactically(const ProofObligation& obligation, const std::string& smt) {
  for (const auto& assumption : obligation.assumptions) {
    if (expressions_equal(assumption.expr, obligation.goal.expr)) {
      return make_result(obligation, VerificationStatus::Proven, "goal is an active assumption",
                         smt);
    }
  }
  if (obligation.goal.expr && obligation.goal.expr->kind == ExprNode::Kind::Boolean &&
      obligation.goal.expr->boolean_value) {
    return make_result(obligation, VerificationStatus::Proven, "goal is literal true", smt);
  }
  return make_result(obligation, VerificationStatus::Unknown, "no local proof rule matched", smt);
}

struct ProofContext {
  SymbolTable symbols;
  std::unordered_map<std::string, std::string> bindings;
  std::vector<NamedPredicate> active;
  int scope_depth = 0;
};

std::string scoped_symbol(const std::string& name, const SourceLocation& location,
                          const std::string& purpose) {
  return name + "." + purpose + "." + std::to_string(location.line) + "." +
         std::to_string(location.column);
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

void process_statements(const std::vector<Statement>& statements, const FunctionDecl& fn,
                        ProofContext& context, int& assert_index, int& return_index,
                        int& loop_index, std::vector<ProofObligation>& obligations);

void preserve_branch_facts(const Expr& condition, const ProofContext& branch,
                           std::size_t fact_start, bool then_branch, ProofContext& target) {
  for (auto index = fact_start; index < branch.active.size(); ++index) {
    auto guarded = make_guarded_fact(condition, branch.active[index], then_branch);
    copy_symbols_for_expr(guarded.expr, branch, target);
    target.active.push_back(std::move(guarded));
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
  obligation.name = "fn." + fn.name + ".loop." + std::to_string(loop_index) + ".invariant." +
                    std::to_string(invariant_index) + "." + invariant.name + "." + phase;
  obligation.location = invariant.location;
  obligation.range = invariant.range.start.line == 0 ? statement.range : invariant.range;
  obligation.assumptions = context.active;
  obligation.goal = rewrite_predicate(invariant, context.bindings);
  obligation.symbols = context.symbols;
  return obligation;
}

void process_if_statement(const Statement& statement, const FunctionDecl& fn, ProofContext& context,
                          int& assert_index, int& return_index, int& loop_index,
                          std::vector<ProofObligation>& obligations) {
  const auto condition = rewrite_expr(statement.expr, context.bindings);

  auto then_context = context;
  then_context.scope_depth = context.scope_depth + 1;
  then_context.active.push_back(make_branch_condition(condition, true));
  const auto then_fact_start = then_context.active.size();
  process_statements(statement.then_branch, fn, then_context, assert_index, return_index,
                     loop_index, obligations);

  auto else_context = context;
  else_context.scope_depth = context.scope_depth + 1;
  else_context.active.push_back(make_branch_condition(condition, false));
  const auto else_fact_start = else_context.active.size();
  process_statements(statement.else_branch, fn, else_context, assert_index, return_index,
                     loop_index, obligations);

  preserve_branch_facts(condition, then_context, then_fact_start, true, context);
  preserve_branch_facts(condition, else_context, else_fact_start, false, context);
  merge_branch_bindings(statement, condition, then_context, else_context, context);
}

void process_while_statement(const Statement& statement, const FunctionDecl& fn,
                             ProofContext& context, int& assert_index, int& return_index,
                             int& loop_index, std::vector<ProofObligation>& obligations) {
  ++loop_index;
  const auto current_loop = loop_index;

  int invariant_index = 0;
  for (const auto& invariant : statement.loop_invariants) {
    ++invariant_index;
    obligations.push_back(make_loop_obligation(fn, statement, current_loop, invariant_index,
                                               "initial", invariant, context));
  }

  const auto assigned_names = assigned_outer_names(statement, context);

  ProofContext head_context = context;
  head_context.active = filter_stale_mutable_facts(context, assigned_names);
  rebind_loop_state(statement, head_context, assigned_names, "loop");
  for (const auto& invariant : statement.loop_invariants) {
    head_context.active.push_back(rewrite_predicate(invariant, head_context.bindings));
  }
  head_context.active.push_back(
      NamedPredicate{"while_condition", rewrite_expr(statement.expr, head_context.bindings),
                     statement.location, statement.expr ? statement.expr->range : statement.range});

  auto body_context = head_context;
  body_context.scope_depth = context.scope_depth + 1;
  process_statements(statement.then_branch, fn, body_context, assert_index, return_index,
                     loop_index, obligations);

  invariant_index = 0;
  for (const auto& invariant : statement.loop_invariants) {
    ++invariant_index;
    obligations.push_back(make_loop_obligation(fn, statement, current_loop, invariant_index,
                                               "preserved", invariant, body_context));
  }

  context.active = filter_stale_mutable_facts(context, assigned_names);
  rebind_loop_state(statement, context, assigned_names, "loop_exit");
  for (const auto& invariant : statement.loop_invariants) {
    context.active.push_back(rewrite_predicate(invariant, context.bindings));
  }
  auto exit_condition =
      make_unary(UnaryOp::Not, rewrite_expr(statement.expr, context.bindings), statement.range);
  context.active.push_back(
      NamedPredicate{"while_exit", exit_condition, statement.location, statement.range});
}

void process_statement(const Statement& statement, const FunctionDecl& fn, ProofContext& context,
                       int& assert_index, int& return_index, int& loop_index,
                       std::vector<ProofObligation>& obligations) {
  if (statement.kind == StatementKind::Let) {
    const auto symbol = context.scope_depth == 0
                            ? statement.name
                            : scoped_symbol(statement.name, statement.location, "local");
    const auto value = rewrite_expr(statement.expr, context.bindings);
    context.symbols[symbol] = statement.type;
    context.bindings[statement.name] = symbol;
    auto equality = make_binary(BinaryOp::Equal, make_identifier(symbol, statement.location), value,
                                statement.location);
    context.active.push_back(
        NamedPredicate{"let_" + statement.name, equality, statement.location, statement.range});
  } else if (statement.kind == StatementKind::Assign) {
    const auto current = context.bindings.at(statement.name);
    const auto value = rewrite_expr(statement.expr, context.bindings);
    const auto symbol = scoped_symbol(statement.name, statement.location, "assign");
    context.symbols[symbol] = context.symbols.at(current);
    context.bindings[statement.name] = symbol;
    auto equality = make_binary(BinaryOp::Equal, make_identifier(symbol, statement.location), value,
                                statement.location);
    context.active.push_back(
        NamedPredicate{"assign_" + statement.name, equality, statement.location, statement.range});
  } else if (statement.kind == StatementKind::If) {
    process_if_statement(statement, fn, context, assert_index, return_index, loop_index,
                         obligations);
  } else if (statement.kind == StatementKind::While) {
    process_while_statement(statement, fn, context, assert_index, return_index, loop_index,
                            obligations);
  } else if (statement.kind == StatementKind::Assume) {
    context.active.push_back(NamedPredicate{statement.name,
                                            rewrite_expr(statement.expr, context.bindings),
                                            statement.location, statement.range});
  } else if (statement.kind == StatementKind::Assert) {
    ++assert_index;
    ProofObligation obligation;
    obligation.name =
        "fn." + fn.name + ".assert." + std::to_string(assert_index) + "." + statement.name;
    obligation.location = statement.location;
    obligation.range = statement.range;
    obligation.assumptions = context.active;
    obligation.goal = NamedPredicate{statement.name, rewrite_expr(statement.expr, context.bindings),
                                     statement.location, statement.range};
    obligation.symbols = context.symbols;
    obligations.push_back(std::move(obligation));
    context.active.push_back(NamedPredicate{statement.name,
                                            rewrite_expr(statement.expr, context.bindings),
                                            statement.location, statement.range});
  } else if (statement.kind == StatementKind::Return && fn.return_type.kind != TypeKind::Void) {
    ++return_index;
    const auto return_name = "return_" + std::to_string(return_index);
    auto equality = make_binary(BinaryOp::Equal,
                                make_identifier(context.bindings.at("result"), statement.location),
                                rewrite_expr(statement.expr, context.bindings), statement.location);
    context.active.push_back(
        NamedPredicate{return_name, equality, statement.location, statement.range});
  }
}

void process_statements(const std::vector<Statement>& statements, const FunctionDecl& fn,
                        ProofContext& context, int& assert_index, int& return_index,
                        int& loop_index, std::vector<ProofObligation>& obligations) {
  for (const auto& statement : statements) {
    process_statement(statement, fn, context, assert_index, return_index, loop_index, obligations);
  }
}

} // namespace

std::vector<ProofObligation> build_obligations(const Module& module) {
  std::vector<ProofObligation> obligations;
  for (const auto& fn : module.functions) {
    ProofContext context;
    for (const auto& param : fn.params) {
      context.symbols[param.name] = param.type;
      context.bindings[param.name] = param.name;
    }
    if (fn.return_type.kind != TypeKind::Void) {
      context.symbols["result"] = fn.return_type;
      context.bindings["result"] = "result";
    }

    for (const auto& precondition : fn.preconditions) {
      context.active.push_back(rewrite_predicate(precondition, context.bindings));
    }
    int assert_index = 0;
    int return_index = 0;
    int loop_index = 0;
    process_statements(fn.body, fn, context, assert_index, return_index, loop_index, obligations);

    int ensure_index = 0;
    for (const auto& ensure : fn.ensures) {
      ++ensure_index;
      ProofObligation obligation;
      obligation.name =
          "fn." + fn.name + ".ensures." + std::to_string(ensure_index) + "." + ensure.name;
      obligation.location = ensure.location;
      obligation.range = ensure.range;
      obligation.assumptions = context.active;
      obligation.goal = rewrite_predicate(ensure, context.bindings);
      obligation.symbols = context.symbols;
      obligations.push_back(std::move(obligation));
    }
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
      symbols[identifier] = Type{TypeKind::Unknown, "i64"};
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

#include "sigil/typecheck.hpp"

#include <unordered_map>
#include <unordered_set>

namespace sigil {

namespace {

using FunctionTable = std::unordered_map<std::string, const FunctionDecl*>;
using StructTable = std::unordered_map<std::string, const StructDecl*>;

bool same_type(const Type& lhs, const Type& rhs) {
  if (lhs.kind == TypeKind::Unknown || rhs.kind == TypeKind::Unknown) {
    return lhs.kind == rhs.kind && lhs.spelling == rhs.spelling;
  }
  return lhs.kind == rhs.kind;
}

bool is_reserved_value_name(const std::string& name) {
  return name == "result";
}

bool is_builtin_type_name(const std::string& name) {
  return name == "i64" || name == "bool" || name == "void";
}

bool is_declared_struct_type(const Type& type, const StructTable& structs) {
  return type.kind == TypeKind::Unknown && structs.find(type.spelling) != structs.end();
}

bool is_known_type(const Type& type, const StructTable& structs) {
  return type.kind != TypeKind::Unknown || is_declared_struct_type(type, structs);
}

void require_known_type(const Type& type, const StructTable& structs, const SourceRange& range,
                        const std::string& owner) {
  if (!is_known_type(type, structs)) {
    throw Diagnostic(range, owner + " uses unsupported type '" + type.display() + "'");
  }
}

void require_value_type(const Type& type, const StructTable& structs, const SourceRange& range,
                        const std::string& owner) {
  require_known_type(type, structs, range, owner);
  if (type.kind == TypeKind::Void) {
    throw Diagnostic(range, owner + " cannot use void as a value type");
  }
}

void insert_symbol(SymbolTable& symbols, const std::string& name, const Type& type,
                   const SourceRange& range, const std::string& owner) {
  if (symbols.find(name) != symbols.end()) {
    throw Diagnostic(range, "duplicate " + owner + " '" + name + "'");
  }
  symbols[name] = type;
}

void require_unreserved_value_name(const std::string& name, const SourceRange& range,
                                   const std::string& owner) {
  if (is_reserved_value_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved name '" + name + "'");
  }
  if (is_builtin_type_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved type name '" + name + "'");
  }
}

void require_unreserved_declaration_name(const std::string& name, const SourceRange& range,
                                         const std::string& owner) {
  if (is_builtin_type_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved type name '" + name + "'");
  }
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                const FunctionTable& functions, const FunctionDecl& current_function);

Type require_type(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                  const FunctionTable& functions, const FunctionDecl& current_function,
                  TypeKind expected, const std::string& context) {
  const auto actual = infer_expr(expr, symbols, structs, functions, current_function);
  if (actual.kind != expected) {
    const auto expected_name = expected == TypeKind::Bool ? "bool" : "i64";
    throw Diagnostic(expr ? expr->range : SourceRange{},
                     context + " must be " + expected_name + ", found " + actual.display());
  }
  return actual;
}

Type infer_binary_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                       const FunctionTable& functions, const FunctionDecl& current_function) {
  const auto lhs = infer_expr(expr->lhs, symbols, structs, functions, current_function);
  const auto rhs = infer_expr(expr->rhs, symbols, structs, functions, current_function);

  switch (expr->binary_op) {
  case BinaryOp::Or:
  case BinaryOp::And:
    if (!lhs.is_bool() || !rhs.is_bool()) {
      throw Diagnostic(expr->range, "boolean operator requires bool operands");
    }
    return Type{TypeKind::Bool, "bool"};

  case BinaryOp::Less:
  case BinaryOp::LessEqual:
  case BinaryOp::Greater:
  case BinaryOp::GreaterEqual:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->range, "comparison operator requires i64 operands");
    }
    return Type{TypeKind::Bool, "bool"};

  case BinaryOp::Add:
  case BinaryOp::Subtract:
  case BinaryOp::Multiply:
  case BinaryOp::Divide:
  case BinaryOp::Modulo:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->range, "arithmetic operator requires i64 operands");
    }
    return Type{TypeKind::I64, "i64"};

  case BinaryOp::Equal:
  case BinaryOp::NotEqual:
    if (!same_type(lhs, rhs)) {
      throw Diagnostic(expr->range, "equality operands must have the same type, found " +
                                        lhs.display() + " and " + rhs.display());
    }
    if (lhs.kind == TypeKind::Void) {
      throw Diagnostic(expr->range, "cannot compare void values");
    }
    return Type{TypeKind::Bool, "bool"};
  }

  throw Diagnostic(expr->range, "unknown binary operator");
}

Type infer_call_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                     const FunctionTable& functions, const FunctionDecl& current_function) {
  const auto found = functions.find(expr->name);
  if (found == functions.end()) {
    throw Diagnostic(expr->range, "unknown function '" + expr->name + "'");
  }
  if (expr->name == current_function.name) {
    throw Diagnostic(expr->range, "recursive function calls are not supported yet");
  }

  const FunctionDecl& callee = *found->second;
  if (expr->arguments.size() != callee.params.size()) {
    throw Diagnostic(expr->range, "function '" + expr->name + "' expects " +
                                      std::to_string(callee.params.size()) + " argument(s), got " +
                                      std::to_string(expr->arguments.size()));
  }

  for (std::size_t index = 0; index < expr->arguments.size(); ++index) {
    const auto actual =
        infer_expr(expr->arguments[index], symbols, structs, functions, current_function);
    const auto& expected = callee.params[index].type;
    if (!same_type(actual, expected)) {
      throw Diagnostic(expr->arguments[index]->range,
                       "argument " + std::to_string(index + 1) + " for function '" + expr->name +
                           "' type mismatch: expected " + expected.display() + ", found " +
                           actual.display());
    }
  }

  if (callee.return_type.kind == TypeKind::Void) {
    throw Diagnostic(expr->range,
                     "function '" + expr->name + "' returns void and cannot be used as a value");
  }
  require_value_type(callee.return_type, structs, callee.range,
                     "function '" + expr->name + "' return type");
  return callee.return_type;
}

const FieldDecl* find_field(const StructDecl& decl, const std::string& name) {
  for (const auto& field : decl.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

Type infer_struct_literal_expr(const Expr& expr, const SymbolTable& symbols,
                               const StructTable& structs, const FunctionTable& functions,
                               const FunctionDecl& current_function) {
  const auto found = structs.find(expr->name);
  if (found == structs.end()) {
    throw Diagnostic(expr->range, "unknown struct '" + expr->name + "'");
  }
  const StructDecl& decl = *found->second;

  std::unordered_set<std::string> initialized;
  for (const auto& initializer : expr->field_initializers) {
    const auto* field = find_field(decl, initializer.name);
    if (!field) {
      throw Diagnostic(initializer.range,
                       "struct '" + decl.name + "' has no field '" + initializer.name + "'");
    }
    if (!initialized.insert(initializer.name).second) {
      throw Diagnostic(initializer.range,
                       "duplicate initializer for field '" + initializer.name + "'");
    }
    const auto actual = infer_expr(initializer.expr, symbols, structs, functions, current_function);
    if (!same_type(actual, field->type)) {
      throw Diagnostic(initializer.range, "field '" + decl.name + "." + initializer.name +
                                              "' type mismatch: expected " + field->type.display() +
                                              ", found " + actual.display());
    }
  }

  for (const auto& field : decl.fields) {
    if (initialized.find(field.name) == initialized.end()) {
      throw Diagnostic(expr->range,
                       "missing initializer for field '" + decl.name + "." + field.name + "'");
    }
  }

  return Type{TypeKind::Unknown, decl.name};
}

Type infer_field_access_expr(const Expr& expr, const SymbolTable& symbols,
                             const StructTable& structs, const FunctionTable& functions,
                             const FunctionDecl& current_function) {
  const auto base_type = infer_expr(expr->lhs, symbols, structs, functions, current_function);
  if (!is_declared_struct_type(base_type, structs)) {
    throw Diagnostic(expr->range,
                     "field access requires a struct value, found " + base_type.display());
  }

  const StructDecl& decl = *structs.at(base_type.spelling);
  const auto* field = find_field(decl, expr->name);
  if (!field) {
    throw Diagnostic(expr->range, "struct '" + decl.name + "' has no field '" + expr->name + "'");
  }
  return field->type;
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                const FunctionTable& functions, const FunctionDecl& current_function) {
  if (!expr) {
    throw Diagnostic(SourceRange{}, "missing expression");
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return Type{TypeKind::I64, "i64"};
  case ExprNode::Kind::Boolean:
    return Type{TypeKind::Bool, "bool"};
  case ExprNode::Kind::Identifier: {
    const auto found = symbols.find(expr->name);
    if (found == symbols.end()) {
      throw Diagnostic(expr->range, "unknown identifier '" + expr->name + "'");
    }
    require_value_type(found->second, structs, expr->range, "identifier '" + expr->name + "'");
    return found->second;
  }
  case ExprNode::Kind::Call:
    return infer_call_expr(expr, symbols, structs, functions, current_function);
  case ExprNode::Kind::StructLiteral:
    return infer_struct_literal_expr(expr, symbols, structs, functions, current_function);
  case ExprNode::Kind::FieldAccess:
    return infer_field_access_expr(expr, symbols, structs, functions, current_function);
  case ExprNode::Kind::Unary: {
    const auto operand = infer_expr(expr->lhs, symbols, structs, functions, current_function);
    if (expr->unary_op == UnaryOp::Not) {
      if (!operand.is_bool()) {
        throw Diagnostic(expr->range, "'!' requires a bool operand");
      }
      return Type{TypeKind::Bool, "bool"};
    }
    if (!operand.is_integer()) {
      throw Diagnostic(expr->range, "unary '-' requires an i64 operand");
    }
    return Type{TypeKind::I64, "i64"};
  }
  case ExprNode::Kind::Binary:
    return infer_binary_expr(expr, symbols, structs, functions, current_function);
  case ExprNode::Kind::If: {
    require_type(expr->condition, symbols, structs, functions, current_function, TypeKind::Bool,
                 "if condition");
    const auto then_type = infer_expr(expr->lhs, symbols, structs, functions, current_function);
    const auto else_type = infer_expr(expr->rhs, symbols, structs, functions, current_function);
    if (!same_type(then_type, else_type)) {
      throw Diagnostic(expr->range, "if branches must have the same type, found " +
                                        then_type.display() + " and " + else_type.display());
    }
    if (then_type.kind == TypeKind::Void) {
      throw Diagnostic(expr->range, "if expression cannot produce void");
    }
    return then_type;
  }
  }

  throw Diagnostic(expr->range, "unknown expression kind");
}

void validate_predicate(const NamedPredicate& predicate, const SymbolTable& symbols,
                        const StructTable& structs, const FunctionTable& functions,
                        const FunctionDecl& current_function, const std::string& owner) {
  require_type(predicate.expr, symbols, structs, functions, current_function, TypeKind::Bool,
               owner + " '" + predicate.name + "'");
}

void validate_statement(const Statement& statement, const FunctionDecl& decl, SymbolTable& locals,
                        std::unordered_set<std::string>& assignable_locals,
                        std::unordered_set<std::string>& proof_labels, const StructTable& structs,
                        const FunctionTable& functions);

void reject_loop_body_returns(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Return) {
      throw Diagnostic(statement.range, "while bodies cannot contain return statements yet");
    }
    if (statement.kind == StatementKind::If) {
      reject_loop_body_returns(statement.then_branch);
      reject_loop_body_returns(statement.else_branch);
    } else if (statement.kind == StatementKind::While) {
      reject_loop_body_returns(statement.then_branch);
    }
  }
}

bool block_returns(const std::vector<Statement>& statements);

bool statement_returns(const Statement& statement) {
  if (statement.kind == StatementKind::Return) {
    return true;
  }
  if (statement.kind == StatementKind::If) {
    return block_returns(statement.then_branch) && block_returns(statement.else_branch);
  }
  return false;
}

bool block_returns(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement_returns(statement)) {
      return true;
    }
  }
  return false;
}

void validate_statement_block(const std::vector<Statement>& statements, const FunctionDecl& decl,
                              SymbolTable locals, std::unordered_set<std::string> assignable_locals,
                              std::unordered_set<std::string>& proof_labels,
                              const StructTable& structs, const FunctionTable& functions) {
  bool terminated = false;
  for (const auto& statement : statements) {
    if (terminated) {
      throw Diagnostic(statement.range, "unreachable statement after guaranteed return");
    }
    validate_statement(statement, decl, locals, assignable_locals, proof_labels, structs,
                       functions);
    terminated = statement_returns(statement);
  }
}

void validate_proof_label(const std::string& name, const SourceRange& range,
                          std::unordered_set<std::string>& proof_labels) {
  if (!proof_labels.insert(name).second) {
    throw Diagnostic(range, "duplicate proof label '" + name + "'");
  }
}

void validate_statement_label(const Statement& statement,
                              std::unordered_set<std::string>& proof_labels) {
  if (!statement.has_explicit_label) {
    return;
  }
  validate_proof_label(statement.name, statement.range, proof_labels);
}

void validate_statement(const Statement& statement, const FunctionDecl& decl, SymbolTable& locals,
                        std::unordered_set<std::string>& assignable_locals,
                        std::unordered_set<std::string>& proof_labels, const StructTable& structs,
                        const FunctionTable& functions) {
  if (statement.kind == StatementKind::Let) {
    require_unreserved_value_name(statement.name, statement.range,
                                  "local '" + decl.name + "." + statement.name + "'");
    require_value_type(statement.type, structs, statement.range,
                       "local '" + decl.name + "." + statement.name + "'");
    const auto actual = infer_expr(statement.expr, locals, structs, functions, decl);
    if (!same_type(actual, statement.type)) {
      throw Diagnostic(statement.range, "let type mismatch: expected " + statement.type.display() +
                                            ", found " + actual.display());
    }
    insert_symbol(locals, statement.name, statement.type, statement.range, "local");
    assignable_locals.insert(statement.name);
  } else if (statement.kind == StatementKind::Assign) {
    const auto found = locals.find(statement.name);
    if (found == locals.end()) {
      throw Diagnostic(statement.range,
                       "assignment target '" + statement.name + "' is not declared");
    }
    if (assignable_locals.find(statement.name) == assignable_locals.end()) {
      throw Diagnostic(statement.range,
                       "assignment target '" + statement.name + "' is not a mutable local");
    }
    const auto actual = infer_expr(statement.expr, locals, structs, functions, decl);
    if (!same_type(actual, found->second)) {
      throw Diagnostic(statement.range, "assignment type mismatch: expected " +
                                            found->second.display() + ", found " +
                                            actual.display());
    }
  } else if (statement.kind == StatementKind::If) {
    require_type(statement.expr, locals, structs, functions, decl, TypeKind::Bool,
                 "if statement condition");
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels,
                             structs, functions);
    validate_statement_block(statement.else_branch, decl, locals, assignable_locals, proof_labels,
                             structs, functions);
  } else if (statement.kind == StatementKind::While) {
    require_type(statement.expr, locals, structs, functions, decl, TypeKind::Bool,
                 "while condition");
    std::unordered_set<std::string> invariant_names;
    for (const auto& invariant : statement.loop_invariants) {
      if (!invariant_names.insert(invariant.name).second) {
        throw Diagnostic(invariant.range, "duplicate loop invariant '" + invariant.name + "'");
      }
      validate_proof_label(invariant.name, invariant.range, proof_labels);
      validate_predicate(invariant, locals, structs, functions, decl, "loop invariant");
    }
    reject_loop_body_returns(statement.then_branch);
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels,
                             structs, functions);
  } else if (statement.kind == StatementKind::Assume) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, structs, functions, decl, TypeKind::Bool,
                 "assume statement");
  } else if (statement.kind == StatementKind::Assert) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, structs, functions, decl, TypeKind::Bool,
                 "assert statement");
  } else if (statement.kind == StatementKind::Return) {
    if (decl.return_type.kind == TypeKind::Void) {
      if (statement.expr) {
        throw Diagnostic(statement.range, "void functions cannot return a value");
      }
      return;
    }
    if (!statement.expr) {
      throw Diagnostic(statement.range, "non-void functions must return a value");
    }
    const auto actual = infer_expr(statement.expr, locals, structs, functions, decl);
    if (!same_type(actual, decl.return_type)) {
      throw Diagnostic(statement.range, "return type mismatch: expected " +
                                            decl.return_type.display() + ", found " +
                                            actual.display());
    }
  }
}

void validate_struct(const StructDecl& decl, const StructTable& structs) {
  SymbolTable fields;
  for (const auto& field : decl.fields) {
    require_unreserved_value_name(field.name, field.range,
                                  "field '" + decl.name + "." + field.name + "'");
    require_value_type(field.type, structs, field.range,
                       "field '" + decl.name + "." + field.name + "'");
    insert_symbol(fields, field.name, field.type, field.range, "field");
  }

  FunctionDecl invariant_context;
  invariant_context.name = decl.name;
  FunctionTable no_functions;
  std::unordered_set<std::string> invariant_names;
  for (const auto& invariant : decl.invariants) {
    if (!invariant_names.insert(invariant.name).second) {
      throw Diagnostic(invariant.range, "duplicate invariant '" + invariant.name + "'");
    }
    validate_predicate(invariant, fields, structs, no_functions, invariant_context, "invariant");
  }
}

void validate_function(const FunctionDecl& decl, const StructTable& structs,
                       const FunctionTable& functions) {
  SymbolTable params;
  for (const auto& param : decl.params) {
    require_unreserved_value_name(param.name, param.range,
                                  "parameter '" + decl.name + "." + param.name + "'");
    require_value_type(param.type, structs, param.range,
                       "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.range, "parameter");
  }
  require_known_type(decl.return_type, structs, decl.range,
                     "function '" + decl.name + "' return type");

  std::unordered_set<std::string> precondition_names;
  std::unordered_set<std::string> postcondition_names;
  std::unordered_set<std::string> contract_labels;
  for (const auto& precondition : decl.preconditions) {
    if (!precondition_names.insert(precondition.name).second) {
      throw Diagnostic(precondition.range, "duplicate precondition '" + precondition.name + "'");
    }
    contract_labels.insert(precondition.name);
    validate_predicate(precondition, params, structs, functions, decl, "precondition");
  }

  SymbolTable post_symbols = params;
  if (decl.return_type.kind != TypeKind::Void) {
    post_symbols["result"] = decl.return_type;
  }
  for (const auto& ensure : decl.ensures) {
    if (!postcondition_names.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate postcondition '" + ensure.name + "'");
    }
    if (!contract_labels.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate contract label '" + ensure.name + "'");
    }
    validate_predicate(ensure, post_symbols, structs, functions, decl, "postcondition");
  }

  SymbolTable locals = params;
  std::unordered_set<std::string> assignable_locals;
  std::unordered_set<std::string> proof_labels = contract_labels;
  validate_statement_block(decl.body, decl, locals, assignable_locals, proof_labels, structs,
                           functions);
  if (decl.return_type.kind != TypeKind::Void && !block_returns(decl.body)) {
    throw Diagnostic(decl.range, "function '" + decl.name + "' must return a value on every path");
  }
}

struct CallEdge {
  std::string callee;
  SourceRange range;
};

using CallGraph = std::unordered_map<std::string, std::vector<CallEdge>>;

void collect_call_edges(const Expr& expr, std::vector<CallEdge>& edges) {
  if (!expr) {
    return;
  }
  if (expr->kind == ExprNode::Kind::Call) {
    edges.push_back(CallEdge{expr->name, expr->range});
    for (const auto& argument : expr->arguments) {
      collect_call_edges(argument, edges);
    }
    return;
  }
  collect_call_edges(expr->condition, edges);
  collect_call_edges(expr->lhs, edges);
  collect_call_edges(expr->rhs, edges);
  for (const auto& field : expr->field_initializers) {
    collect_call_edges(field.expr, edges);
  }
}

void collect_call_edges(const std::vector<NamedPredicate>& predicates,
                        std::vector<CallEdge>& edges) {
  for (const auto& predicate : predicates) {
    collect_call_edges(predicate.expr, edges);
  }
}

void collect_call_edges(const Statement& statement, std::vector<CallEdge>& edges);

void collect_call_edges(const std::vector<Statement>& statements, std::vector<CallEdge>& edges) {
  for (const auto& statement : statements) {
    collect_call_edges(statement, edges);
  }
}

void collect_call_edges(const Statement& statement, std::vector<CallEdge>& edges) {
  collect_call_edges(statement.expr, edges);
  collect_call_edges(statement.loop_invariants, edges);
  collect_call_edges(statement.then_branch, edges);
  collect_call_edges(statement.else_branch, edges);
}

CallGraph build_call_graph(const Module& module) {
  CallGraph graph;
  for (const auto& decl : module.functions) {
    auto& edges = graph[decl.name];
    collect_call_edges(decl.preconditions, edges);
    collect_call_edges(decl.ensures, edges);
    collect_call_edges(decl.body, edges);
  }
  return graph;
}

enum class VisitState {
  Visiting,
  Visited,
};

void visit_call_graph(const std::string& name, const CallGraph& graph,
                      const FunctionTable& functions,
                      std::unordered_map<std::string, VisitState>& states) {
  states[name] = VisitState::Visiting;

  const auto found = graph.find(name);
  if (found != graph.end()) {
    for (const auto& edge : found->second) {
      if (functions.find(edge.callee) == functions.end()) {
        continue;
      }
      const auto state = states.find(edge.callee);
      if (state != states.end() && state->second == VisitState::Visiting) {
        throw Diagnostic(edge.range, "recursive function calls are not supported yet");
      }
      if (state == states.end()) {
        visit_call_graph(edge.callee, graph, functions, states);
      }
    }
  }

  states[name] = VisitState::Visited;
}

void reject_recursive_calls(const Module& module, const FunctionTable& functions) {
  const auto graph = build_call_graph(module);
  std::unordered_map<std::string, VisitState> states;
  for (const auto& decl : module.functions) {
    if (states.find(decl.name) == states.end()) {
      visit_call_graph(decl.name, graph, functions, states);
    }
  }
}

} // namespace

void validate_module(const Module& module) {
  std::unordered_set<std::string> declaration_names;
  std::unordered_set<std::string> struct_names;
  StructTable structs;
  for (const auto& decl : module.structs) {
    require_unreserved_declaration_name(decl.name, decl.range, "struct '" + decl.name + "'");
    if (!struct_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate struct '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    structs[decl.name] = &decl;
  }

  for (const auto& decl : module.structs) {
    validate_struct(decl, structs);
  }

  std::unordered_set<std::string> function_names;
  FunctionTable functions;
  for (const auto& decl : module.functions) {
    require_unreserved_declaration_name(decl.name, decl.range, "function '" + decl.name + "'");
    if (!function_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate function '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    functions[decl.name] = &decl;
  }

  for (const auto& decl : module.functions) {
    validate_function(decl, structs, functions);
  }
  reject_recursive_calls(module, functions);
}

} // namespace sigil

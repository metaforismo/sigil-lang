#include "sigil/typecheck.hpp"

#include <unordered_set>

namespace sigil {

namespace {

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

void require_known_type(const Type& type, const SourceRange& range, const std::string& owner) {
  if (type.kind == TypeKind::Unknown) {
    throw Diagnostic(range, owner + " uses unsupported type '" + type.display() + "'");
  }
}

void require_value_type(const Type& type, const SourceRange& range, const std::string& owner) {
  require_known_type(type, range, owner);
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

Type infer_expr(const Expr& expr, const SymbolTable& symbols);

Type require_type(const Expr& expr, const SymbolTable& symbols, TypeKind expected,
                  const std::string& context) {
  const auto actual = infer_expr(expr, symbols);
  if (actual.kind != expected) {
    const auto expected_name = expected == TypeKind::Bool ? "bool" : "i64";
    throw Diagnostic(expr ? expr->range : SourceRange{},
                     context + " must be " + expected_name + ", found " + actual.display());
  }
  return actual;
}

Type infer_binary_expr(const Expr& expr, const SymbolTable& symbols) {
  const auto lhs = infer_expr(expr->lhs, symbols);
  const auto rhs = infer_expr(expr->rhs, symbols);

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

Type infer_expr(const Expr& expr, const SymbolTable& symbols) {
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
    require_value_type(found->second, expr->range, "identifier '" + expr->name + "'");
    return found->second;
  }
  case ExprNode::Kind::Unary: {
    const auto operand = infer_expr(expr->lhs, symbols);
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
    return infer_binary_expr(expr, symbols);
  case ExprNode::Kind::If: {
    require_type(expr->condition, symbols, TypeKind::Bool, "if condition");
    const auto then_type = infer_expr(expr->lhs, symbols);
    const auto else_type = infer_expr(expr->rhs, symbols);
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
                        const std::string& owner) {
  require_type(predicate.expr, symbols, TypeKind::Bool, owner + " '" + predicate.name + "'");
}

void validate_statement(const Statement& statement, const FunctionDecl& decl, SymbolTable& locals,
                        std::unordered_set<std::string>& assignable_locals,
                        std::unordered_set<std::string>& proof_labels);

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
                              std::unordered_set<std::string>& proof_labels) {
  bool terminated = false;
  for (const auto& statement : statements) {
    if (terminated) {
      throw Diagnostic(statement.range, "unreachable statement after guaranteed return");
    }
    validate_statement(statement, decl, locals, assignable_locals, proof_labels);
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
                        std::unordered_set<std::string>& proof_labels) {
  if (statement.kind == StatementKind::Let) {
    require_unreserved_value_name(statement.name, statement.range,
                                  "local '" + decl.name + "." + statement.name + "'");
    require_value_type(statement.type, statement.range,
                       "local '" + decl.name + "." + statement.name + "'");
    const auto actual = infer_expr(statement.expr, locals);
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
    const auto actual = infer_expr(statement.expr, locals);
    if (!same_type(actual, found->second)) {
      throw Diagnostic(statement.range, "assignment type mismatch: expected " +
                                            found->second.display() + ", found " +
                                            actual.display());
    }
  } else if (statement.kind == StatementKind::If) {
    require_type(statement.expr, locals, TypeKind::Bool, "if statement condition");
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels);
    validate_statement_block(statement.else_branch, decl, locals, assignable_locals, proof_labels);
  } else if (statement.kind == StatementKind::While) {
    require_type(statement.expr, locals, TypeKind::Bool, "while condition");
    std::unordered_set<std::string> invariant_names;
    for (const auto& invariant : statement.loop_invariants) {
      if (!invariant_names.insert(invariant.name).second) {
        throw Diagnostic(invariant.range, "duplicate loop invariant '" + invariant.name + "'");
      }
      validate_proof_label(invariant.name, invariant.range, proof_labels);
      validate_predicate(invariant, locals, "loop invariant");
    }
    reject_loop_body_returns(statement.then_branch);
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels);
  } else if (statement.kind == StatementKind::Assume) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, TypeKind::Bool, "assume statement");
  } else if (statement.kind == StatementKind::Assert) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, TypeKind::Bool, "assert statement");
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
    const auto actual = infer_expr(statement.expr, locals);
    if (!same_type(actual, decl.return_type)) {
      throw Diagnostic(statement.range, "return type mismatch: expected " +
                                            decl.return_type.display() + ", found " +
                                            actual.display());
    }
  }
}

void validate_struct(const StructDecl& decl) {
  SymbolTable fields;
  for (const auto& field : decl.fields) {
    require_unreserved_value_name(field.name, field.range,
                                  "field '" + decl.name + "." + field.name + "'");
    require_value_type(field.type, field.range, "field '" + decl.name + "." + field.name + "'");
    insert_symbol(fields, field.name, field.type, field.range, "field");
  }

  std::unordered_set<std::string> invariant_names;
  for (const auto& invariant : decl.invariants) {
    if (!invariant_names.insert(invariant.name).second) {
      throw Diagnostic(invariant.range, "duplicate invariant '" + invariant.name + "'");
    }
    validate_predicate(invariant, fields, "invariant");
  }
}

void validate_function(const FunctionDecl& decl) {
  SymbolTable params;
  for (const auto& param : decl.params) {
    require_unreserved_value_name(param.name, param.range,
                                  "parameter '" + decl.name + "." + param.name + "'");
    require_value_type(param.type, param.range, "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.range, "parameter");
  }
  require_known_type(decl.return_type, decl.range, "function '" + decl.name + "' return type");

  std::unordered_set<std::string> precondition_names;
  std::unordered_set<std::string> postcondition_names;
  std::unordered_set<std::string> contract_labels;
  for (const auto& precondition : decl.preconditions) {
    if (!precondition_names.insert(precondition.name).second) {
      throw Diagnostic(precondition.range, "duplicate precondition '" + precondition.name + "'");
    }
    contract_labels.insert(precondition.name);
    validate_predicate(precondition, params, "precondition");
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
    validate_predicate(ensure, post_symbols, "postcondition");
  }

  SymbolTable locals = params;
  std::unordered_set<std::string> assignable_locals;
  std::unordered_set<std::string> proof_labels;
  validate_statement_block(decl.body, decl, locals, assignable_locals, proof_labels);
  if (decl.return_type.kind != TypeKind::Void && !block_returns(decl.body)) {
    throw Diagnostic(decl.range, "function '" + decl.name + "' must return a value on every path");
  }
}

} // namespace

void validate_module(const Module& module) {
  std::unordered_set<std::string> declaration_names;
  std::unordered_set<std::string> struct_names;
  for (const auto& decl : module.structs) {
    require_unreserved_declaration_name(decl.name, decl.range, "struct '" + decl.name + "'");
    if (!struct_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate struct '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    validate_struct(decl);
  }

  std::unordered_set<std::string> function_names;
  for (const auto& decl : module.functions) {
    require_unreserved_declaration_name(decl.name, decl.range, "function '" + decl.name + "'");
    if (!function_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate function '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    validate_function(decl);
  }
}

} // namespace sigil

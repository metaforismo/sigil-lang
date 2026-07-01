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

void validate_struct(const StructDecl& decl) {
  SymbolTable fields;
  for (const auto& field : decl.fields) {
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
    require_value_type(param.type, param.range, "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.range, "parameter");
  }
  require_known_type(decl.return_type, decl.range, "function '" + decl.name + "' return type");

  std::unordered_set<std::string> predicate_names;
  for (const auto& precondition : decl.preconditions) {
    if (!predicate_names.insert("requires:" + precondition.name).second) {
      throw Diagnostic(precondition.range, "duplicate precondition '" + precondition.name + "'");
    }
    validate_predicate(precondition, params, "precondition");
  }

  SymbolTable post_symbols = params;
  if (decl.return_type.kind != TypeKind::Void) {
    post_symbols["result"] = decl.return_type;
  }
  for (const auto& ensure : decl.ensures) {
    if (!predicate_names.insert("ensures:" + ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate postcondition '" + ensure.name + "'");
    }
    validate_predicate(ensure, post_symbols, "postcondition");
  }

  SymbolTable locals = params;
  for (const auto& statement : decl.body) {
    if (statement.kind == StatementKind::Let) {
      require_value_type(statement.type, statement.range,
                         "local '" + decl.name + "." + statement.name + "'");
      const auto actual = infer_expr(statement.expr, locals);
      if (!same_type(actual, statement.type)) {
        throw Diagnostic(statement.range, "let type mismatch: expected " +
                                              statement.type.display() + ", found " +
                                              actual.display());
      }
      insert_symbol(locals, statement.name, statement.type, statement.range, "local");
    } else if (statement.kind == StatementKind::Assume) {
      require_type(statement.expr, locals, TypeKind::Bool, "assume statement");
    } else if (statement.kind == StatementKind::Assert) {
      require_type(statement.expr, locals, TypeKind::Bool, "assert statement");
    } else if (statement.kind == StatementKind::Return) {
      if (decl.return_type.kind == TypeKind::Void) {
        throw Diagnostic(statement.range, "void functions cannot return a value yet");
      }
      const auto actual = infer_expr(statement.expr, locals);
      if (!same_type(actual, decl.return_type)) {
        throw Diagnostic(statement.range, "return type mismatch: expected " +
                                              decl.return_type.display() + ", found " +
                                              actual.display());
      }
    }
  }
}

} // namespace

void validate_module(const Module& module) {
  std::unordered_set<std::string> struct_names;
  for (const auto& decl : module.structs) {
    if (!struct_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate struct '" + decl.name + "'");
    }
    validate_struct(decl);
  }

  std::unordered_set<std::string> function_names;
  for (const auto& decl : module.functions) {
    if (!function_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate function '" + decl.name + "'");
    }
    validate_function(decl);
  }
}

} // namespace sigil

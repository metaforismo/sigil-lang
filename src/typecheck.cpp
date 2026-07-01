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

void require_known_type(const Type& type, const SourceLocation& location,
                        const std::string& owner) {
  if (type.kind == TypeKind::Unknown) {
    throw Diagnostic(location, owner + " uses unsupported type '" + type.display() + "'");
  }
}

void require_value_type(const Type& type, const SourceLocation& location,
                        const std::string& owner) {
  require_known_type(type, location, owner);
  if (type.kind == TypeKind::Void) {
    throw Diagnostic(location, owner + " cannot use void as a value type");
  }
}

void insert_symbol(SymbolTable& symbols, const std::string& name, const Type& type,
                   const SourceLocation& location, const std::string& owner) {
  if (symbols.find(name) != symbols.end()) {
    throw Diagnostic(location, "duplicate " + owner + " '" + name + "'");
  }
  symbols[name] = type;
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols);

Type require_type(const Expr& expr, const SymbolTable& symbols, TypeKind expected,
                  const std::string& context) {
  const auto actual = infer_expr(expr, symbols);
  if (actual.kind != expected) {
    const auto expected_name = expected == TypeKind::Bool ? "bool" : "i64";
    throw Diagnostic(expr ? expr->location : SourceLocation{},
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
      throw Diagnostic(expr->location, "boolean operator requires bool operands");
    }
    return Type{TypeKind::Bool, "bool"};

  case BinaryOp::Less:
  case BinaryOp::LessEqual:
  case BinaryOp::Greater:
  case BinaryOp::GreaterEqual:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->location, "comparison operator requires i64 operands");
    }
    return Type{TypeKind::Bool, "bool"};

  case BinaryOp::Add:
  case BinaryOp::Subtract:
  case BinaryOp::Multiply:
  case BinaryOp::Divide:
  case BinaryOp::Modulo:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->location, "arithmetic operator requires i64 operands");
    }
    return Type{TypeKind::I64, "i64"};

  case BinaryOp::Equal:
  case BinaryOp::NotEqual:
    if (!same_type(lhs, rhs)) {
      throw Diagnostic(expr->location, "equality operands must have the same type, found " +
                                           lhs.display() + " and " + rhs.display());
    }
    if (lhs.kind == TypeKind::Void) {
      throw Diagnostic(expr->location, "cannot compare void values");
    }
    return Type{TypeKind::Bool, "bool"};
  }

  throw Diagnostic(expr->location, "unknown binary operator");
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols) {
  if (!expr) {
    throw Diagnostic({}, "missing expression");
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return Type{TypeKind::I64, "i64"};
  case ExprNode::Kind::Boolean:
    return Type{TypeKind::Bool, "bool"};
  case ExprNode::Kind::Identifier: {
    const auto found = symbols.find(expr->name);
    if (found == symbols.end()) {
      throw Diagnostic(expr->location, "unknown identifier '" + expr->name + "'");
    }
    require_value_type(found->second, expr->location, "identifier '" + expr->name + "'");
    return found->second;
  }
  case ExprNode::Kind::Unary: {
    const auto operand = infer_expr(expr->lhs, symbols);
    if (expr->unary_op == UnaryOp::Not) {
      if (!operand.is_bool()) {
        throw Diagnostic(expr->location, "'!' requires a bool operand");
      }
      return Type{TypeKind::Bool, "bool"};
    }
    if (!operand.is_integer()) {
      throw Diagnostic(expr->location, "unary '-' requires an i64 operand");
    }
    return Type{TypeKind::I64, "i64"};
  }
  case ExprNode::Kind::Binary:
    return infer_binary_expr(expr, symbols);
  }

  throw Diagnostic(expr->location, "unknown expression kind");
}

void validate_predicate(const NamedPredicate& predicate, const SymbolTable& symbols,
                        const std::string& owner) {
  require_type(predicate.expr, symbols, TypeKind::Bool, owner + " '" + predicate.name + "'");
}

void validate_struct(const StructDecl& decl) {
  SymbolTable fields;
  for (const auto& field : decl.fields) {
    require_value_type(field.type, field.location, "field '" + decl.name + "." + field.name + "'");
    insert_symbol(fields, field.name, field.type, field.location, "field");
  }

  std::unordered_set<std::string> invariant_names;
  for (const auto& invariant : decl.invariants) {
    if (!invariant_names.insert(invariant.name).second) {
      throw Diagnostic(invariant.location, "duplicate invariant '" + invariant.name + "'");
    }
    validate_predicate(invariant, fields, "invariant");
  }
}

void validate_function(const FunctionDecl& decl) {
  SymbolTable params;
  for (const auto& param : decl.params) {
    require_value_type(param.type, param.location,
                       "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.location, "parameter");
  }
  require_known_type(decl.return_type, decl.location, "function '" + decl.name + "' return type");

  std::unordered_set<std::string> predicate_names;
  for (const auto& precondition : decl.preconditions) {
    if (!predicate_names.insert("requires:" + precondition.name).second) {
      throw Diagnostic(precondition.location, "duplicate precondition '" + precondition.name + "'");
    }
    validate_predicate(precondition, params, "precondition");
  }

  SymbolTable post_symbols = params;
  if (decl.return_type.kind != TypeKind::Void) {
    post_symbols["result"] = decl.return_type;
  }
  for (const auto& ensure : decl.ensures) {
    if (!predicate_names.insert("ensures:" + ensure.name).second) {
      throw Diagnostic(ensure.location, "duplicate postcondition '" + ensure.name + "'");
    }
    validate_predicate(ensure, post_symbols, "postcondition");
  }

  SymbolTable locals = params;
  for (const auto& statement : decl.body) {
    if (statement.kind == StatementKind::Let) {
      require_value_type(statement.type, statement.location,
                         "local '" + decl.name + "." + statement.name + "'");
      const auto actual = infer_expr(statement.expr, locals);
      if (!same_type(actual, statement.type)) {
        throw Diagnostic(statement.location, "let type mismatch: expected " +
                                                 statement.type.display() + ", found " +
                                                 actual.display());
      }
      insert_symbol(locals, statement.name, statement.type, statement.location, "local");
    } else if (statement.kind == StatementKind::Assume) {
      require_type(statement.expr, locals, TypeKind::Bool, "assume statement");
    } else if (statement.kind == StatementKind::Assert) {
      require_type(statement.expr, locals, TypeKind::Bool, "assert statement");
    } else if (statement.kind == StatementKind::Return) {
      if (decl.return_type.kind == TypeKind::Void) {
        throw Diagnostic(statement.location, "void functions cannot return a value yet");
      }
      const auto actual = infer_expr(statement.expr, locals);
      if (!same_type(actual, decl.return_type)) {
        throw Diagnostic(statement.location, "return type mismatch: expected " +
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
      throw Diagnostic(decl.location, "duplicate struct '" + decl.name + "'");
    }
    validate_struct(decl);
  }

  std::unordered_set<std::string> function_names;
  for (const auto& decl : module.functions) {
    if (!function_names.insert(decl.name).second) {
      throw Diagnostic(decl.location, "duplicate function '" + decl.name + "'");
    }
    validate_function(decl);
  }
}

} // namespace sigil

#include "sigil/ast.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace sigil {

std::string SourceLocation::display() const {
  std::ostringstream out;
  if (!file.empty()) {
    out << file << ":";
  }
  out << line << ":" << column;
  return out.str();
}

Diagnostic::Diagnostic(SourceLocation location, const std::string& message)
    : std::runtime_error(location.display() + ": " + message), location_(std::move(location)) {}

Type Type::from_name(const std::string& name) {
  if (name == "i64") {
    return {TypeKind::I64, name};
  }
  if (name == "bool") {
    return {TypeKind::Bool, name};
  }
  if (name == "void") {
    return {TypeKind::Void, name};
  }
  return {TypeKind::Unknown, name};
}

std::string Type::display() const {
  if (!spelling.empty()) {
    return spelling;
  }
  switch (kind) {
  case TypeKind::I64:
    return "i64";
  case TypeKind::Bool:
    return "bool";
  case TypeKind::Void:
    return "void";
  case TypeKind::Unknown:
    return "unknown";
  }
  return "unknown";
}

std::string Type::smt_sort() const {
  if (kind == TypeKind::Bool) {
    return "Bool";
  }
  return "Int";
}

Expr make_integer(std::int64_t value, SourceLocation location) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Integer;
  expr->integer_value = value;
  expr->location = std::move(location);
  return expr;
}

Expr make_boolean(bool value, SourceLocation location) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Boolean;
  expr->boolean_value = value;
  expr->location = std::move(location);
  return expr;
}

Expr make_identifier(std::string name, SourceLocation location) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Identifier;
  expr->name = std::move(name);
  expr->location = std::move(location);
  return expr;
}

Expr make_unary(UnaryOp op, Expr operand, SourceLocation location) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Unary;
  expr->unary_op = op;
  expr->lhs = std::move(operand);
  expr->location = std::move(location);
  return expr;
}

Expr make_binary(BinaryOp op, Expr lhs, Expr rhs, SourceLocation location) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Binary;
  expr->binary_op = op;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  expr->location = std::move(location);
  return expr;
}

namespace {

std::string unary_display(UnaryOp op) {
  switch (op) {
  case UnaryOp::Not:
    return "!";
  case UnaryOp::Negate:
    return "-";
  }
  return "?";
}

std::string binary_display(BinaryOp op) {
  switch (op) {
  case BinaryOp::Or:
    return "||";
  case BinaryOp::And:
    return "&&";
  case BinaryOp::Equal:
    return "==";
  case BinaryOp::NotEqual:
    return "!=";
  case BinaryOp::Less:
    return "<";
  case BinaryOp::LessEqual:
    return "<=";
  case BinaryOp::Greater:
    return ">";
  case BinaryOp::GreaterEqual:
    return ">=";
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Subtract:
    return "-";
  case BinaryOp::Multiply:
    return "*";
  case BinaryOp::Divide:
    return "/";
  case BinaryOp::Modulo:
    return "%";
  }
  return "?";
}

std::string smt_binary(BinaryOp op) {
  switch (op) {
  case BinaryOp::Or:
    return "or";
  case BinaryOp::And:
    return "and";
  case BinaryOp::Equal:
    return "=";
  case BinaryOp::Less:
    return "<";
  case BinaryOp::LessEqual:
    return "<=";
  case BinaryOp::Greater:
    return ">";
  case BinaryOp::GreaterEqual:
    return ">=";
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Subtract:
    return "-";
  case BinaryOp::Multiply:
    return "*";
  case BinaryOp::Divide:
    return "div";
  case BinaryOp::Modulo:
    return "mod";
  case BinaryOp::NotEqual:
    return "distinct";
  }
  return "?";
}

std::string sanitize_symbol(const std::string& name) {
  std::string symbol = name;
  std::replace(symbol.begin(), symbol.end(), '.', '_');
  return symbol;
}

} // namespace

std::string display_expr(const Expr& expr) {
  if (!expr) {
    return "<missing>";
  }
  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return std::to_string(expr->integer_value);
  case ExprNode::Kind::Boolean:
    return expr->boolean_value ? "true" : "false";
  case ExprNode::Kind::Identifier:
    return expr->name;
  case ExprNode::Kind::Unary:
    return unary_display(expr->unary_op) + display_expr(expr->lhs);
  case ExprNode::Kind::Binary:
    return "(" + display_expr(expr->lhs) + " " + binary_display(expr->binary_op) + " " +
           display_expr(expr->rhs) + ")";
  }
  return "<expr>";
}

std::string emit_smt_expr(const Expr& expr) {
  if (!expr) {
    throw std::runtime_error("cannot emit missing expression");
  }
  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return std::to_string(expr->integer_value);
  case ExprNode::Kind::Boolean:
    return expr->boolean_value ? "true" : "false";
  case ExprNode::Kind::Identifier:
    return sanitize_symbol(expr->name);
  case ExprNode::Kind::Unary:
    if (expr->unary_op == UnaryOp::Not) {
      return "(not " + emit_smt_expr(expr->lhs) + ")";
    }
    return "(- " + emit_smt_expr(expr->lhs) + ")";
  case ExprNode::Kind::Binary:
    return "(" + smt_binary(expr->binary_op) + " " + emit_smt_expr(expr->lhs) + " " +
           emit_smt_expr(expr->rhs) + ")";
  }
  throw std::runtime_error("unknown expression kind");
}

void collect_identifiers(const Expr& expr, std::vector<std::string>& names) {
  if (!expr) {
    return;
  }
  if (expr->kind == ExprNode::Kind::Identifier) {
    if (std::find(names.begin(), names.end(), expr->name) == names.end()) {
      names.push_back(expr->name);
    }
    return;
  }
  collect_identifiers(expr->lhs, names);
  collect_identifiers(expr->rhs, names);
}

} // namespace sigil

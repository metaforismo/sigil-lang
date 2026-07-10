#include "sigil/ast.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sigil {

std::string SourceLocation::display() const {
  std::ostringstream out;
  if (!file.empty()) {
    out << file << ":";
  }
  out << line << ":" << column;
  return out.str();
}

namespace {

SourceLocation inclusive_range_end(const SourceRange& range) {
  auto end = range.end;
  if (end.line < range.start.line ||
      (end.line == range.start.line && end.column <= range.start.column)) {
    return range.start;
  }
  if (end.column > 1) {
    --end.column;
  }
  return end;
}

} // namespace

std::string SourceRange::display() const {
  const auto inclusive_end = inclusive_range_end(*this);
  if (start.file != inclusive_end.file) {
    return start.display() + "-" + inclusive_end.display();
  }
  if (start.line != inclusive_end.line) {
    std::ostringstream out;
    if (!start.file.empty()) {
      out << start.file << ":";
    }
    out << start.line << ":" << start.column << "-" << inclusive_end.line << ":"
        << inclusive_end.column;
    return out.str();
  }
  if (start.column == inclusive_end.column) {
    return start.display();
  }
  std::ostringstream out;
  if (!start.file.empty()) {
    out << start.file << ":";
  }
  out << start.line << ":" << start.column << "-" << inclusive_end.column;
  return out.str();
}

Diagnostic::Diagnostic(SourceLocation location, const std::string& message)
    : Diagnostic(SourceRange{location, location}, message) {}

Diagnostic::Diagnostic(SourceRange range, const std::string& message)
    : std::runtime_error(range.display() + ": " + message), range_(std::move(range)) {}

Type Type::from_name(const std::string& name) {
  if (name == "i64") {
    return {TypeKind::I64, name, {}};
  }
  if (name == "bool") {
    return {TypeKind::Bool, name, {}};
  }
  if (name == "void") {
    return {TypeKind::Void, name, {}};
  }
  return {TypeKind::Unknown, name, {}};
}

std::string Type::display() const {
  std::string base = spelling;
  if (base.empty()) {
    switch (kind) {
    case TypeKind::I64:
      base = "i64";
      break;
    case TypeKind::Bool:
      base = "bool";
      break;
    case TypeKind::Void:
      base = "void";
      break;
    case TypeKind::Unknown:
      base = "unknown";
      break;
    }
  }
  if (arguments.empty()) {
    return base;
  }
  std::ostringstream out;
  out << base << "[";
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << arguments[index].display();
  }
  out << "]";
  return out.str();
}

std::string Type::smt_sort() const {
  if (kind == TypeKind::Unknown && spelling == "__sigil_model_data" && arguments.size() == 1) {
    return "(Array Int " + arguments[0].smt_sort() + ")";
  }
  if (kind == TypeKind::Bool) {
    return "Bool";
  }
  return "Int";
}

Expr make_integer(std::int64_t value, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Integer;
  expr->integer_value = value;
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_integer(std::int64_t value, SourceLocation location) {
  return make_integer(value, SourceRange{location, location});
}

Expr make_boolean(bool value, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Boolean;
  expr->boolean_value = value;
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_boolean(bool value, SourceLocation location) {
  return make_boolean(value, SourceRange{location, location});
}

Expr make_identifier(std::string name, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Identifier;
  expr->name = std::move(name);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_identifier(std::string name, SourceLocation location) {
  return make_identifier(std::move(name), SourceRange{location, location});
}

Expr make_call(std::string callee, std::vector<Expr> arguments, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Call;
  expr->name = std::move(callee);
  expr->arguments = std::move(arguments);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_struct_literal(Type type, std::vector<FieldInitializer> fields, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::StructLiteral;
  expr->name = type.spelling;
  expr->literal_type = std::move(type);
  expr->field_initializers = std::move(fields);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_struct_literal(std::string type_name, std::vector<FieldInitializer> fields,
                         SourceRange range) {
  return make_struct_literal(Type::from_name(type_name), std::move(fields), std::move(range));
}

Expr make_field_access(Expr base, std::string field_name, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::FieldAccess;
  expr->lhs = std::move(base);
  expr->name = std::move(field_name);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_unary(UnaryOp op, Expr operand, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Unary;
  expr->unary_op = op;
  expr->lhs = std::move(operand);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_unary(UnaryOp op, Expr operand, SourceLocation location) {
  return make_unary(op, std::move(operand), SourceRange{location, location});
}

Expr make_binary(BinaryOp op, Expr lhs, Expr rhs, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::Binary;
  expr->binary_op = op;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_binary(BinaryOp op, Expr lhs, Expr rhs, SourceLocation location) {
  return make_binary(op, std::move(lhs), std::move(rhs), SourceRange{location, location});
}

Expr make_if(Expr condition, Expr then_branch, Expr else_branch, SourceRange range) {
  auto expr = std::make_shared<ExprNode>();
  expr->kind = ExprNode::Kind::If;
  expr->condition = std::move(condition);
  expr->lhs = std::move(then_branch);
  expr->rhs = std::move(else_branch);
  expr->location = range.start;
  expr->range = std::move(range);
  return expr;
}

Expr make_if(Expr condition, Expr then_branch, Expr else_branch, SourceLocation location) {
  return make_if(std::move(condition), std::move(then_branch), std::move(else_branch),
                 SourceRange{location, location});
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

std::string display_arguments(const std::vector<Expr>& arguments) {
  std::ostringstream out;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << display_expr(arguments[index]);
  }
  return out.str();
}

std::string display_field_initializers(const std::vector<FieldInitializer>& fields) {
  std::ostringstream out;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << fields[index].name << ": " << display_expr(fields[index].expr);
  }
  return out.str();
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
  case ExprNode::Kind::Call:
    if (expr->name == "__sigil_select" && expr->arguments.size() == 2) {
      return "select(" + display_arguments(expr->arguments) + ")";
    }
    if (expr->name == "__sigil_store" && expr->arguments.size() == 3) {
      return "store(" + display_arguments(expr->arguments) + ")";
    }
    if ((expr->name == "__sigil_const_i64_array" || expr->name == "__sigil_const_bool_array") &&
        expr->arguments.size() == 1) {
      return "const_array(" + display_arguments(expr->arguments) + ")";
    }
    return expr->name + "(" + display_arguments(expr->arguments) + ")";
  case ExprNode::Kind::StructLiteral:
    return expr->literal_type.display() + " { " +
           display_field_initializers(expr->field_initializers) + " }";
  case ExprNode::Kind::FieldAccess:
    return display_expr(expr->lhs) + "." + expr->name;
  case ExprNode::Kind::Unary:
    return unary_display(expr->unary_op) + display_expr(expr->lhs);
  case ExprNode::Kind::Binary:
    return "(" + display_expr(expr->lhs) + " " + binary_display(expr->binary_op) + " " +
           display_expr(expr->rhs) + ")";
  case ExprNode::Kind::If:
    return "(if " + display_expr(expr->condition) + " { " + display_expr(expr->lhs) + " } else { " +
           display_expr(expr->rhs) + " })";
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
  case ExprNode::Kind::Call:
    if (expr->name == "__sigil_select" && expr->arguments.size() == 2) {
      return "(select " + emit_smt_expr(expr->arguments[0]) + " " +
             emit_smt_expr(expr->arguments[1]) + ")";
    }
    if (expr->name == "__sigil_store" && expr->arguments.size() == 3) {
      return "(store " + emit_smt_expr(expr->arguments[0]) + " " +
             emit_smt_expr(expr->arguments[1]) + " " + emit_smt_expr(expr->arguments[2]) + ")";
    }
    if (expr->name == "__sigil_const_i64_array" && expr->arguments.size() == 1) {
      return "((as const (Array Int Int)) " + emit_smt_expr(expr->arguments[0]) + ")";
    }
    if (expr->name == "__sigil_const_bool_array" && expr->arguments.size() == 1) {
      return "((as const (Array Int Bool)) " + emit_smt_expr(expr->arguments[0]) + ")";
    }
    throw std::runtime_error("cannot emit unresolved call expression '" + expr->name + "'");
  case ExprNode::Kind::StructLiteral:
    throw std::runtime_error("cannot emit unresolved struct literal '" +
                             expr->literal_type.display() + "'");
  case ExprNode::Kind::FieldAccess:
    throw std::runtime_error("cannot emit unresolved field access '" + display_expr(expr) + "'");
  case ExprNode::Kind::Unary:
    if (expr->unary_op == UnaryOp::Not) {
      return "(not " + emit_smt_expr(expr->lhs) + ")";
    }
    return "(- " + emit_smt_expr(expr->lhs) + ")";
  case ExprNode::Kind::Binary:
    return "(" + smt_binary(expr->binary_op) + " " + emit_smt_expr(expr->lhs) + " " +
           emit_smt_expr(expr->rhs) + ")";
  case ExprNode::Kind::If:
    return "(ite " + emit_smt_expr(expr->condition) + " " + emit_smt_expr(expr->lhs) + " " +
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
  collect_identifiers(expr->condition, names);
  collect_identifiers(expr->lhs, names);
  collect_identifiers(expr->rhs, names);
  for (const auto& argument : expr->arguments) {
    collect_identifiers(argument, names);
  }
  for (const auto& field : expr->field_initializers) {
    collect_identifiers(field.expr, names);
  }
}

} // namespace sigil

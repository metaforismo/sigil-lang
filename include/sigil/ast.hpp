#pragma once

#include "sigil/diagnostics.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sigil {

enum class TypeKind {
  I64,
  Bool,
  Void,
  Unknown,
};

struct Type {
  TypeKind kind = TypeKind::Unknown;
  std::string spelling;

  static Type from_name(const std::string& name);
  std::string display() const;
  std::string smt_sort() const;
  bool is_integer() const {
    return kind == TypeKind::I64;
  }
  bool is_bool() const {
    return kind == TypeKind::Bool;
  }
};

enum class UnaryOp {
  Not,
  Negate,
};

enum class BinaryOp {
  Or,
  And,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
};

struct ExprNode;
using Expr = std::shared_ptr<ExprNode>;

struct ExprNode {
  enum class Kind {
    Integer,
    Boolean,
    Identifier,
    Unary,
    Binary,
    If,
  };

  Kind kind = Kind::Identifier;
  SourceLocation location;
  SourceRange range;
  std::int64_t integer_value = 0;
  bool boolean_value = false;
  std::string name;
  UnaryOp unary_op = UnaryOp::Not;
  BinaryOp binary_op = BinaryOp::Equal;
  Expr condition;
  Expr lhs;
  Expr rhs;
};

Expr make_integer(std::int64_t value, SourceRange range);
Expr make_integer(std::int64_t value, SourceLocation location = {});
Expr make_boolean(bool value, SourceRange range);
Expr make_boolean(bool value, SourceLocation location = {});
Expr make_identifier(std::string name, SourceRange range);
Expr make_identifier(std::string name, SourceLocation location = {});
Expr make_unary(UnaryOp op, Expr operand, SourceRange range);
Expr make_unary(UnaryOp op, Expr operand, SourceLocation location = {});
Expr make_binary(BinaryOp op, Expr lhs, Expr rhs, SourceRange range);
Expr make_binary(BinaryOp op, Expr lhs, Expr rhs, SourceLocation location = {});
Expr make_if(Expr condition, Expr then_branch, Expr else_branch, SourceRange range);
Expr make_if(Expr condition, Expr then_branch, Expr else_branch, SourceLocation location = {});

std::string display_expr(const Expr& expr);
std::string emit_smt_expr(const Expr& expr);
void collect_identifiers(const Expr& expr, std::vector<std::string>& names);

struct NamedPredicate {
  std::string name;
  Expr expr;
  SourceLocation location;
  SourceRange range;
};

struct FieldDecl {
  std::string name;
  Type type;
  SourceLocation location;
  SourceRange range;
};

struct StructDecl {
  std::string name;
  std::vector<FieldDecl> fields;
  std::vector<NamedPredicate> invariants;
  SourceLocation location;
  SourceRange range;
};

struct ParamDecl {
  std::string name;
  Type type;
  SourceLocation location;
  SourceRange range;
};

enum class StatementKind {
  Let,
  Assign,
  If,
  Assume,
  Assert,
  Return,
};

struct Statement {
  StatementKind kind = StatementKind::Assume;
  std::string name;
  Type type;
  Expr expr;
  std::vector<Statement> then_branch;
  std::vector<Statement> else_branch;
  SourceLocation location;
  SourceRange range;
};

struct FunctionDecl {
  std::string name;
  std::vector<ParamDecl> params;
  Type return_type;
  std::vector<NamedPredicate> preconditions;
  std::vector<NamedPredicate> ensures;
  std::vector<Statement> body;
  SourceLocation location;
  SourceRange range;
};

struct Module {
  std::string name;
  std::vector<StructDecl> structs;
  std::vector<FunctionDecl> functions;
  SourceLocation location;
  SourceRange range;
};

using SymbolTable = std::unordered_map<std::string, Type>;

} // namespace sigil

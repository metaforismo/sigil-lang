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
  std::vector<Type> arguments;

  static Type from_name(const std::string& name);
  std::string display() const;
  std::string smt_sort() const;
  bool is_integer() const {
    return kind == TypeKind::I64;
  }
  bool is_bool() const {
    return kind == TypeKind::Bool;
  }
  bool has_arguments() const {
    return !arguments.empty();
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

struct FieldInitializer {
  std::string name;
  Expr expr;
  SourceLocation location;
  SourceRange range;
};

struct ExprNode {
  enum class Kind {
    Integer,
    Boolean,
    Identifier,
    Call,
    StructLiteral,
    FieldAccess,
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
  Type literal_type;
  UnaryOp unary_op = UnaryOp::Not;
  BinaryOp binary_op = BinaryOp::Equal;
  Expr condition;
  Expr lhs;
  Expr rhs;
  std::vector<Expr> arguments;
  std::vector<FieldInitializer> field_initializers;
};

Expr make_integer(std::int64_t value, SourceRange range);
Expr make_integer(std::int64_t value, SourceLocation location = {});
Expr make_boolean(bool value, SourceRange range);
Expr make_boolean(bool value, SourceLocation location = {});
Expr make_identifier(std::string name, SourceRange range);
Expr make_identifier(std::string name, SourceLocation location = {});
Expr make_call(std::string callee, std::vector<Expr> arguments, SourceRange range);
Expr make_struct_literal(Type type, std::vector<FieldInitializer> fields, SourceRange range);
Expr make_struct_literal(std::string type_name, std::vector<FieldInitializer> fields,
                         SourceRange range);
Expr make_field_access(Expr base, std::string field_name, SourceRange range);
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

struct TypeParamDecl {
  std::string name;
  SourceLocation location;
  SourceRange range;
};

struct StructDecl {
  std::string name;
  bool is_container = false;
  std::vector<TypeParamDecl> type_params;
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
  While,
  Assume,
  Assert,
  Return,
};

struct Statement {
  StatementKind kind = StatementKind::Assume;
  std::string name;
  bool has_explicit_label = false;
  Type type;
  Expr expr;
  std::vector<NamedPredicate> loop_invariants;
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

struct TheoremDecl {
  std::string name;
  std::vector<ParamDecl> params;
  std::vector<NamedPredicate> preconditions;
  std::vector<NamedPredicate> ensures;
  std::vector<Statement> body;
  SourceLocation location;
  SourceRange range;
};

struct Module {
  std::string name;
  std::vector<StructDecl> structs;
  std::vector<TheoremDecl> theorems;
  std::vector<FunctionDecl> functions;
  SourceLocation location;
  SourceRange range;
};

using SymbolTable = std::unordered_map<std::string, Type>;

} // namespace sigil

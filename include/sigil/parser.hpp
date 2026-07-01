#pragma once

#include "sigil/ast.hpp"
#include "sigil/lexer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sigil {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  Module parse_module();

private:
  bool is_at_end() const;
  const Token& peek() const;
  const Token& previous() const;
  const Token& advance();
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  Token consume(TokenKind kind, const std::string& message);

  StructDecl parse_struct();
  FunctionDecl parse_function();
  FieldDecl parse_field();
  NamedPredicate parse_named_predicate(TokenKind keyword);
  Statement parse_statement();
  std::vector<ParamDecl> parse_params();
  Type parse_type();

  Expr parse_expr();
  Expr parse_or();
  Expr parse_and();
  Expr parse_equality();
  Expr parse_comparison();
  Expr parse_term();
  Expr parse_factor();
  Expr parse_unary();
  Expr parse_primary();

  std::vector<Token> tokens_;
  std::size_t current_ = 0;
};

Module parse_source(std::string_view source, const std::string& file_name);

} // namespace sigil

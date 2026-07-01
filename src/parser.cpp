#include "sigil/parser.hpp"

#include <cstdlib>
#include <sstream>

namespace sigil {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Module Parser::parse_module() {
  Module module;
  const auto module_token = consume(TokenKind::Module, "expected module declaration");
  module.location = module_token.location;
  module.name = consume(TokenKind::Identifier, "expected module name").text;
  consume(TokenKind::Semicolon, "expected ';' after module declaration");

  while (!is_at_end()) {
    if (check(TokenKind::Struct)) {
      module.structs.push_back(parse_struct());
    } else if (check(TokenKind::Fn)) {
      module.functions.push_back(parse_function());
    } else {
      throw Diagnostic(peek().location, "expected struct or function declaration");
    }
  }
  return module;
}

bool Parser::is_at_end() const {
  return peek().kind == TokenKind::End;
}

const Token& Parser::peek() const {
  return tokens_[current_];
}

const Token& Parser::previous() const {
  return tokens_[current_ - 1];
}

const Token& Parser::advance() {
  if (!is_at_end()) {
    ++current_;
  }
  return previous();
}

bool Parser::check(TokenKind kind) const {
  return !is_at_end() && peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  advance();
  return true;
}

Token Parser::consume(TokenKind kind, const std::string& message) {
  if (check(kind)) {
    return advance();
  }
  std::ostringstream out;
  out << message << ", found " << token_name(peek().kind);
  throw Diagnostic(peek().location, out.str());
}

StructDecl Parser::parse_struct() {
  const auto start = consume(TokenKind::Struct, "expected 'struct'");
  StructDecl decl;
  decl.location = start.location;
  decl.name = consume(TokenKind::Identifier, "expected struct name").text;
  consume(TokenKind::LBrace, "expected '{' after struct name");
  while (!check(TokenKind::RBrace)) {
    if (check(TokenKind::Invariant)) {
      decl.invariants.push_back(parse_named_predicate(TokenKind::Invariant));
    } else {
      decl.fields.push_back(parse_field());
    }
  }
  consume(TokenKind::RBrace, "expected '}' after struct body");
  return decl;
}

FunctionDecl Parser::parse_function() {
  const auto start = consume(TokenKind::Fn, "expected 'fn'");
  FunctionDecl decl;
  decl.location = start.location;
  decl.name = consume(TokenKind::Identifier, "expected function name").text;
  consume(TokenKind::LParen, "expected '(' after function name");
  decl.params = parse_params();
  consume(TokenKind::RParen, "expected ')' after function parameters");
  consume(TokenKind::Arrow, "expected '->' before return type");
  decl.return_type = parse_type();

  while (check(TokenKind::Requires) || check(TokenKind::Ensures)) {
    if (check(TokenKind::Requires)) {
      decl.preconditions.push_back(parse_named_predicate(TokenKind::Requires));
    } else {
      decl.ensures.push_back(parse_named_predicate(TokenKind::Ensures));
    }
  }

  consume(TokenKind::LBrace, "expected '{' before function body");
  while (!check(TokenKind::RBrace)) {
    decl.body.push_back(parse_statement());
  }
  consume(TokenKind::RBrace, "expected '}' after function body");
  return decl;
}

FieldDecl Parser::parse_field() {
  FieldDecl field;
  const auto name = consume(TokenKind::Identifier, "expected field name");
  field.location = name.location;
  field.name = name.text;
  consume(TokenKind::Colon, "expected ':' after field name");
  field.type = parse_type();
  consume(TokenKind::Semicolon, "expected ';' after field declaration");
  return field;
}

NamedPredicate Parser::parse_named_predicate(TokenKind keyword) {
  const auto start = consume(keyword, "expected predicate keyword");
  NamedPredicate predicate;
  predicate.location = start.location;
  predicate.name = consume(TokenKind::Identifier, "expected predicate name").text;
  consume(TokenKind::Colon, "expected ':' after predicate name");
  predicate.expr = parse_expr();
  consume(TokenKind::Semicolon, "expected ';' after predicate");
  return predicate;
}

Statement Parser::parse_statement() {
  Statement statement;
  if (match(TokenKind::Let)) {
    statement.kind = StatementKind::Let;
    statement.location = previous().location;
    statement.name = consume(TokenKind::Identifier, "expected local binding name").text;
    consume(TokenKind::Colon, "expected ':' after local binding name");
    statement.type = parse_type();
    consume(TokenKind::Equal, "expected '=' before local binding expression");
    statement.expr = parse_expr();
    consume(TokenKind::Semicolon, "expected ';' after let statement");
    return statement;
  }

  if (match(TokenKind::Assume) || match(TokenKind::Assert)) {
    const auto keyword = previous();
    statement.kind =
        keyword.kind == TokenKind::Assume ? StatementKind::Assume : StatementKind::Assert;
    statement.location = keyword.location;
    if (check(TokenKind::Identifier) && tokens_[current_ + 1].kind == TokenKind::Colon) {
      statement.name = advance().text;
      consume(TokenKind::Colon, "expected ':' after statement name");
    } else {
      statement.name = statement.kind == StatementKind::Assume ? "assume" : "assert";
    }
    statement.expr = parse_expr();
    consume(TokenKind::Semicolon, "expected ';' after statement");
    return statement;
  }

  if (match(TokenKind::Return)) {
    statement.kind = StatementKind::Return;
    statement.location = previous().location;
    statement.name = "return";
    statement.expr = parse_expr();
    consume(TokenKind::Semicolon, "expected ';' after return statement");
    return statement;
  }

  throw Diagnostic(peek().location, "expected let, assume, assert, or return statement");
}

std::vector<ParamDecl> Parser::parse_params() {
  std::vector<ParamDecl> params;
  if (check(TokenKind::RParen)) {
    return params;
  }
  do {
    ParamDecl param;
    const auto name = consume(TokenKind::Identifier, "expected parameter name");
    param.location = name.location;
    param.name = name.text;
    consume(TokenKind::Colon, "expected ':' after parameter name");
    param.type = parse_type();
    params.push_back(std::move(param));
  } while (match(TokenKind::Comma));
  return params;
}

Type Parser::parse_type() {
  const auto token = consume(TokenKind::Identifier, "expected type name");
  return Type::from_name(token.text);
}

Expr Parser::parse_expr() {
  return parse_or();
}

Expr Parser::parse_or() {
  auto expr = parse_and();
  while (match(TokenKind::OrOr)) {
    const auto op = previous();
    expr = make_binary(BinaryOp::Or, expr, parse_and(), op.location);
  }
  return expr;
}

Expr Parser::parse_and() {
  auto expr = parse_equality();
  while (match(TokenKind::AndAnd)) {
    const auto op = previous();
    expr = make_binary(BinaryOp::And, expr, parse_equality(), op.location);
  }
  return expr;
}

Expr Parser::parse_equality() {
  auto expr = parse_comparison();
  while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
    const auto op = previous();
    expr = make_binary(op.kind == TokenKind::EqualEqual ? BinaryOp::Equal : BinaryOp::NotEqual,
                       expr, parse_comparison(), op.location);
  }
  return expr;
}

Expr Parser::parse_comparison() {
  auto expr = parse_term();
  while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
         match(TokenKind::GreaterEqual)) {
    const auto op = previous();
    BinaryOp binary = BinaryOp::Less;
    if (op.kind == TokenKind::LessEqual) {
      binary = BinaryOp::LessEqual;
    } else if (op.kind == TokenKind::Greater) {
      binary = BinaryOp::Greater;
    } else if (op.kind == TokenKind::GreaterEqual) {
      binary = BinaryOp::GreaterEqual;
    }
    expr = make_binary(binary, expr, parse_term(), op.location);
  }
  return expr;
}

Expr Parser::parse_term() {
  auto expr = parse_factor();
  while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
    const auto op = previous();
    expr = make_binary(op.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract, expr,
                       parse_factor(), op.location);
  }
  return expr;
}

Expr Parser::parse_factor() {
  auto expr = parse_unary();
  while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent)) {
    const auto op = previous();
    BinaryOp binary = BinaryOp::Multiply;
    if (op.kind == TokenKind::Slash) {
      binary = BinaryOp::Divide;
    } else if (op.kind == TokenKind::Percent) {
      binary = BinaryOp::Modulo;
    }
    expr = make_binary(binary, expr, parse_unary(), op.location);
  }
  return expr;
}

Expr Parser::parse_unary() {
  if (match(TokenKind::Bang) || match(TokenKind::Minus)) {
    const auto op = previous();
    return make_unary(op.kind == TokenKind::Bang ? UnaryOp::Not : UnaryOp::Negate, parse_unary(),
                      op.location);
  }
  return parse_primary();
}

Expr Parser::parse_primary() {
  if (match(TokenKind::Number)) {
    return make_integer(std::strtoll(previous().text.c_str(), nullptr, 10), previous().location);
  }
  if (match(TokenKind::True)) {
    return make_boolean(true, previous().location);
  }
  if (match(TokenKind::False)) {
    return make_boolean(false, previous().location);
  }
  if (match(TokenKind::If)) {
    const auto if_token = previous();
    auto condition = parse_expr();
    consume(TokenKind::LBrace, "expected '{' before then expression");
    auto then_branch = parse_expr();
    consume(TokenKind::RBrace, "expected '}' after then expression");
    consume(TokenKind::Else, "expected 'else' after then expression");
    consume(TokenKind::LBrace, "expected '{' before else expression");
    auto else_branch = parse_expr();
    consume(TokenKind::RBrace, "expected '}' after else expression");
    return make_if(condition, then_branch, else_branch, if_token.location);
  }
  if (match(TokenKind::Identifier)) {
    return make_identifier(previous().text, previous().location);
  }
  if (match(TokenKind::LParen)) {
    auto expr = parse_expr();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }
  throw Diagnostic(peek().location, "expected expression");
}

Module parse_source(std::string_view source, const std::string& file_name) {
  Lexer lexer(source, file_name);
  Parser parser(lexer.tokenize());
  return parser.parse_module();
}

} // namespace sigil

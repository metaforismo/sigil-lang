#include "sigil/parser.hpp"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace sigil {

namespace {

SourceRange span(SourceRange start, SourceRange end) {
  return SourceRange{std::move(start.start), std::move(end.end)};
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Module Parser::parse_module() {
  Module module;
  const auto module_token = consume(TokenKind::Module, "expected module declaration");
  module.location = module_token.location;
  module.range = module_token.range;
  module.name = consume(TokenKind::Identifier, "expected module name").text;
  const auto module_semicolon =
      consume(TokenKind::Semicolon, "expected ';' after module declaration");
  module.range = span(module_token.range, module_semicolon.range);

  while (!is_at_end()) {
    if (check(TokenKind::Struct)) {
      module.structs.push_back(parse_struct());
      module.range.end = module.structs.back().range.end;
    } else if (check(TokenKind::Fn)) {
      module.functions.push_back(parse_function());
      module.range.end = module.functions.back().range.end;
    } else {
      throw Diagnostic(peek().range, "expected struct or function declaration");
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
  throw Diagnostic(peek().range, out.str());
}

StructDecl Parser::parse_struct() {
  const auto start = consume(TokenKind::Struct, "expected 'struct'");
  StructDecl decl;
  decl.location = start.location;
  decl.range = start.range;
  decl.name = consume(TokenKind::Identifier, "expected struct name").text;
  consume(TokenKind::LBrace, "expected '{' after struct name");
  while (!check(TokenKind::RBrace)) {
    if (check(TokenKind::Invariant)) {
      decl.invariants.push_back(parse_named_predicate(TokenKind::Invariant));
    } else {
      decl.fields.push_back(parse_field());
    }
  }
  const auto end = consume(TokenKind::RBrace, "expected '}' after struct body");
  decl.range = span(start.range, end.range);
  return decl;
}

FunctionDecl Parser::parse_function() {
  const auto start = consume(TokenKind::Fn, "expected 'fn'");
  FunctionDecl decl;
  decl.location = start.location;
  decl.range = start.range;
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

  decl.body = parse_statement_block("function body");
  const auto end = previous();
  decl.range = span(start.range, end.range);
  return decl;
}

FieldDecl Parser::parse_field() {
  FieldDecl field;
  const auto name = consume(TokenKind::Identifier, "expected field name");
  field.location = name.location;
  field.range = name.range;
  field.name = name.text;
  consume(TokenKind::Colon, "expected ':' after field name");
  field.type = parse_type();
  const auto end = consume(TokenKind::Semicolon, "expected ';' after field declaration");
  field.range = span(name.range, end.range);
  return field;
}

NamedPredicate Parser::parse_named_predicate(TokenKind keyword) {
  const auto start = consume(keyword, "expected predicate keyword");
  NamedPredicate predicate;
  predicate.location = start.location;
  predicate.range = start.range;
  predicate.name = consume(TokenKind::Identifier, "expected predicate name").text;
  consume(TokenKind::Colon, "expected ':' after predicate name");
  predicate.expr = parse_expr();
  const auto end = consume(TokenKind::Semicolon, "expected ';' after predicate");
  predicate.range = span(start.range, end.range);
  return predicate;
}

Statement Parser::parse_statement() {
  Statement statement;
  if (match(TokenKind::Let)) {
    statement.kind = StatementKind::Let;
    const auto start = previous();
    statement.location = start.location;
    statement.range = start.range;
    statement.name = consume(TokenKind::Identifier, "expected local binding name").text;
    consume(TokenKind::Colon, "expected ':' after local binding name");
    statement.type = parse_type();
    consume(TokenKind::Equal, "expected '=' before local binding expression");
    statement.expr = parse_expr();
    const auto end = consume(TokenKind::Semicolon, "expected ';' after let statement");
    statement.range = span(start.range, end.range);
    return statement;
  }

  if (check(TokenKind::Identifier) && tokens_[current_ + 1].kind == TokenKind::Equal) {
    const auto start = advance();
    statement.kind = StatementKind::Assign;
    statement.location = start.location;
    statement.range = start.range;
    statement.name = start.text;
    consume(TokenKind::Equal, "expected '=' before assignment expression");
    statement.expr = parse_expr();
    const auto end = consume(TokenKind::Semicolon, "expected ';' after assignment");
    statement.range = span(start.range, end.range);
    return statement;
  }

  if (match(TokenKind::If)) {
    const auto start = previous();
    statement.kind = StatementKind::If;
    statement.location = start.location;
    statement.range = start.range;
    statement.name = "if";
    statement.expr = parse_expr();
    statement.then_branch = parse_statement_block("if then branch");
    consume(TokenKind::Else, "expected 'else' after if then branch");
    statement.else_branch = parse_statement_block("if else branch");
    statement.range = span(start.range, previous().range);
    return statement;
  }

  if (match(TokenKind::While)) {
    const auto start = previous();
    statement.kind = StatementKind::While;
    statement.location = start.location;
    statement.range = start.range;
    statement.name = "while";
    statement.expr = parse_expr();
    if (!check(TokenKind::Invariant)) {
      throw Diagnostic(peek().range, "expected at least one invariant before while body");
    }
    while (check(TokenKind::Invariant)) {
      statement.loop_invariants.push_back(parse_named_predicate(TokenKind::Invariant));
    }
    statement.then_branch = parse_statement_block("while body");
    statement.range = span(start.range, previous().range);
    return statement;
  }

  if (match(TokenKind::Assume) || match(TokenKind::Assert)) {
    const auto keyword = previous();
    statement.kind =
        keyword.kind == TokenKind::Assume ? StatementKind::Assume : StatementKind::Assert;
    statement.location = keyword.location;
    statement.range = keyword.range;
    if (check(TokenKind::Identifier) && tokens_[current_ + 1].kind == TokenKind::Colon) {
      statement.has_explicit_label = true;
      statement.name = advance().text;
      consume(TokenKind::Colon, "expected ':' after statement name");
    } else {
      statement.name = statement.kind == StatementKind::Assume ? "assume" : "assert";
    }
    statement.expr = parse_expr();
    const auto end = consume(TokenKind::Semicolon, "expected ';' after statement");
    statement.range = span(keyword.range, end.range);
    return statement;
  }

  if (match(TokenKind::Return)) {
    statement.kind = StatementKind::Return;
    const auto start = previous();
    statement.location = start.location;
    statement.range = start.range;
    statement.name = "return";
    if (!check(TokenKind::Semicolon)) {
      statement.expr = parse_expr();
    }
    const auto end = consume(TokenKind::Semicolon, "expected ';' after return statement");
    statement.range = span(start.range, end.range);
    return statement;
  }

  throw Diagnostic(peek().range,
                   "expected let, assignment, if, while, assume, assert, or return statement");
}

std::vector<Statement> Parser::parse_statement_block(const std::string& owner) {
  consume(TokenKind::LBrace, "expected '{' before " + owner);
  std::vector<Statement> statements;
  while (!check(TokenKind::RBrace)) {
    statements.push_back(parse_statement());
  }
  consume(TokenKind::RBrace, "expected '}' after " + owner);
  return statements;
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
    param.range = name.range;
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
    auto rhs = parse_and();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(BinaryOp::Or, expr, rhs, range);
  }
  return expr;
}

Expr Parser::parse_and() {
  auto expr = parse_equality();
  while (match(TokenKind::AndAnd)) {
    auto rhs = parse_equality();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(BinaryOp::And, expr, rhs, range);
  }
  return expr;
}

Expr Parser::parse_equality() {
  auto expr = parse_comparison();
  while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
    const auto op = previous();
    auto rhs = parse_comparison();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(op.kind == TokenKind::EqualEqual ? BinaryOp::Equal : BinaryOp::NotEqual,
                       expr, rhs, range);
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
    auto rhs = parse_term();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(binary, expr, rhs, range);
  }
  return expr;
}

Expr Parser::parse_term() {
  auto expr = parse_factor();
  while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
    const auto op = previous();
    auto rhs = parse_factor();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(op.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Subtract, expr, rhs,
                       range);
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
    auto rhs = parse_unary();
    const auto range = span(expr->range, rhs->range);
    expr = make_binary(binary, expr, rhs, range);
  }
  return expr;
}

Expr Parser::parse_unary() {
  if (match(TokenKind::Bang) || match(TokenKind::Minus)) {
    const auto op = previous();
    auto operand = parse_unary();
    return make_unary(op.kind == TokenKind::Bang ? UnaryOp::Not : UnaryOp::Negate, operand,
                      span(op.range, operand->range));
  }
  return parse_primary();
}

Expr Parser::parse_primary() {
  if (match(TokenKind::Number)) {
    const auto token = previous();
    return make_integer(std::strtoll(token.text.c_str(), nullptr, 10), token.range);
  }
  if (match(TokenKind::True)) {
    return make_boolean(true, previous().range);
  }
  if (match(TokenKind::False)) {
    return make_boolean(false, previous().range);
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
    const auto end = consume(TokenKind::RBrace, "expected '}' after else expression");
    return make_if(condition, then_branch, else_branch, span(if_token.range, end.range));
  }
  if (match(TokenKind::Identifier)) {
    const auto token = previous();
    return make_identifier(token.text, token.range);
  }
  if (match(TokenKind::LParen)) {
    auto expr = parse_expr();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }
  throw Diagnostic(peek().range, "expected expression");
}

Module parse_source(std::string_view source, const std::string& file_name) {
  Lexer lexer(source, file_name);
  Parser parser(lexer.tokenize());
  return parser.parse_module();
}

} // namespace sigil

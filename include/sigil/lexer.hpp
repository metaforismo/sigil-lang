#pragma once

#include "sigil/diagnostics.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sigil {

enum class TokenKind {
  End,
  Identifier,
  Number,
  LParen,
  RParen,
  LBrace,
  RBrace,
  Colon,
  Semicolon,
  Comma,
  Arrow,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  BangEqual,
  AndAnd,
  OrOr,
  Module,
  Struct,
  Invariant,
  Fn,
  Requires,
  Ensures,
  Assume,
  Assert,
  Return,
  True,
  False,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  SourceLocation location;
};

class Lexer {
public:
  Lexer(std::string_view source, std::string file_name);

  std::vector<Token> tokenize();

private:
  bool at_end() const;
  char peek() const;
  char peek_next() const;
  char advance();
  bool match(char expected);
  void skip_whitespace_and_comments();
  Token make_token(TokenKind kind, std::size_t start, SourceLocation location) const;
  Token identifier(std::size_t start, SourceLocation location);
  Token number(std::size_t start, SourceLocation location);

  std::string source_;
  std::string file_name_;
  std::size_t current_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

const char* token_name(TokenKind kind);

} // namespace sigil

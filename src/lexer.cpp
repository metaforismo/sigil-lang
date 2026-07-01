#include "sigil/lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace sigil {

Lexer::Lexer(std::string_view source, std::string file_name)
    : source_(source), file_name_(std::move(file_name)) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (!at_end()) {
    skip_whitespace_and_comments();
    if (at_end()) {
      break;
    }

    const auto start = current_;
    const SourceLocation location{file_name_, line_, column_};
    const char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      tokens.push_back(identifier(start, location));
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      tokens.push_back(number(start, location));
      continue;
    }

    switch (c) {
    case '(':
      tokens.push_back(make_token(TokenKind::LParen, start, location));
      break;
    case ')':
      tokens.push_back(make_token(TokenKind::RParen, start, location));
      break;
    case '{':
      tokens.push_back(make_token(TokenKind::LBrace, start, location));
      break;
    case '}':
      tokens.push_back(make_token(TokenKind::RBrace, start, location));
      break;
    case ':':
      tokens.push_back(make_token(TokenKind::Colon, start, location));
      break;
    case ';':
      tokens.push_back(make_token(TokenKind::Semicolon, start, location));
      break;
    case ',':
      tokens.push_back(make_token(TokenKind::Comma, start, location));
      break;
    case '+':
      tokens.push_back(make_token(TokenKind::Plus, start, location));
      break;
    case '-':
      tokens.push_back(
          make_token(match('>') ? TokenKind::Arrow : TokenKind::Minus, start, location));
      break;
    case '*':
      tokens.push_back(make_token(TokenKind::Star, start, location));
      break;
    case '/':
      tokens.push_back(make_token(TokenKind::Slash, start, location));
      break;
    case '%':
      tokens.push_back(make_token(TokenKind::Percent, start, location));
      break;
    case '!':
      tokens.push_back(
          make_token(match('=') ? TokenKind::BangEqual : TokenKind::Bang, start, location));
      break;
    case '<':
      tokens.push_back(
          make_token(match('=') ? TokenKind::LessEqual : TokenKind::Less, start, location));
      break;
    case '>':
      tokens.push_back(
          make_token(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater, start, location));
      break;
    case '=':
      tokens.push_back(
          make_token(match('=') ? TokenKind::EqualEqual : TokenKind::Equal, start, location));
      break;
    case '&':
      if (!match('&')) {
        throw Diagnostic(location, "expected '&' after '&'");
      }
      tokens.push_back(make_token(TokenKind::AndAnd, start, location));
      break;
    case '|':
      if (!match('|')) {
        throw Diagnostic(location, "expected '|' after '|'");
      }
      tokens.push_back(make_token(TokenKind::OrOr, start, location));
      break;
    default:
      throw Diagnostic(location, std::string("unexpected character '") + c + "'");
    }
  }

  tokens.push_back(Token{TokenKind::End, "", SourceLocation{file_name_, line_, column_}});
  return tokens;
}

bool Lexer::at_end() const {
  return current_ >= source_.size();
}

char Lexer::peek() const {
  return at_end() ? '\0' : source_[current_];
}

char Lexer::peek_next() const {
  return current_ + 1 >= source_.size() ? '\0' : source_[current_ + 1];
}

char Lexer::advance() {
  const char c = source_[current_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Lexer::match(char expected) {
  if (at_end() || source_[current_] != expected) {
    return false;
  }
  advance();
  return true;
}

void Lexer::skip_whitespace_and_comments() {
  while (!at_end()) {
    const char c = peek();
    if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
      advance();
      continue;
    }
    if (c == '/' && peek_next() == '/') {
      while (!at_end() && peek() != '\n') {
        advance();
      }
      continue;
    }
    break;
  }
}

Token Lexer::make_token(TokenKind kind, std::size_t start, SourceLocation location) const {
  return Token{kind, source_.substr(start, current_ - start), std::move(location)};
}

Token Lexer::identifier(std::size_t start, SourceLocation location) {
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    advance();
  }
  static const std::unordered_map<std::string, TokenKind> keywords = {
      {"module", TokenKind::Module},
      {"struct", TokenKind::Struct},
      {"invariant", TokenKind::Invariant},
      {"fn", TokenKind::Fn},
      {"requires", TokenKind::Requires},
      {"ensures", TokenKind::Ensures},
      {"let", TokenKind::Let},
      {"assume", TokenKind::Assume},
      {"assert", TokenKind::Assert},
      {"return", TokenKind::Return},
      {"true", TokenKind::True},
      {"false", TokenKind::False},
  };
  const auto text = source_.substr(start, current_ - start);
  const auto found = keywords.find(text);
  return Token{found == keywords.end() ? TokenKind::Identifier : found->second, text,
               std::move(location)};
}

Token Lexer::number(std::size_t start, SourceLocation location) {
  while (std::isdigit(static_cast<unsigned char>(peek()))) {
    advance();
  }
  return make_token(TokenKind::Number, start, std::move(location));
}

const char* token_name(TokenKind kind) {
  switch (kind) {
  case TokenKind::End:
    return "end of file";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::Number:
    return "number";
  case TokenKind::LParen:
    return "'('";
  case TokenKind::RParen:
    return "')'";
  case TokenKind::LBrace:
    return "'{'";
  case TokenKind::RBrace:
    return "'}'";
  case TokenKind::Colon:
    return "':'";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Comma:
    return "','";
  case TokenKind::Arrow:
    return "'->'";
  case TokenKind::Equal:
    return "'='";
  case TokenKind::Plus:
    return "'+'";
  case TokenKind::Minus:
    return "'-'";
  case TokenKind::Star:
    return "'*'";
  case TokenKind::Slash:
    return "'/'";
  case TokenKind::Percent:
    return "'%'";
  case TokenKind::Bang:
    return "'!'";
  case TokenKind::Less:
    return "'<'";
  case TokenKind::Greater:
    return "'>'";
  case TokenKind::LessEqual:
    return "'<='";
  case TokenKind::GreaterEqual:
    return "'>='";
  case TokenKind::EqualEqual:
    return "'=='";
  case TokenKind::BangEqual:
    return "'!='";
  case TokenKind::AndAnd:
    return "'&&'";
  case TokenKind::OrOr:
    return "'||'";
  case TokenKind::Module:
    return "'module'";
  case TokenKind::Struct:
    return "'struct'";
  case TokenKind::Invariant:
    return "'invariant'";
  case TokenKind::Fn:
    return "'fn'";
  case TokenKind::Requires:
    return "'requires'";
  case TokenKind::Ensures:
    return "'ensures'";
  case TokenKind::Let:
    return "'let'";
  case TokenKind::Assume:
    return "'assume'";
  case TokenKind::Assert:
    return "'assert'";
  case TokenKind::Return:
    return "'return'";
  case TokenKind::True:
    return "'true'";
  case TokenKind::False:
    return "'false'";
  }
  return "token";
}

} // namespace sigil

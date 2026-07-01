#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace sigil {

struct SourceLocation {
  std::string file;
  std::size_t line = 1;
  std::size_t column = 1;

  std::string display() const;
};

class Diagnostic : public std::runtime_error {
public:
  Diagnostic(SourceLocation location, const std::string& message);

  const SourceLocation& location() const noexcept { return location_; }

private:
  SourceLocation location_;
};

}  // namespace sigil

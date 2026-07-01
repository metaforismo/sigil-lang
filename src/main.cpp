#include "sigil/gccjit_backend.hpp"
#include "sigil/parser.hpp"
#include "sigil/proof.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("could not open " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void print_help() {
  std::cout << "sigil " << SIGIL_VERSION << "\n\n"
            << "Usage:\n"
            << "  sigil check <file.sigil> [--dump-smt] [--strict] [--no-z3]\n"
            << "  sigil backend\n";
}

int check_command(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_help();
    return 1;
  }

  std::string path;
  bool dump_smt = false;
  bool strict = false;
  bool use_z3 = true;
  for (const auto& arg : args) {
    if (arg == "--dump-smt") {
      dump_smt = true;
    } else if (arg == "--strict") {
      strict = true;
    } else if (arg == "--no-z3") {
      use_z3 = false;
    } else if (path.empty()) {
      path = arg;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  const auto obligations = sigil::build_obligations(module);
  const auto results = sigil::verify_obligations(obligations, use_z3);

  std::size_t invariant_count = 0;
  for (const auto& decl : module.structs) {
    invariant_count += decl.invariants.size();
  }

  std::cout << "module " << module.name << "\n";
  std::cout << "  structs: " << module.structs.size() << "\n";
  std::cout << "  functions: " << module.functions.size() << "\n";
  std::cout << "  registered struct invariants: " << invariant_count << "\n";
  std::cout << "  proof obligations: " << results.size() << "\n";

  bool has_failure = false;
  bool has_unknown = false;
  for (const auto& result : results) {
    std::cout << "[" << sigil::status_name(result.status) << "] " << result.obligation_name
              << " - " << result.details << "\n";
    if (dump_smt) {
      std::cout << result.smt_lib << "\n";
    }
    has_failure = has_failure ||
                  result.status == sigil::VerificationStatus::Refuted ||
                  result.status == sigil::VerificationStatus::Error;
    has_unknown = has_unknown || result.status == sigil::VerificationStatus::Unknown;
  }

  if (has_failure || (strict && has_unknown)) {
    return 2;
  }
  return 0;
}

int backend_command() {
  const auto status = sigil::gccjit_status();
  std::cout << (status.available ? "available" : "unavailable") << ": " << status.detail << "\n";
  return status.available ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
      print_help();
      return 0;
    }

    const auto command = args[0];
    args.erase(args.begin());
    if (command == "check") {
      return check_command(args);
    }
    if (command == "backend") {
      return backend_command();
    }

    std::cerr << "unknown command: " << command << "\n";
    print_help();
    return 1;
  } catch (const sigil::Diagnostic& diagnostic) {
    std::cerr << diagnostic.what() << "\n";
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}

#include "sigil/gccjit_backend.hpp"
#include "sigil/parser.hpp"
#include "sigil/proof.hpp"
#include "sigil/typecheck.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
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
            << "  sigil check <file.sigil> [--dump-smt] [--save-smt <dir>] [--show-model]\n"
            << "                          [--solver-timeout-ms <ms>] [--strict] [--no-z3]\n"
            << "  sigil compile <file.sigil>\n"
            << "  sigil run <file.sigil> <function> [args...]\n"
            << "  sigil backend\n";
}

int parse_positive_int(const std::string& value, const std::string& option_name) {
  std::size_t consumed = 0;
  long long parsed = 0;
  try {
    parsed = std::stoll(value, &consumed, 10);
  } catch (const std::exception&) {
    throw std::runtime_error(option_name + " requires a positive integer");
  }
  if (consumed != value.size() || parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
    throw std::runtime_error(option_name + " requires a positive integer");
  }
  return static_cast<int>(parsed);
}

std::string indent_block(const std::string& block, const std::string& indent) {
  std::istringstream lines(block);
  std::ostringstream out;
  std::string line;
  while (std::getline(lines, line)) {
    out << indent << line << "\n";
  }
  return out.str();
}

const sigil::FunctionDecl* find_function(const sigil::Module& module,
                                         const std::string& function_name) {
  for (const auto& fn : module.functions) {
    if (fn.name == function_name) {
      return &fn;
    }
  }
  return nullptr;
}

sigil::GccJitScalarValue parse_run_argument(const std::string& value, const sigil::Type& type,
                                            std::size_t index) {
  if (type.kind == sigil::TypeKind::I64) {
    std::size_t consumed = 0;
    long long parsed = 0;
    try {
      parsed = std::stoll(value, &consumed, 10);
    } catch (const std::exception&) {
      throw std::runtime_error("argument " + std::to_string(index + 1) + " must be an i64");
    }
    if (consumed != value.size()) {
      throw std::runtime_error("argument " + std::to_string(index + 1) + " must be an i64");
    }
    return sigil::gccjit_i64(parsed);
  }

  if (type.kind == sigil::TypeKind::Bool) {
    if (value == "true" || value == "1") {
      return sigil::gccjit_bool(true);
    }
    if (value == "false" || value == "0") {
      return sigil::gccjit_bool(false);
    }
    throw std::runtime_error("argument " + std::to_string(index + 1) +
                             " must be a bool: true, false, 1, or 0");
  }

  throw std::runtime_error("argument " + std::to_string(index + 1) + " has unsupported type '" +
                           type.display() + "'");
}

int check_command(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_help();
    return 1;
  }

  std::string path;
  bool dump_smt = false;
  bool strict = false;
  sigil::ProofOptions proof_options;
  for (std::size_t index = 0; index < args.size(); ++index) {
    const auto& arg = args[index];
    if (arg == "--dump-smt") {
      dump_smt = true;
    } else if (arg == "--save-smt") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--save-smt requires an output directory");
      }
      proof_options.smt_output_dir = args[++index];
    } else if (arg == "--show-model") {
      proof_options.include_models = true;
    } else if (arg == "--solver-timeout-ms") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--solver-timeout-ms requires a positive integer");
      }
      proof_options.solver_timeout_ms = parse_positive_int(args[++index], "--solver-timeout-ms");
    } else if (arg == "--strict") {
      strict = true;
    } else if (arg == "--no-z3") {
      proof_options.use_z3 = false;
    } else if (path.empty()) {
      path = arg;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);
  const auto obligations = sigil::build_obligations(module);
  const auto results = sigil::verify_obligations(obligations, proof_options);

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
    std::cout << "[" << sigil::status_name(result.status) << "] " << result.obligation_name << " - "
              << result.details << "\n";
    std::cout << "  at: " << result.range.display() << "\n";
    if (!result.smt_path.empty()) {
      std::cout << "  smt: " << result.smt_path << "\n";
    }
    if (!result.model.empty()) {
      std::cout << "  model:\n" << indent_block(result.model, "    ");
    }
    if (dump_smt) {
      std::cout << result.smt_lib << "\n";
    }
    has_failure = has_failure || result.status == sigil::VerificationStatus::Refuted ||
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

int compile_command(const std::vector<std::string>& args) {
  if (args.size() != 1) {
    print_help();
    return 1;
  }

  const auto& path = args[0];
  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);
  const auto result = sigil::compile_module_with_gccjit(module);

  std::cout << "module " << module.name << "\n";
  std::cout << "  backend: libgccjit\n";
  std::cout << "  status: " << (result.compiled ? "compiled" : "not compiled") << "\n";
  std::cout << "  detail: " << result.detail << "\n";
  for (const auto& fn : result.functions) {
    std::cout << "  " << (fn.lowered ? "lowered" : "skipped") << ": " << fn.name;
    if (!fn.detail.empty()) {
      std::cout << " - " << fn.detail;
    }
    std::cout << "\n";
  }

  if (!result.available) {
    return 3;
  }
  return result.compiled ? 0 : 2;
}

int run_command(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    print_help();
    return 1;
  }

  const auto& path = args[0];
  const auto& function_name = args[1];
  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);

  const auto* fn = find_function(module, function_name);
  if (!fn) {
    throw std::runtime_error("unknown function: " + function_name);
  }
  if (args.size() - 2 != fn->params.size()) {
    throw std::runtime_error("function '" + function_name + "' expects " +
                             std::to_string(fn->params.size()) + " argument(s), got " +
                             std::to_string(args.size() - 2));
  }

  std::vector<sigil::GccJitScalarValue> values;
  values.reserve(fn->params.size());
  for (std::size_t index = 0; index < fn->params.size(); ++index) {
    values.push_back(parse_run_argument(args[index + 2], fn->params[index].type, index));
  }

  const auto result = sigil::invoke_function_with_gccjit(module, function_name, values);
  std::cout << "module " << module.name << "\n";
  std::cout << "  run: " << function_name << "\n";
  std::cout << "  status: " << (result.invoked ? "invoked" : "not invoked") << "\n";
  std::cout << "  detail: " << result.detail << "\n";
  if (result.invoked) {
    std::cout << "  result: " << sigil::display_gccjit_value(result.value) << "\n";
  }

  if (!result.available) {
    return 3;
  }
  return result.invoked ? 0 : 2;
}

} // namespace

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
    if (command == "compile") {
      return compile_command(args);
    }
    if (command == "run") {
      return run_command(args);
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

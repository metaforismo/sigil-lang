#include "sigil/agent_refinement.hpp"
#include "sigil/gccjit_backend.hpp"
#include "sigil/parser.hpp"
#include "sigil/proof.hpp"
#include "sigil/typecheck.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
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
            << "  sigil check <file.sigil> [--dump-smt] [--save-smt <dir>]\n"
            << "                          [--save-proof-hints <dir>]\n"
            << "                          [--save-agent-requests <dir>] [--show-model]\n"
            << "                          [--solver-timeout-ms <ms>] [--strict] [--no-z3]\n"
            << "  sigil agent-check <candidate.sigil> [--dump-smt] [--save-smt <dir>]\n"
            << "                                      [--show-model] [--solver-timeout-ms <ms>]\n"
            << "                                      [--strict] [--no-z3]\n"
            << "  sigil agent-refine <file.sigil> --agent-command <executable>\n"
            << "                     --save-trace <dir> [--max-attempts <n>]\n"
            << "                     [--agent-timeout-ms <ms>] [--solver-timeout-ms <ms>]\n"
            << "                     [--no-z3]\n"
            << "  sigil compile <file.sigil> [--dump-native-ir] [--save-native-ir <dir>]\n"
            << "                            [--dump-binary-facts] [--save-binary-facts <dir>]\n"
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

bool has_range(const sigil::SourceRange& range) {
  return range.start.line != 0;
}

const char* yes_no(bool value) {
  return value ? "yes" : "no";
}

const char* availability(bool value) {
  return value ? "available" : "unavailable";
}

void print_range_if_available(const sigil::SourceRange& range) {
  if (has_range(range)) {
    std::cout << "    at: " << range.display() << "\n";
  }
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

std::string available_function_names(const sigil::Module& module) {
  if (module.functions.empty()) {
    return "(none)";
  }

  std::ostringstream out;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << module.functions[index].name;
  }
  return out.str();
}

std::string function_signature(const sigil::FunctionDecl& fn) {
  std::ostringstream out;
  out << fn.name << "(";
  for (std::size_t index = 0; index < fn.params.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << fn.params[index].name << ": " << fn.params[index].type.display();
  }
  out << ") -> " << fn.return_type.display();
  return out.str();
}

sigil::GccJitScalarValue parse_run_argument(const std::string& value, const sigil::ParamDecl& param,
                                            std::size_t index) {
  const auto& type = param.type;
  const auto parameter_context = " for parameter '" + param.name + "'";

  if (type.kind == sigil::TypeKind::I64) {
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || parsed_end != end) {
      throw std::runtime_error("argument " + std::to_string(index + 1) + " must be an i64" +
                               parameter_context);
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
    throw std::runtime_error("argument " + std::to_string(index + 1) + " must be a bool" +
                             parameter_context + ": true, false, 1, or 0");
  }

  throw std::runtime_error("argument " + std::to_string(index + 1) + " has unsupported type '" +
                           type.display() + "'" + parameter_context);
}

std::vector<std::string>
write_native_artifacts(const std::vector<sigil::GccJitNativeArtifact>& artifacts,
                       const std::string& output_dir) {
  std::filesystem::create_directories(output_dir);
  std::vector<std::string> paths;
  paths.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    const auto path = std::filesystem::path(output_dir) / artifact.file_name;
    std::ofstream file(path);
    if (!file) {
      throw std::runtime_error("could not write native IR artifact: " + path.string());
    }
    file << artifact.text;
    paths.push_back(path.string());
  }
  return paths;
}

std::vector<std::string>
write_binary_proof_artifacts(const std::vector<sigil::GccJitBinaryProofArtifact>& artifacts,
                             const std::string& output_dir) {
  std::filesystem::create_directories(output_dir);
  std::vector<std::string> paths;
  paths.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    const auto path = std::filesystem::path(output_dir) / artifact.file_name;
    std::ofstream file(path);
    if (!file) {
      throw std::runtime_error("could not write binary proof artifact: " + path.string());
    }
    file << artifact.text;
    paths.push_back(path.string());
  }
  return paths;
}

std::vector<std::string>
write_proof_hint_artifacts(const std::vector<sigil::ProofHintArtifact>& artifacts,
                           const std::string& output_dir) {
  std::filesystem::create_directories(output_dir);
  std::vector<std::string> paths;
  paths.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    const auto path = std::filesystem::path(output_dir) / artifact.file_name;
    std::ofstream file(path);
    if (!file) {
      throw std::runtime_error("could not write proof hint artifact: " + path.string());
    }
    file << artifact.text;
    paths.push_back(path.string());
  }
  return paths;
}

struct WrittenAgentArtifact {
  std::string label;
  std::string path;
};

std::vector<WrittenAgentArtifact>
write_agent_handoff_artifacts(const std::vector<sigil::AgentHandoffArtifact>& artifacts,
                              const std::string& output_dir) {
  std::filesystem::create_directories(output_dir);
  std::vector<WrittenAgentArtifact> paths;
  paths.reserve(artifacts.size());
  for (const auto& artifact : artifacts) {
    const auto path = std::filesystem::path(output_dir) / artifact.file_name;
    std::ofstream file(path);
    if (!file) {
      throw std::runtime_error("could not write agent handoff artifact: " + path.string());
    }
    file << artifact.text;
    paths.push_back(WrittenAgentArtifact{artifact.label, path.string()});
  }
  return paths;
}

std::size_t count_struct_invariants(const sigil::Module& module) {
  std::size_t invariant_count = 0;
  for (const auto& decl : module.structs) {
    invariant_count += decl.invariants.size();
  }
  return invariant_count;
}

std::size_t count_struct_declarations(const sigil::Module& module) {
  std::size_t count = 0;
  for (const auto& decl : module.structs) {
    if (!decl.is_container) {
      ++count;
    }
  }
  return count;
}

std::size_t count_container_declarations(const sigil::Module& module) {
  std::size_t count = 0;
  for (const auto& decl : module.structs) {
    if (decl.is_container) {
      ++count;
    }
  }
  return count;
}

struct VerificationSummary {
  bool has_failure = false;
  bool has_unknown = false;
};

VerificationSummary
summarize_verification_results(const std::vector<sigil::VerificationResult>& results) {
  VerificationSummary summary;
  for (const auto& result : results) {
    summary.has_failure = summary.has_failure ||
                          result.status == sigil::VerificationStatus::Refuted ||
                          result.status == sigil::VerificationStatus::Error;
    summary.has_unknown =
        summary.has_unknown || result.status == sigil::VerificationStatus::Unknown;
  }
  return summary;
}

void print_verification_results(const std::vector<sigil::VerificationResult>& results,
                                bool dump_smt) {
  for (const auto& result : results) {
    std::cout << "[" << sigil::status_name(result.status) << "] " << result.obligation_name << " - "
              << result.details << "\n";
    std::cout << "  at: " << result.range.display() << "\n";
    if (!result.smt_path.empty()) {
      std::cout << "  smt: " << result.smt_path << "\n";
    }
    if (!result.counterexample.empty()) {
      std::cout << "  counterexample:\n" << indent_block(result.counterexample, "    ");
    }
    if (!result.model.empty()) {
      std::cout << "  model:\n" << indent_block(result.model, "    ");
    }
    if (dump_smt) {
      std::cout << result.smt_lib << "\n";
    }
  }
}

bool proofs_rejected(const VerificationSummary& summary, bool strict) {
  return summary.has_failure || (strict && summary.has_unknown);
}

int check_command(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_help();
    return 1;
  }

  std::string path;
  bool dump_smt = false;
  bool strict = false;
  std::string proof_hint_output_dir;
  std::string agent_handoff_output_dir;
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
    } else if (arg == "--save-proof-hints") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--save-proof-hints requires an output directory");
      }
      proof_hint_output_dir = args[++index];
    } else if (arg == "--save-agent-requests") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--save-agent-requests requires an output directory");
      }
      agent_handoff_output_dir = args[++index];
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
  if (path.empty()) {
    throw std::runtime_error("check requires <file.sigil>");
  }

  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);
  const auto obligations = sigil::build_obligations(module);
  const auto results = sigil::verify_obligations(obligations, proof_options);
  std::vector<std::string> proof_hint_paths;
  if (!proof_hint_output_dir.empty()) {
    proof_hint_paths = write_proof_hint_artifacts(
        sigil::build_proof_hint_artifacts(obligations, results), proof_hint_output_dir);
  }
  std::vector<WrittenAgentArtifact> agent_handoff_paths;
  if (!agent_handoff_output_dir.empty()) {
    agent_handoff_paths = write_agent_handoff_artifacts(
        sigil::build_agent_handoff_artifacts(obligations, results), agent_handoff_output_dir);
  }

  std::cout << "module " << module.name << "\n";
  std::cout << "  structs: " << count_struct_declarations(module) << "\n";
  std::cout << "  containers: " << count_container_declarations(module) << "\n";
  std::cout << "  theorems: " << module.theorems.size() << "\n";
  std::cout << "  functions: " << module.functions.size() << "\n";
  std::cout << "  registered aggregate invariants: " << count_struct_invariants(module) << "\n";
  std::cout << "  proof obligations: " << results.size() << "\n";

  const auto summary = summarize_verification_results(results);
  print_verification_results(results, dump_smt);
  for (const auto& proof_hint_path : proof_hint_paths) {
    std::cout << "  proof-hint: " << proof_hint_path << "\n";
  }
  for (const auto& artifact : agent_handoff_paths) {
    std::cout << "  " << artifact.label << ": " << artifact.path << "\n";
  }

  if (proofs_rejected(summary, strict)) {
    return 2;
  }
  return 0;
}

int agent_check_command(const std::vector<std::string>& args) {
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
  if (path.empty()) {
    throw std::runtime_error("agent-check requires <candidate.sigil>");
  }

  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);
  const auto obligations = sigil::build_obligations(module);
  const auto results = sigil::verify_obligations(obligations, proof_options);
  const auto summary = summarize_verification_results(results);
  const bool surface_rejected = !module.functions.empty();
  const bool rejected = surface_rejected || proofs_rejected(summary, strict);

  std::cout << "agent-candidate " << module.name << "\n";
  std::cout << "  structs: " << count_struct_declarations(module) << "\n";
  std::cout << "  containers: " << count_container_declarations(module) << "\n";
  std::cout << "  theorems: " << module.theorems.size() << "\n";
  std::cout << "  functions: " << module.functions.size() << "\n";
  std::cout << "  registered aggregate invariants: " << count_struct_invariants(module) << "\n";
  std::cout << "  proof obligations: " << results.size() << "\n";
  std::cout << "  status: " << (rejected ? "rejected" : "accepted") << "\n";
  if (surface_rejected) {
    std::cout << "  reason: runtime functions are outside the proof-only candidate surface\n";
  }
  print_verification_results(results, dump_smt);

  return rejected ? 2 : 0;
}

int agent_refine_command(const std::vector<std::string>& args) {
  std::string path;
  sigil::AgentRefinementOptions options;
  for (std::size_t index = 0; index < args.size(); ++index) {
    const auto& arg = args[index];
    if (arg == "--agent-command") {
      if (++index >= args.size()) {
        throw std::runtime_error("--agent-command requires an executable path");
      }
      options.agent_command = args[index];
    } else if (arg == "--save-trace") {
      if (++index >= args.size()) {
        throw std::runtime_error("--save-trace requires an output directory");
      }
      options.trace_output_dir = args[index];
    } else if (arg == "--max-attempts") {
      if (++index >= args.size()) {
        throw std::runtime_error("--max-attempts requires a positive integer");
      }
      options.max_attempts = parse_positive_int(args[index], "--max-attempts");
    } else if (arg == "--agent-timeout-ms") {
      if (++index >= args.size()) {
        throw std::runtime_error("--agent-timeout-ms requires a positive integer");
      }
      options.agent_timeout_ms = parse_positive_int(args[index], "--agent-timeout-ms");
    } else if (arg == "--solver-timeout-ms") {
      if (++index >= args.size()) {
        throw std::runtime_error("--solver-timeout-ms requires a positive integer");
      }
      options.proof.solver_timeout_ms = parse_positive_int(args[index], "--solver-timeout-ms");
    } else if (arg == "--no-z3") {
      options.proof.use_z3 = false;
    } else if (path.empty()) {
      path = arg;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (path.empty() || options.agent_command.empty() || options.trace_output_dir.empty()) {
    throw std::runtime_error(
        "agent-refine requires <file.sigil>, --agent-command, and --save-trace");
  }

  const auto result = sigil::run_agent_refinement(path, options);
  std::cout << "agent-refinement " << result.module_name << "\n";
  std::cout << "  status: " << (result.accepted ? "accepted" : "exhausted") << "\n";
  std::cout << "  attempts-used: " << result.attempts_used << "\n";
  std::cout << "  trace: " << result.trace_path << "\n";
  return result.accepted ? 0 : 2;
}

int backend_command(const std::vector<std::string>& args) {
  if (!args.empty()) {
    throw std::runtime_error("unknown argument: " + args.front());
  }

  const auto capabilities = sigil::gccjit_capabilities();
  std::cout << availability(capabilities.context_available) << ": " << capabilities.detail << "\n";
  std::cout << "  compiled-with-libgccjit: " << yes_no(capabilities.compiled_with_libgccjit)
            << "\n";
  std::cout << "  jit-context: " << availability(capabilities.context_available) << "\n";
  std::cout << "  native-lowering: " << availability(capabilities.native_lowering) << "\n";
  std::cout << "  abi-invocation: " << availability(capabilities.abi_invocation) << "\n";
  std::cout << "  debug-info: " << (capabilities.debug_info ? "enabled" : "disabled") << "\n";
  std::cout << "  native-ir-artifacts: " << availability(capabilities.native_ir_artifacts) << "\n";
  std::cout << "  binary-proof-artifacts: " << availability(capabilities.binary_proof_artifacts)
            << "\n";
  return capabilities.context_available ? 0 : 3;
}

int compile_command(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_help();
    return 1;
  }

  std::string path;
  bool dump_native_ir = false;
  bool dump_binary_facts = false;
  std::string native_ir_output_dir;
  std::string binary_facts_output_dir;
  for (std::size_t index = 0; index < args.size(); ++index) {
    const auto& arg = args[index];
    if (arg == "--dump-native-ir") {
      dump_native_ir = true;
    } else if (arg == "--save-native-ir") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--save-native-ir requires an output directory");
      }
      native_ir_output_dir = args[++index];
    } else if (arg == "--dump-binary-facts") {
      dump_binary_facts = true;
    } else if (arg == "--save-binary-facts") {
      if (index + 1 >= args.size()) {
        throw std::runtime_error("--save-binary-facts requires an output directory");
      }
      binary_facts_output_dir = args[++index];
    } else if (path.empty()) {
      path = arg;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (path.empty()) {
    print_help();
    return 1;
  }

  const auto source = read_file(path);
  const auto module = sigil::parse_source(source, path);
  sigil::validate_module(module);
  const auto result = sigil::compile_module_with_gccjit(module);
  const auto artifacts = sigil::build_native_ir_artifacts(module, result);
  const auto binary_artifacts = sigil::build_binary_proof_artifacts(module, result);
  std::vector<std::string> artifact_paths;
  if (!native_ir_output_dir.empty()) {
    artifact_paths = write_native_artifacts(artifacts, native_ir_output_dir);
  }
  std::vector<std::string> binary_artifact_paths;
  if (!binary_facts_output_dir.empty()) {
    binary_artifact_paths = write_binary_proof_artifacts(binary_artifacts, binary_facts_output_dir);
  }

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
    print_range_if_available(fn.range);
  }
  for (const auto& artifact_path : artifact_paths) {
    std::cout << "  native-ir: " << artifact_path << "\n";
  }
  for (const auto& artifact_path : binary_artifact_paths) {
    std::cout << "  binary-facts: " << artifact_path << "\n";
  }
  if (dump_native_ir) {
    for (const auto& artifact : artifacts) {
      std::cout << "--- " << artifact.file_name << "\n" << artifact.text;
    }
  }
  if (dump_binary_facts) {
    for (const auto& artifact : binary_artifacts) {
      std::cout << "--- " << artifact.file_name << "\n" << artifact.text;
    }
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
    throw std::runtime_error("unknown function: " + function_name +
                             "; available functions: " + available_function_names(module));
  }
  if (args.size() - 2 != fn->params.size()) {
    throw std::runtime_error("function '" + function_name + "' expects " +
                             std::to_string(fn->params.size()) + " argument(s), got " +
                             std::to_string(args.size() - 2) +
                             "; signature: " + function_signature(*fn));
  }

  std::vector<sigil::GccJitScalarValue> values;
  values.reserve(fn->params.size());
  for (std::size_t index = 0; index < fn->params.size(); ++index) {
    values.push_back(parse_run_argument(args[index + 2], fn->params[index], index));
  }

  const auto result = sigil::invoke_function_with_gccjit(module, function_name, values);
  std::cout << "module " << module.name << "\n";
  std::cout << "  run: " << function_name << "\n";
  std::cout << "  status: " << (result.invoked ? "invoked" : "not invoked") << "\n";
  std::cout << "  detail: " << result.detail << "\n";
  print_range_if_available(result.range);
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
    if (command == "agent-check") {
      return agent_check_command(args);
    }
    if (command == "agent-refine") {
      return agent_refine_command(args);
    }
    if (command == "compile") {
      return compile_command(args);
    }
    if (command == "run") {
      return run_command(args);
    }
    if (command == "backend") {
      return backend_command(args);
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

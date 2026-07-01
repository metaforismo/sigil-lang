#include "sigil/gccjit_backend.hpp"

#if SIGIL_HAVE_GCCJIT
#include <libgccjit.h>
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sigil {

GccJitScalarValue gccjit_i64(std::int64_t value) {
  return GccJitScalarValue{TypeKind::I64, value, value != 0};
}

GccJitScalarValue gccjit_bool(bool value) {
  return GccJitScalarValue{TypeKind::Bool, value ? 1 : 0, value};
}

std::string display_gccjit_value(const GccJitScalarValue& value) {
  if (value.kind == TypeKind::Bool) {
    return value.boolean ? "true" : "false";
  }
  return std::to_string(value.integer);
}

namespace {

[[maybe_unused]] const char* unavailable_detail() {
  return "this binary was built without libgccjit; install libgccjit and reconfigure CMake";
}

#if SIGIL_HAVE_GCCJIT

struct ContextDeleter {
  void operator()(gcc_jit_context* context) const {
    if (context) {
      gcc_jit_context_release(context);
    }
  }
};

struct ResultDeleter {
  void operator()(gcc_jit_result* result) const {
    if (result) {
      gcc_jit_result_release(result);
    }
  }
};

class LoweringError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

bool is_native_scalar(const Type& type) {
  return type.kind == TypeKind::I64 || type.kind == TypeKind::Bool;
}

std::string unsupported_type_detail(const std::string& owner, const Type& type) {
  return owner + " has unsupported native type '" + type.display() + "'";
}

bool expression_is_lowerable(const Expr& expr, std::string& detail);

bool binary_is_lowerable(BinaryOp op, std::string& detail) {
  if (op == BinaryOp::Divide || op == BinaryOp::Modulo) {
    detail = "division and modulo are not native-lowered until Sigil fixes their exact semantics";
    return false;
  }
  return true;
}

bool expression_is_lowerable(const Expr& expr, std::string& detail) {
  if (!expr) {
    detail = "missing expression";
    return false;
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
  case ExprNode::Kind::Boolean:
  case ExprNode::Kind::Identifier:
    return true;
  case ExprNode::Kind::Unary:
    return expression_is_lowerable(expr->lhs, detail);
  case ExprNode::Kind::Binary:
    return binary_is_lowerable(expr->binary_op, detail) &&
           expression_is_lowerable(expr->lhs, detail) && expression_is_lowerable(expr->rhs, detail);
  case ExprNode::Kind::If:
    return expression_is_lowerable(expr->condition, detail) &&
           expression_is_lowerable(expr->lhs, detail) && expression_is_lowerable(expr->rhs, detail);
  }

  detail = "unknown expression kind";
  return false;
}

bool statements_are_lowerable(const std::vector<Statement>& statements, std::string& detail);

bool statement_is_lowerable(const Statement& statement, std::string& detail) {
  switch (statement.kind) {
  case StatementKind::Let:
    if (!is_native_scalar(statement.type)) {
      detail = unsupported_type_detail("local '" + statement.name + "'", statement.type);
      return false;
    }
    return expression_is_lowerable(statement.expr, detail);
  case StatementKind::Assign:
  case StatementKind::Assume:
  case StatementKind::Assert:
  case StatementKind::Return:
    return expression_is_lowerable(statement.expr, detail);
  case StatementKind::If:
    return expression_is_lowerable(statement.expr, detail) &&
           statements_are_lowerable(statement.then_branch, detail) &&
           statements_are_lowerable(statement.else_branch, detail);
  }

  detail = "unknown statement kind";
  return false;
}

bool statements_are_lowerable(const std::vector<Statement>& statements, std::string& detail) {
  for (const auto& statement : statements) {
    if (!statement_is_lowerable(statement, detail)) {
      return false;
    }
  }
  return true;
}

bool block_always_returns(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Return) {
      return true;
    }
    if (statement.kind == StatementKind::If && block_always_returns(statement.then_branch) &&
        block_always_returns(statement.else_branch)) {
      return true;
    }
  }
  return false;
}

bool function_is_lowerable(const FunctionDecl& fn, std::string& detail) {
  if (!is_native_scalar(fn.return_type)) {
    detail = unsupported_type_detail("function '" + fn.name + "' return", fn.return_type);
    return false;
  }
  for (const auto& param : fn.params) {
    if (!is_native_scalar(param.type)) {
      detail = unsupported_type_detail("parameter '" + param.name + "'", param.type);
      return false;
    }
  }
  if (!statements_are_lowerable(fn.body, detail)) {
    return false;
  }
  if (!block_always_returns(fn.body)) {
    detail = "function does not return on every lowered path";
    return false;
  }
  return true;
}

std::string join_names(const std::vector<std::string>& names) {
  std::ostringstream out;
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (index > 0) {
      out << ", ";
    }
    out << names[index];
  }
  return out.str();
}

std::string sanitize_native_name(std::string name) {
  for (auto& ch : name) {
    const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '_';
    if (!valid) {
      ch = '_';
    }
  }
  return name;
}

struct NativeVariable {
  Type type;
  gcc_jit_lvalue* lvalue = nullptr;
};

using NativeVariables = std::unordered_map<std::string, NativeVariable>;

struct BlockState {
  gcc_jit_block* block = nullptr;
  bool terminated = false;
};

class FunctionLowerer {
public:
  FunctionLowerer(gcc_jit_context* context, gcc_jit_type* i64_type, gcc_jit_type* bool_type,
                  const FunctionDecl& fn)
      : context_(context), i64_type_(i64_type), bool_type_(bool_type), fn_(fn) {}

  void lower() {
    std::vector<gcc_jit_param*> params;
    params.reserve(fn_.params.size());
    for (const auto& param : fn_.params) {
      params.push_back(gcc_jit_context_new_param(context_, location(param.location),
                                                 type_for(param.type), param.name.c_str()));
    }

    function_ = gcc_jit_context_new_function(
        context_, location(fn_.location), GCC_JIT_FUNCTION_EXPORTED, type_for(fn_.return_type),
        fn_.name.c_str(), static_cast<int>(params.size()), params.data(), 0);

    NativeVariables variables;
    for (std::size_t index = 0; index < fn_.params.size(); ++index) {
      variables[fn_.params[index].name] =
          NativeVariable{fn_.params[index].type, gcc_jit_param_as_lvalue(params[index])};
    }

    BlockState state{gcc_jit_function_new_block(function_, "entry"), false};
    lower_statements(fn_.body, variables, state);
    if (!state.terminated) {
      throw LoweringError("function '" + fn_.name + "' did not terminate during lowering");
    }
  }

private:
  gcc_jit_location* location(const SourceLocation& source) const {
    const char* file = source.file.empty() ? nullptr : source.file.c_str();
    return gcc_jit_context_new_location(context_, file, static_cast<int>(source.line),
                                        static_cast<int>(source.column));
  }

  gcc_jit_location* location(const SourceRange& range) const {
    return location(range.start);
  }

  gcc_jit_type* type_for(const Type& type) const {
    if (type.kind == TypeKind::I64) {
      return i64_type_;
    }
    if (type.kind == TypeKind::Bool) {
      return bool_type_;
    }
    throw LoweringError("unsupported native type '" + type.display() + "'");
  }

  Type expr_type(const Expr& expr, const NativeVariables& variables) const {
    if (!expr) {
      throw LoweringError("missing expression");
    }
    switch (expr->kind) {
    case ExprNode::Kind::Integer:
      return Type{TypeKind::I64, "i64"};
    case ExprNode::Kind::Boolean:
      return Type{TypeKind::Bool, "bool"};
    case ExprNode::Kind::Identifier: {
      const auto found = variables.find(expr->name);
      if (found == variables.end()) {
        throw LoweringError("unknown native variable '" + expr->name + "'");
      }
      return found->second.type;
    }
    case ExprNode::Kind::Unary:
      return expr->unary_op == UnaryOp::Not ? Type{TypeKind::Bool, "bool"}
                                            : Type{TypeKind::I64, "i64"};
    case ExprNode::Kind::Binary:
      switch (expr->binary_op) {
      case BinaryOp::Add:
      case BinaryOp::Subtract:
      case BinaryOp::Multiply:
      case BinaryOp::Divide:
      case BinaryOp::Modulo:
        return Type{TypeKind::I64, "i64"};
      case BinaryOp::Or:
      case BinaryOp::And:
      case BinaryOp::Equal:
      case BinaryOp::NotEqual:
      case BinaryOp::Less:
      case BinaryOp::LessEqual:
      case BinaryOp::Greater:
      case BinaryOp::GreaterEqual:
        return Type{TypeKind::Bool, "bool"};
      }
      break;
    case ExprNode::Kind::If:
      return expr_type(expr->lhs, variables);
    }
    throw LoweringError("unknown expression kind");
  }

  std::string fresh_local_name(const std::string& source_name) {
    return "_sigil_" + sanitize_native_name(fn_.name) + "_" + sanitize_native_name(source_name) +
           "_" + std::to_string(++local_counter_);
  }

  gcc_jit_rvalue* lower_expr(const Expr& expr, NativeVariables& variables, BlockState& state) {
    if (state.terminated) {
      throw LoweringError("cannot lower expression into a terminated block");
    }
    switch (expr->kind) {
    case ExprNode::Kind::Integer:
      return gcc_jit_context_new_rvalue_from_long(context_, i64_type_,
                                                  static_cast<long>(expr->integer_value));
    case ExprNode::Kind::Boolean:
      return expr->boolean_value ? gcc_jit_context_one(context_, bool_type_)
                                 : gcc_jit_context_zero(context_, bool_type_);
    case ExprNode::Kind::Identifier: {
      const auto found = variables.find(expr->name);
      if (found == variables.end()) {
        throw LoweringError("unknown native variable '" + expr->name + "'");
      }
      return gcc_jit_lvalue_as_rvalue(found->second.lvalue);
    }
    case ExprNode::Kind::Unary:
      return lower_unary_expr(expr, variables, state);
    case ExprNode::Kind::Binary:
      return lower_binary_expr(expr, variables, state);
    case ExprNode::Kind::If:
      return lower_if_expr(expr, variables, state);
    }
    throw LoweringError("unknown expression kind");
  }

  gcc_jit_rvalue* lower_unary_expr(const Expr& expr, NativeVariables& variables,
                                   BlockState& state) {
    const auto operand = lower_expr(expr->lhs, variables, state);
    const auto op =
        expr->unary_op == UnaryOp::Not ? GCC_JIT_UNARY_OP_LOGICAL_NEGATE : GCC_JIT_UNARY_OP_MINUS;
    const auto result_type = expr->unary_op == UnaryOp::Not ? bool_type_ : i64_type_;
    return gcc_jit_context_new_unary_op(context_, location(expr->range), op, result_type, operand);
  }

  gcc_jit_rvalue* lower_binary_expr(const Expr& expr, NativeVariables& variables,
                                    BlockState& state) {
    const auto lhs = lower_expr(expr->lhs, variables, state);
    const auto rhs = lower_expr(expr->rhs, variables, state);
    switch (expr->binary_op) {
    case BinaryOp::Add:
      return gcc_jit_context_new_binary_op(context_, location(expr->range), GCC_JIT_BINARY_OP_PLUS,
                                           i64_type_, lhs, rhs);
    case BinaryOp::Subtract:
      return gcc_jit_context_new_binary_op(context_, location(expr->range), GCC_JIT_BINARY_OP_MINUS,
                                           i64_type_, lhs, rhs);
    case BinaryOp::Multiply:
      return gcc_jit_context_new_binary_op(context_, location(expr->range), GCC_JIT_BINARY_OP_MULT,
                                           i64_type_, lhs, rhs);
    case BinaryOp::Divide:
    case BinaryOp::Modulo:
      throw LoweringError("division and modulo are not native-lowered yet");
    case BinaryOp::And:
      return gcc_jit_context_new_binary_op(context_, location(expr->range),
                                           GCC_JIT_BINARY_OP_LOGICAL_AND, bool_type_, lhs, rhs);
    case BinaryOp::Or:
      return gcc_jit_context_new_binary_op(context_, location(expr->range),
                                           GCC_JIT_BINARY_OP_LOGICAL_OR, bool_type_, lhs, rhs);
    case BinaryOp::Equal:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_EQ,
                                            lhs, rhs);
    case BinaryOp::NotEqual:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_NE,
                                            lhs, rhs);
    case BinaryOp::Less:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_LT,
                                            lhs, rhs);
    case BinaryOp::LessEqual:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_LE,
                                            lhs, rhs);
    case BinaryOp::Greater:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_GT,
                                            lhs, rhs);
    case BinaryOp::GreaterEqual:
      return gcc_jit_context_new_comparison(context_, location(expr->range), GCC_JIT_COMPARISON_GE,
                                            lhs, rhs);
    }
    throw LoweringError("unknown binary operator");
  }

  gcc_jit_rvalue* lower_if_expr(const Expr& expr, NativeVariables& variables, BlockState& state) {
    const auto result_type = expr_type(expr, variables);
    const auto temp_name = fresh_local_name("if");
    const auto temp = gcc_jit_function_new_local(function_, location(expr->range),
                                                 type_for(result_type), temp_name.c_str());
    const auto condition = lower_expr(expr->condition, variables, state);

    auto* then_block = gcc_jit_function_new_block(function_, "if_expr_then");
    auto* else_block = gcc_jit_function_new_block(function_, "if_expr_else");
    auto* merge_block = gcc_jit_function_new_block(function_, "if_expr_merge");
    gcc_jit_block_end_with_conditional(state.block, location(expr->condition->range), condition,
                                       then_block, else_block);

    BlockState then_state{then_block, false};
    auto then_variables = variables;
    auto* then_value = lower_expr(expr->lhs, then_variables, then_state);
    gcc_jit_block_add_assignment(then_state.block, location(expr->lhs->range), temp, then_value);
    gcc_jit_block_end_with_jump(then_state.block, location(expr->lhs->range), merge_block);

    BlockState else_state{else_block, false};
    auto else_variables = variables;
    auto* else_value = lower_expr(expr->rhs, else_variables, else_state);
    gcc_jit_block_add_assignment(else_state.block, location(expr->rhs->range), temp, else_value);
    gcc_jit_block_end_with_jump(else_state.block, location(expr->rhs->range), merge_block);

    state.block = merge_block;
    state.terminated = false;
    return gcc_jit_lvalue_as_rvalue(temp);
  }

  void lower_statements(const std::vector<Statement>& statements, NativeVariables& variables,
                        BlockState& state) {
    for (const auto& statement : statements) {
      if (state.terminated) {
        return;
      }
      lower_statement(statement, variables, state);
    }
  }

  void lower_statement(const Statement& statement, NativeVariables& variables, BlockState& state) {
    switch (statement.kind) {
    case StatementKind::Let: {
      const auto local_name = fresh_local_name(statement.name);
      auto* local = gcc_jit_function_new_local(function_, location(statement.location),
                                               type_for(statement.type), local_name.c_str());
      auto* value = lower_expr(statement.expr, variables, state);
      gcc_jit_block_add_assignment(state.block, location(statement.range), local, value);
      variables[statement.name] = NativeVariable{statement.type, local};
      return;
    }
    case StatementKind::Assign: {
      const auto found = variables.find(statement.name);
      if (found == variables.end()) {
        throw LoweringError("unknown native assignment target '" + statement.name + "'");
      }
      auto* value = lower_expr(statement.expr, variables, state);
      gcc_jit_block_add_assignment(state.block, location(statement.range), found->second.lvalue,
                                   value);
      return;
    }
    case StatementKind::If:
      lower_if_statement(statement, variables, state);
      return;
    case StatementKind::Assume:
    case StatementKind::Assert:
      return;
    case StatementKind::Return: {
      auto* value = lower_expr(statement.expr, variables, state);
      gcc_jit_block_end_with_return(state.block, location(statement.range), value);
      state.terminated = true;
      return;
    }
    }
    throw LoweringError("unknown statement kind");
  }

  void lower_if_statement(const Statement& statement, NativeVariables& variables,
                          BlockState& state) {
    auto* condition = lower_expr(statement.expr, variables, state);
    auto* then_block = gcc_jit_function_new_block(function_, "if_then");
    auto* else_block = gcc_jit_function_new_block(function_, "if_else");
    const bool needs_merge = !(block_always_returns(statement.then_branch) &&
                               block_always_returns(statement.else_branch));
    auto* merge_block = needs_merge ? gcc_jit_function_new_block(function_, "if_merge") : nullptr;
    gcc_jit_block_end_with_conditional(state.block, location(statement.expr->range), condition,
                                       then_block, else_block);

    BlockState then_state{then_block, false};
    auto then_variables = variables;
    lower_statements(statement.then_branch, then_variables, then_state);
    if (!then_state.terminated) {
      gcc_jit_block_end_with_jump(then_state.block, location(statement.range), merge_block);
    }

    BlockState else_state{else_block, false};
    auto else_variables = variables;
    lower_statements(statement.else_branch, else_variables, else_state);
    if (!else_state.terminated) {
      gcc_jit_block_end_with_jump(else_state.block, location(statement.range), merge_block);
    }

    state.block = merge_block;
    state.terminated = !needs_merge;
  }

  gcc_jit_context* context_ = nullptr;
  gcc_jit_type* i64_type_ = nullptr;
  gcc_jit_type* bool_type_ = nullptr;
  const FunctionDecl& fn_;
  gcc_jit_function* function_ = nullptr;
  std::size_t local_counter_ = 0;
};

std::unique_ptr<gcc_jit_context, ContextDeleter> acquire_configured_context() {
  std::unique_ptr<gcc_jit_context, ContextDeleter> context(gcc_jit_context_acquire());
  if (!context) {
    return context;
  }
#ifdef LIBGCCJIT_HAVE_gcc_jit_context_set_bool_allow_unreachable_blocks
  gcc_jit_context_set_bool_allow_unreachable_blocks(context.get(), 1);
#endif
  gcc_jit_context_set_int_option(context.get(), GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 2);
  return context;
}

GccJitCompileResult lower_module_into_context(gcc_jit_context* context, const Module& module,
                                              std::vector<std::string>& lowered_names) {
  GccJitCompileResult result;
  result.available = true;
  auto* i64_type = gcc_jit_context_get_type(context, GCC_JIT_TYPE_INT64_T);
  auto* bool_type = gcc_jit_context_get_type(context, GCC_JIT_TYPE_BOOL);

  for (const auto& fn : module.functions) {
    std::string detail;
    if (!function_is_lowerable(fn, detail)) {
      result.functions.push_back(GccJitFunctionReport{fn.name, false, detail});
      continue;
    }

    try {
      FunctionLowerer lowerer(context, i64_type, bool_type, fn);
      lowerer.lower();
      lowered_names.push_back(fn.name);
      result.functions.push_back(GccJitFunctionReport{fn.name, true, "lowered"});
    } catch (const std::exception& error) {
      result.functions.push_back(GccJitFunctionReport{fn.name, false, error.what()});
    }
  }

  if (lowered_names.empty()) {
    result.detail = "no functions were lowerable by the GCCJIT backend";
  }
  return result;
}

const FunctionDecl* find_function(const Module& module, const std::string& function_name) {
  for (const auto& fn : module.functions) {
    if (fn.name == function_name) {
      return &fn;
    }
  }
  return nullptr;
}

const GccJitFunctionReport* find_function_report(const GccJitCompileResult& result,
                                                 const std::string& function_name) {
  for (const auto& report : result.functions) {
    if (report.name == function_name) {
      return &report;
    }
  }
  return nullptr;
}

std::string scalar_kind_name(TypeKind kind) {
  if (kind == TypeKind::Bool) {
    return "bool";
  }
  if (kind == TypeKind::I64) {
    return "i64";
  }
  return "unsupported";
}

bool invocation_arguments_match(const FunctionDecl& fn,
                                const std::vector<GccJitScalarValue>& arguments,
                                std::string& detail) {
  if (fn.params.size() != arguments.size()) {
    detail = "function '" + fn.name + "' expects " + std::to_string(fn.params.size()) +
             " argument(s), got " + std::to_string(arguments.size());
    return false;
  }
  for (std::size_t index = 0; index < fn.params.size(); ++index) {
    if (fn.params[index].type.kind != arguments[index].kind) {
      detail = "argument " + std::to_string(index + 1) + " for function '" + fn.name +
               "' must be " + fn.params[index].type.display() + ", got " +
               scalar_kind_name(arguments[index].kind);
      return false;
    }
  }
  return true;
}

template <typename T> T native_arg(const GccJitScalarValue& value);

template <> std::int64_t native_arg<std::int64_t>(const GccJitScalarValue& value) {
  return value.integer;
}

template <> bool native_arg<bool>(const GccJitScalarValue& value) {
  return value.boolean;
}

template <typename Return, typename... Args, std::size_t... Index>
Return invoke_raw_impl(void* code, const std::vector<GccJitScalarValue>& arguments,
                       std::index_sequence<Index...>) {
  using FunctionPointer = Return (*)(Args...);
  const auto fn = reinterpret_cast<FunctionPointer>(code);
  return fn(native_arg<Args>(arguments[Index])...);
}

template <typename Return, typename... Args>
Return invoke_raw(void* code, const std::vector<GccJitScalarValue>& arguments) {
  return invoke_raw_impl<Return, Args...>(code, arguments, std::index_sequence_for<Args...>{});
}

template <typename Return>
Return invoke_with_signature(void* code, const FunctionDecl& fn,
                             const std::vector<GccJitScalarValue>& arguments) {
  if (fn.params.empty()) {
    return invoke_raw<Return>(code, arguments);
  }

  if (fn.params.size() == 1) {
    if (fn.params[0].type.kind == TypeKind::I64) {
      return invoke_raw<Return, std::int64_t>(code, arguments);
    }
    if (fn.params[0].type.kind == TypeKind::Bool) {
      return invoke_raw<Return, bool>(code, arguments);
    }
  }

  if (fn.params.size() == 2) {
    const auto lhs = fn.params[0].type.kind;
    const auto rhs = fn.params[1].type.kind;
    if (lhs == TypeKind::I64 && rhs == TypeKind::I64) {
      return invoke_raw<Return, std::int64_t, std::int64_t>(code, arguments);
    }
    if (lhs == TypeKind::I64 && rhs == TypeKind::Bool) {
      return invoke_raw<Return, std::int64_t, bool>(code, arguments);
    }
    if (lhs == TypeKind::Bool && rhs == TypeKind::I64) {
      return invoke_raw<Return, bool, std::int64_t>(code, arguments);
    }
    if (lhs == TypeKind::Bool && rhs == TypeKind::Bool) {
      return invoke_raw<Return, bool, bool>(code, arguments);
    }
  }

  throw LoweringError("ABI invocation currently supports up to two scalar parameters");
}

GccJitScalarValue invoke_code(void* code, const FunctionDecl& fn,
                              const std::vector<GccJitScalarValue>& arguments) {
  if (fn.return_type.kind == TypeKind::I64) {
    return gccjit_i64(invoke_with_signature<std::int64_t>(code, fn, arguments));
  }
  if (fn.return_type.kind == TypeKind::Bool) {
    return gccjit_bool(invoke_with_signature<bool>(code, fn, arguments));
  }
  throw LoweringError("ABI invocation only supports i64 and bool return values");
}

#endif

} // namespace

GccJitStatus gccjit_status() {
#if SIGIL_HAVE_GCCJIT
  gcc_jit_context* context = gcc_jit_context_acquire();
  if (!context) {
    return {false, "libgccjit was found at build time, but gcc_jit_context_acquire failed"};
  }
  gcc_jit_context_release(context);
  return {true, "libgccjit is available and can allocate a JIT context"};
#else
  return {false, unavailable_detail()};
#endif
}

GccJitCompileResult compile_module_with_gccjit(const Module& module) {
#if SIGIL_HAVE_GCCJIT
  auto context = acquire_configured_context();
  if (!context) {
    return {
        false, false, "libgccjit was found at build time, but gcc_jit_context_acquire failed", {}};
  }

  std::vector<std::string> lowered_names;
  auto result = lower_module_into_context(context.get(), module, lowered_names);

  if (lowered_names.empty()) {
    return result;
  }

  std::unique_ptr<gcc_jit_result, ResultDeleter> jit_result(gcc_jit_context_compile(context.get()));
  if (!jit_result) {
    const char* last_error = gcc_jit_context_get_last_error(context.get());
    result.compiled = false;
    result.detail = last_error ? last_error : "gcc_jit_context_compile failed";
    return result;
  }

  result.compiled = true;
  result.detail = "compiled " + std::to_string(lowered_names.size()) +
                  " function(s): " + join_names(lowered_names);
  return result;
#else
  (void)module;
  return {false, false, unavailable_detail(), {}};
#endif
}

GccJitInvocationResult
invoke_function_with_gccjit(const Module& module, const std::string& function_name,
                            const std::vector<GccJitScalarValue>& arguments) {
#if SIGIL_HAVE_GCCJIT
  GccJitInvocationResult invocation;
  invocation.available = true;

  const auto* fn = find_function(module, function_name);
  if (!fn) {
    invocation.detail = "unknown function '" + function_name + "'";
    return invocation;
  }

  std::string detail;
  if (!invocation_arguments_match(*fn, arguments, detail)) {
    invocation.detail = detail;
    return invocation;
  }

  auto context = acquire_configured_context();
  if (!context) {
    invocation.available = false;
    invocation.detail = "libgccjit was found at build time, but gcc_jit_context_acquire failed";
    return invocation;
  }

  std::vector<std::string> lowered_names;
  const auto lowering = lower_module_into_context(context.get(), module, lowered_names);
  const auto* report = find_function_report(lowering, function_name);
  if (!report || !report->lowered) {
    invocation.detail = report ? report->detail : "function was not lowered";
    return invocation;
  }

  std::unique_ptr<gcc_jit_result, ResultDeleter> jit_result(gcc_jit_context_compile(context.get()));
  if (!jit_result) {
    const char* last_error = gcc_jit_context_get_last_error(context.get());
    invocation.detail = last_error ? last_error : "gcc_jit_context_compile failed";
    return invocation;
  }
  invocation.compiled = true;

  void* code = gcc_jit_result_get_code(jit_result.get(), function_name.c_str());
  if (!code) {
    invocation.detail = "compiled function '" + function_name + "' was not found in JIT result";
    return invocation;
  }

  try {
    invocation.value = invoke_code(code, *fn, arguments);
    invocation.invoked = true;
    invocation.detail = "invoked " + function_name;
  } catch (const std::exception& error) {
    invocation.detail = error.what();
  }
  return invocation;
#else
  (void)module;
  (void)function_name;
  (void)arguments;
  return {false, false, false, unavailable_detail(), {}};
#endif
}

} // namespace sigil

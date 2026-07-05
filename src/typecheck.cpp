#include "sigil/typecheck.hpp"

#include <unordered_map>
#include <unordered_set>

namespace sigil {

namespace {

using FunctionTable = std::unordered_map<std::string, const FunctionDecl*>;
using TheoremTable = std::unordered_map<std::string, const TheoremDecl*>;
using StructTable = std::unordered_map<std::string, const StructDecl*>;
using TypeParamSet = std::unordered_set<std::string>;
using TypeSubstitutions = std::unordered_map<std::string, Type>;

struct CallableContext {
  const FunctionTable& functions;
  const TheoremTable& theorems;
  const FunctionDecl& current_function;
  bool current_is_theorem = false;
  bool allow_theorem_calls = false;
};

bool same_type(const Type& lhs, const Type& rhs) {
  if (lhs.kind == TypeKind::Unknown || rhs.kind == TypeKind::Unknown) {
    if (lhs.kind != rhs.kind || lhs.spelling != rhs.spelling ||
        lhs.arguments.size() != rhs.arguments.size()) {
      return false;
    }
    for (std::size_t index = 0; index < lhs.arguments.size(); ++index) {
      if (!same_type(lhs.arguments[index], rhs.arguments[index])) {
        return false;
      }
    }
    return true;
  }
  return lhs.kind == rhs.kind && lhs.arguments.empty() && rhs.arguments.empty();
}

bool is_reserved_value_name(const std::string& name) {
  return name == "result";
}

bool is_builtin_type_name(const std::string& name) {
  return name == "i64" || name == "bool" || name == "void" || name == "Array" || name == "Slice" ||
         name == "Ref";
}

bool is_declared_struct_type(const Type& type, const StructTable& structs) {
  return type.kind == TypeKind::Unknown && structs.find(type.spelling) != structs.end();
}

bool is_model_container_name(const std::string& name) {
  return name == "Array" || name == "Slice";
}

bool is_model_container_type(const Type& type) {
  return type.kind == TypeKind::Unknown && is_model_container_name(type.spelling);
}

bool is_ref_model_name(const std::string& name) {
  return name == "Ref";
}

bool is_ref_model_type(const Type& type) {
  return type.kind == TypeKind::Unknown && is_ref_model_name(type.spelling);
}

bool is_model_type_name(const std::string& name) {
  return is_model_container_name(name) || is_ref_model_name(name);
}

bool is_model_type(const Type& type) {
  return is_model_container_type(type) || is_ref_model_type(type);
}

bool is_type_parameter_reference(const Type& type, const TypeParamSet& type_params) {
  return type.kind == TypeKind::Unknown && type.arguments.empty() &&
         type_params.find(type.spelling) != type_params.end();
}

bool is_aggregate_type(const Type& type, const StructTable& structs) {
  return is_declared_struct_type(type, structs) || is_model_type(type);
}

const char* aggregate_kind(const StructDecl& decl) {
  return decl.is_container ? "container" : "struct";
}

std::string aggregate_type_label(const StructDecl& decl) {
  return std::string(aggregate_kind(decl)) + " '" + decl.name + "'";
}

bool is_scalar_type(const Type& type) {
  return type.kind == TypeKind::I64 || type.kind == TypeKind::Bool;
}

void require_known_type(const Type& type, const StructTable& structs,
                        const TypeParamSet& type_params, const SourceRange& range,
                        const std::string& owner);

void require_type_argument(const Type& type, const StructTable& structs,
                           const TypeParamSet& type_params, const SourceRange& range,
                           const std::string& owner) {
  require_known_type(type, structs, type_params, range, owner);
  if (type.kind == TypeKind::Void) {
    throw Diagnostic(range, owner + " cannot use void as a type argument");
  }
}

void require_known_type(const Type& type, const StructTable& structs,
                        const TypeParamSet& type_params, const SourceRange& range,
                        const std::string& owner) {
  if (is_type_parameter_reference(type, type_params)) {
    return;
  }
  if (type.kind != TypeKind::Unknown) {
    if (type.has_arguments()) {
      throw Diagnostic(range, "type '" + type.spelling + "' cannot take type arguments");
    }
    return;
  }

  if (is_model_type_name(type.spelling)) {
    const auto actual = type.arguments.size();
    if (actual != 1) {
      throw Diagnostic(range, "model type '" + type.spelling +
                                  "' expects 1 type argument(s), got " + std::to_string(actual));
    }

    const auto& element = type.arguments.front();
    require_type_argument(element, structs, type_params, range,
                          "type argument for '" + type.display() + "'");
    if (!is_type_parameter_reference(element, type_params) && !is_scalar_type(element)) {
      throw Diagnostic(range, "model type '" + type.spelling +
                                  "' element type cannot be aggregate type '" + element.display() +
                                  "'");
    }
    return;
  }

  const auto found = structs.find(type.spelling);
  if (found == structs.end()) {
    throw Diagnostic(range, owner + " uses unsupported type '" + type.display() + "'");
  }

  const auto expected = found->second->type_params.size();
  const auto actual = type.arguments.size();
  if (expected != actual) {
    const auto* kind = aggregate_kind(*found->second);
    if (expected == 0) {
      throw Diagnostic(range, std::string(kind) + " '" + type.spelling +
                                  "' expects 0 type argument(s), got " + std::to_string(actual));
    }
    throw Diagnostic(range, "generic " + std::string(kind) + " '" + type.spelling + "' expects " +
                                std::to_string(expected) + " type argument(s), got " +
                                std::to_string(actual));
  }

  for (const auto& argument : type.arguments) {
    require_type_argument(argument, structs, type_params, range,
                          "type argument for '" + type.display() + "'");
  }
}

void require_known_type(const Type& type, const StructTable& structs, const SourceRange& range,
                        const std::string& owner) {
  require_known_type(type, structs, TypeParamSet{}, range, owner);
}

void require_value_type(const Type& type, const StructTable& structs, const SourceRange& range,
                        const std::string& owner) {
  require_known_type(type, structs, range, owner);
  if (type.kind == TypeKind::Void) {
    throw Diagnostic(range, owner + " cannot use void as a value type");
  }
}

void require_function_parameter_type(const Type& type, const StructTable& structs,
                                     const SourceRange& range, const std::string& owner) {
  require_value_type(type, structs, range, owner);
  if (is_declared_struct_type(type, structs)) {
    throw Diagnostic(range, owner + " cannot use aggregate type '" + type.display() +
                                "' until aggregate function boundaries are supported");
  }
}

void require_function_return_type(const Type& type, const StructTable& structs,
                                  const SourceRange& range, const std::string& owner) {
  require_known_type(type, structs, range, owner);
  if (is_aggregate_type(type, structs)) {
    throw Diagnostic(range, owner + " cannot use aggregate type '" + type.display() +
                                "' until aggregate returns are supported");
  }
}

void insert_symbol(SymbolTable& symbols, const std::string& name, const Type& type,
                   const SourceRange& range, const std::string& owner) {
  if (symbols.find(name) != symbols.end()) {
    throw Diagnostic(range, "duplicate " + owner + " '" + name + "'");
  }
  symbols[name] = type;
}

void require_unreserved_value_name(const std::string& name, const SourceRange& range,
                                   const std::string& owner) {
  if (is_reserved_value_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved name '" + name + "'");
  }
  if (is_builtin_type_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved type name '" + name + "'");
  }
}

void require_unreserved_declaration_name(const std::string& name, const SourceRange& range,
                                         const std::string& owner) {
  if (is_builtin_type_name(name)) {
    throw Diagnostic(range, owner + " cannot use reserved type name '" + name + "'");
  }
}

Type substitute_type(const Type& type, const TypeSubstitutions& substitutions) {
  if (type.kind == TypeKind::Unknown && type.arguments.empty()) {
    const auto found = substitutions.find(type.spelling);
    if (found != substitutions.end()) {
      return found->second;
    }
  }

  Type substituted = type;
  for (auto& argument : substituted.arguments) {
    argument = substitute_type(argument, substitutions);
  }
  return substituted;
}

TypeParamSet collect_type_params(const StructDecl& decl) {
  TypeParamSet params;
  for (const auto& param : decl.type_params) {
    if (is_builtin_type_name(param.name)) {
      throw Diagnostic(param.range,
                       "type parameter cannot use reserved type name '" + param.name + "'");
    }
    if (!params.insert(param.name).second) {
      throw Diagnostic(param.range, "duplicate type parameter '" + param.name + "'");
    }
  }
  return params;
}

TypeSubstitutions build_type_substitutions(const StructDecl& decl, const Type& concrete_type) {
  TypeSubstitutions substitutions;
  for (std::size_t index = 0;
       index < decl.type_params.size() && index < concrete_type.arguments.size(); ++index) {
    substitutions[decl.type_params[index].name] = concrete_type.arguments[index];
  }
  return substitutions;
}

CallableContext with_theorem_calls_allowed(const CallableContext& context) {
  return CallableContext{context.functions, context.theorems, context.current_function,
                         context.current_is_theorem, true};
}

bool is_model_intrinsic_name(const std::string& name) {
  return name == "len" || name == "at" || name == "store" || name == "load" || name == "is_valid" ||
         name == "addr" || name == "same_ref" || name == "disjoint";
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                const CallableContext& context);

Type require_type(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                  const CallableContext& calls, TypeKind expected, const std::string& context);

Type infer_model_intrinsic_expr(const Expr& expr, const SymbolTable& symbols,
                                const StructTable& structs, const CallableContext& context) {
  if (expr->name == "len") {
    if (expr->arguments.size() != 1) {
      throw Diagnostic(expr->range,
                       "len expects 1 argument, got " + std::to_string(expr->arguments.size()));
    }
    const auto container = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_model_container_type(container)) {
      throw Diagnostic(expr->arguments[0]->range, "len expects an Array[T] or Slice[T] argument");
    }
    return Type{TypeKind::I64, "i64", {}};
  }

  if (expr->name == "at") {
    if (expr->arguments.size() != 2) {
      throw Diagnostic(expr->range,
                       "at expects 2 arguments, got " + std::to_string(expr->arguments.size()));
    }
    const auto container = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_model_container_type(container)) {
      throw Diagnostic(expr->arguments[0]->range, "at expects an Array[T] or Slice[T] argument");
    }
    require_type(expr->arguments[1], symbols, structs, context, TypeKind::I64, "at index");
    return container.arguments.front();
  }

  if (expr->name == "store") {
    if (expr->arguments.size() == 2) {
      const auto ref = infer_expr(expr->arguments[0], symbols, structs, context);
      if (is_model_container_type(ref)) {
        throw Diagnostic(expr->range, "store expects 3 arguments, got 2");
      }
      if (!is_ref_model_type(ref)) {
        throw Diagnostic(expr->arguments[0]->range, "store expects a Ref[T] argument");
      }
      const auto value = infer_expr(expr->arguments[1], symbols, structs, context);
      const auto expected = ref.arguments.front();
      if (!same_type(value, expected)) {
        throw Diagnostic(expr->arguments[1]->range, "store value type mismatch: expected " +
                                                        expected.display() + ", found " +
                                                        value.display());
      }
      return ref;
    }

    if (expr->arguments.size() != 3) {
      throw Diagnostic(expr->range,
                       "store expects 3 arguments, got " + std::to_string(expr->arguments.size()));
    }
    const auto container = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_model_container_type(container)) {
      throw Diagnostic(expr->arguments[0]->range, "store expects an Array[T] or Slice[T] argument");
    }
    require_type(expr->arguments[1], symbols, structs, context, TypeKind::I64, "store index");
    const auto value = infer_expr(expr->arguments[2], symbols, structs, context);
    const auto expected = container.arguments.front();
    if (!same_type(value, expected)) {
      throw Diagnostic(expr->arguments[2]->range, "store value type mismatch: expected " +
                                                      expected.display() + ", found " +
                                                      value.display());
    }
    return container;
  }

  if (expr->name == "load") {
    if (expr->arguments.size() != 1) {
      throw Diagnostic(expr->range,
                       "load expects 1 argument, got " + std::to_string(expr->arguments.size()));
    }
    const auto ref = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_ref_model_type(ref)) {
      throw Diagnostic(expr->arguments[0]->range, "load expects a Ref[T] argument");
    }
    return ref.arguments.front();
  }

  if (expr->name == "is_valid") {
    if (expr->arguments.size() != 1) {
      throw Diagnostic(expr->range, "is_valid expects 1 argument, got " +
                                        std::to_string(expr->arguments.size()));
    }
    const auto ref = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_ref_model_type(ref)) {
      throw Diagnostic(expr->arguments[0]->range, "is_valid expects a Ref[T] argument");
    }
    return Type{TypeKind::Bool, "bool", {}};
  }

  if (expr->name == "addr") {
    if (expr->arguments.size() != 1) {
      throw Diagnostic(expr->range,
                       "addr expects 1 argument, got " + std::to_string(expr->arguments.size()));
    }
    const auto ref = infer_expr(expr->arguments[0], symbols, structs, context);
    if (!is_ref_model_type(ref)) {
      throw Diagnostic(expr->arguments[0]->range, "addr expects a Ref[T] argument");
    }
    return Type{TypeKind::I64, "i64", {}};
  }

  if (expr->name == "same_ref" || expr->name == "disjoint") {
    if (expr->arguments.size() != 2) {
      throw Diagnostic(expr->range, expr->name + " expects 2 arguments, got " +
                                        std::to_string(expr->arguments.size()));
    }
    for (const auto& argument : expr->arguments) {
      const auto ref = infer_expr(argument, symbols, structs, context);
      if (!is_ref_model_type(ref)) {
        throw Diagnostic(argument->range, expr->name + " expects Ref[T] arguments");
      }
    }
    return Type{TypeKind::Bool, "bool", {}};
  }

  throw Diagnostic(expr->range, "unknown function '" + expr->name + "'");
}

void validate_predicate(const NamedPredicate& predicate, const SymbolTable& symbols,
                        const StructTable& structs, const CallableContext& context,
                        const std::string& owner);

Type require_type(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                  const CallableContext& calls, TypeKind expected, const std::string& context) {
  const auto actual = infer_expr(expr, symbols, structs, calls);
  if (actual.kind != expected) {
    const auto expected_name = expected == TypeKind::Bool ? "bool" : "i64";
    throw Diagnostic(expr ? expr->range : SourceRange{},
                     context + " must be " + expected_name + ", found " + actual.display());
  }
  return actual;
}

Type infer_binary_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                       const CallableContext& context) {
  const auto lhs = infer_expr(expr->lhs, symbols, structs, context);
  const auto rhs = infer_expr(expr->rhs, symbols, structs, context);

  switch (expr->binary_op) {
  case BinaryOp::Or:
  case BinaryOp::And:
    if (!lhs.is_bool() || !rhs.is_bool()) {
      throw Diagnostic(expr->range, "boolean operator requires bool operands");
    }
    return Type{TypeKind::Bool, "bool", {}};

  case BinaryOp::Less:
  case BinaryOp::LessEqual:
  case BinaryOp::Greater:
  case BinaryOp::GreaterEqual:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->range, "comparison operator requires i64 operands");
    }
    return Type{TypeKind::Bool, "bool", {}};

  case BinaryOp::Add:
  case BinaryOp::Subtract:
  case BinaryOp::Multiply:
  case BinaryOp::Divide:
  case BinaryOp::Modulo:
    if (!lhs.is_integer() || !rhs.is_integer()) {
      throw Diagnostic(expr->range, "arithmetic operator requires i64 operands");
    }
    return Type{TypeKind::I64, "i64", {}};

  case BinaryOp::Equal:
  case BinaryOp::NotEqual:
    if (!same_type(lhs, rhs)) {
      throw Diagnostic(expr->range, "equality operands must have the same type, found " +
                                        lhs.display() + " and " + rhs.display());
    }
    if (lhs.kind == TypeKind::Void) {
      throw Diagnostic(expr->range, "cannot compare void values");
    }
    if (is_aggregate_type(lhs, structs)) {
      throw Diagnostic(expr->range, "equality does not support aggregate type '" + lhs.display() +
                                        "' until structural equality semantics are defined");
    }
    return Type{TypeKind::Bool, "bool", {}};
  }

  throw Diagnostic(expr->range, "unknown binary operator");
}

Type infer_call_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                     const CallableContext& context) {
  const auto found = context.functions.find(expr->name);
  if (found == context.functions.end()) {
    const auto theorem_found = context.theorems.find(expr->name);
    if (theorem_found == context.theorems.end()) {
      if (is_model_intrinsic_name(expr->name)) {
        return infer_model_intrinsic_expr(expr, symbols, structs, context);
      }
      throw Diagnostic(expr->range, "unknown function '" + expr->name + "'");
    }
    if (!context.allow_theorem_calls) {
      throw Diagnostic(expr->range,
                       "theorem '" + expr->name + "' can only be used in proof-only expressions");
    }
    if (context.current_is_theorem && expr->name == context.current_function.name) {
      throw Diagnostic(expr->range, "recursive theorem calls are not supported yet");
    }

    const TheoremDecl& theorem = *theorem_found->second;
    if (expr->arguments.size() != theorem.params.size()) {
      throw Diagnostic(expr->range, "theorem '" + expr->name + "' expects " +
                                        std::to_string(theorem.params.size()) +
                                        " argument(s), got " +
                                        std::to_string(expr->arguments.size()));
    }

    for (std::size_t index = 0; index < expr->arguments.size(); ++index) {
      const auto actual = infer_expr(expr->arguments[index], symbols, structs, context);
      const auto& expected = theorem.params[index].type;
      if (!same_type(actual, expected)) {
        throw Diagnostic(expr->arguments[index]->range,
                         "argument " + std::to_string(index + 1) + " for theorem '" + expr->name +
                             "' type mismatch: expected " + expected.display() + ", found " +
                             actual.display());
      }
    }
    return Type{TypeKind::Bool, "bool", {}};
  }
  if (!context.current_is_theorem && expr->name == context.current_function.name) {
    throw Diagnostic(expr->range, "recursive function calls are not supported yet");
  }

  const FunctionDecl& callee = *found->second;
  if (expr->arguments.size() != callee.params.size()) {
    throw Diagnostic(expr->range, "function '" + expr->name + "' expects " +
                                      std::to_string(callee.params.size()) + " argument(s), got " +
                                      std::to_string(expr->arguments.size()));
  }

  for (std::size_t index = 0; index < expr->arguments.size(); ++index) {
    const auto actual = infer_expr(expr->arguments[index], symbols, structs, context);
    const auto& expected = callee.params[index].type;
    if (!same_type(actual, expected)) {
      throw Diagnostic(expr->arguments[index]->range,
                       "argument " + std::to_string(index + 1) + " for function '" + expr->name +
                           "' type mismatch: expected " + expected.display() + ", found " +
                           actual.display());
    }
  }

  if (callee.return_type.kind == TypeKind::Void) {
    throw Diagnostic(expr->range,
                     "function '" + expr->name + "' returns void and cannot be used as a value");
  }
  require_value_type(callee.return_type, structs, callee.range,
                     "function '" + expr->name + "' return type");
  return callee.return_type;
}

const FieldDecl* find_field(const StructDecl& decl, const std::string& name) {
  for (const auto& field : decl.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

Type infer_struct_literal_expr(const Expr& expr, const SymbolTable& symbols,
                               const StructTable& structs, const CallableContext& context) {
  const auto found = structs.find(expr->literal_type.spelling);
  if (found == structs.end()) {
    throw Diagnostic(expr->range, "unknown aggregate '" + expr->literal_type.spelling + "'");
  }
  const StructDecl& decl = *found->second;
  require_known_type(expr->literal_type, structs, expr->range,
                     std::string(aggregate_kind(decl)) + " literal '" +
                         expr->literal_type.display() + "'");
  const auto type_substitutions = build_type_substitutions(decl, expr->literal_type);

  std::unordered_set<std::string> initialized;
  for (const auto& initializer : expr->field_initializers) {
    const auto* field = find_field(decl, initializer.name);
    if (!field) {
      throw Diagnostic(initializer.range,
                       aggregate_type_label(decl) + " has no field '" + initializer.name + "'");
    }
    if (!initialized.insert(initializer.name).second) {
      throw Diagnostic(initializer.range,
                       "duplicate initializer for field '" + initializer.name + "'");
    }
    const auto actual = infer_expr(initializer.expr, symbols, structs, context);
    const auto expected = substitute_type(field->type, type_substitutions);
    if (!same_type(actual, expected)) {
      throw Diagnostic(initializer.range, "field '" + decl.name + "." + initializer.name +
                                              "' type mismatch: expected " + expected.display() +
                                              ", found " + actual.display());
    }
  }

  for (const auto& field : decl.fields) {
    if (initialized.find(field.name) == initialized.end()) {
      throw Diagnostic(expr->range,
                       "missing initializer for field '" + decl.name + "." + field.name + "'");
    }
  }

  SymbolTable invariant_fields;
  for (const auto& field : decl.fields) {
    invariant_fields[field.name] = substitute_type(field.type, type_substitutions);
  }
  for (const auto& invariant : decl.invariants) {
    validate_predicate(invariant, invariant_fields, structs, context, "invariant");
  }

  return expr->literal_type;
}

Type infer_field_access_expr(const Expr& expr, const SymbolTable& symbols,
                             const StructTable& structs, const CallableContext& context) {
  const auto base_type = infer_expr(expr->lhs, symbols, structs, context);
  if (!is_declared_struct_type(base_type, structs)) {
    throw Diagnostic(expr->range,
                     "field access requires an aggregate value, found " + base_type.display());
  }

  const StructDecl& decl = *structs.at(base_type.spelling);
  const auto* field = find_field(decl, expr->name);
  if (!field) {
    throw Diagnostic(expr->range,
                     aggregate_type_label(decl) + " has no field '" + expr->name + "'");
  }
  return substitute_type(field->type, build_type_substitutions(decl, base_type));
}

Type infer_expr(const Expr& expr, const SymbolTable& symbols, const StructTable& structs,
                const CallableContext& context) {
  if (!expr) {
    throw Diagnostic(SourceRange{}, "missing expression");
  }

  switch (expr->kind) {
  case ExprNode::Kind::Integer:
    return Type{TypeKind::I64, "i64", {}};
  case ExprNode::Kind::Boolean:
    return Type{TypeKind::Bool, "bool", {}};
  case ExprNode::Kind::Identifier: {
    const auto found = symbols.find(expr->name);
    if (found == symbols.end()) {
      throw Diagnostic(expr->range, "unknown identifier '" + expr->name + "'");
    }
    require_value_type(found->second, structs, expr->range, "identifier '" + expr->name + "'");
    return found->second;
  }
  case ExprNode::Kind::Call:
    return infer_call_expr(expr, symbols, structs, context);
  case ExprNode::Kind::StructLiteral:
    return infer_struct_literal_expr(expr, symbols, structs, context);
  case ExprNode::Kind::FieldAccess:
    return infer_field_access_expr(expr, symbols, structs, context);
  case ExprNode::Kind::Unary: {
    const auto operand = infer_expr(expr->lhs, symbols, structs, context);
    if (expr->unary_op == UnaryOp::Not) {
      if (!operand.is_bool()) {
        throw Diagnostic(expr->range, "'!' requires a bool operand");
      }
      return Type{TypeKind::Bool, "bool", {}};
    }
    if (!operand.is_integer()) {
      throw Diagnostic(expr->range, "unary '-' requires an i64 operand");
    }
    return Type{TypeKind::I64, "i64", {}};
  }
  case ExprNode::Kind::Binary:
    return infer_binary_expr(expr, symbols, structs, context);
  case ExprNode::Kind::If: {
    require_type(expr->condition, symbols, structs, context, TypeKind::Bool, "if condition");
    const auto then_type = infer_expr(expr->lhs, symbols, structs, context);
    const auto else_type = infer_expr(expr->rhs, symbols, structs, context);
    if (!same_type(then_type, else_type)) {
      throw Diagnostic(expr->range, "if branches must have the same type, found " +
                                        then_type.display() + " and " + else_type.display());
    }
    if (then_type.kind == TypeKind::Void) {
      throw Diagnostic(expr->range, "if expression cannot produce void");
    }
    if (is_aggregate_type(then_type, structs)) {
      throw Diagnostic(expr->range, "if expression cannot produce aggregate type '" +
                                        then_type.display() +
                                        "' until aggregate merge semantics are defined");
    }
    return then_type;
  }
  }

  throw Diagnostic(expr->range, "unknown expression kind");
}

void validate_predicate(const NamedPredicate& predicate, const SymbolTable& symbols,
                        const StructTable& structs, const CallableContext& context,
                        const std::string& owner) {
  require_type(predicate.expr, symbols, structs, with_theorem_calls_allowed(context),
               TypeKind::Bool, owner + " '" + predicate.name + "'");
}

void validate_statement(const Statement& statement, const FunctionDecl& decl, SymbolTable& locals,
                        std::unordered_set<std::string>& assignable_locals,
                        std::unordered_set<std::string>& proof_labels, const StructTable& structs,
                        const CallableContext& context, bool proof_only_body);

void reject_loop_body_returns(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::Return) {
      throw Diagnostic(statement.range, "while bodies cannot contain return statements yet");
    }
    if (statement.kind == StatementKind::If) {
      reject_loop_body_returns(statement.then_branch);
      reject_loop_body_returns(statement.else_branch);
    } else if (statement.kind == StatementKind::While) {
      reject_loop_body_returns(statement.then_branch);
    }
  }
}

bool block_returns(const std::vector<Statement>& statements);

bool statement_returns(const Statement& statement) {
  if (statement.kind == StatementKind::Return) {
    return true;
  }
  if (statement.kind == StatementKind::If) {
    return block_returns(statement.then_branch) && block_returns(statement.else_branch);
  }
  return false;
}

bool block_returns(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement_returns(statement)) {
      return true;
    }
  }
  return false;
}

void validate_statement_block(const std::vector<Statement>& statements, const FunctionDecl& decl,
                              SymbolTable locals, std::unordered_set<std::string> assignable_locals,
                              std::unordered_set<std::string>& proof_labels,
                              const StructTable& structs, const CallableContext& context,
                              bool proof_only_body) {
  bool terminated = false;
  for (const auto& statement : statements) {
    if (terminated) {
      throw Diagnostic(statement.range, "unreachable statement after guaranteed return");
    }
    validate_statement(statement, decl, locals, assignable_locals, proof_labels, structs, context,
                       proof_only_body);
    terminated = statement_returns(statement);
  }
}

void validate_proof_label(const std::string& name, const SourceRange& range,
                          std::unordered_set<std::string>& proof_labels) {
  if (!proof_labels.insert(name).second) {
    throw Diagnostic(range, "duplicate proof label '" + name + "'");
  }
}

void validate_statement_label(const Statement& statement,
                              std::unordered_set<std::string>& proof_labels) {
  if (!statement.has_explicit_label) {
    return;
  }
  validate_proof_label(statement.name, statement.range, proof_labels);
}

void validate_statement(const Statement& statement, const FunctionDecl& decl, SymbolTable& locals,
                        std::unordered_set<std::string>& assignable_locals,
                        std::unordered_set<std::string>& proof_labels, const StructTable& structs,
                        const CallableContext& context, bool proof_only_body) {
  const auto value_context = proof_only_body ? with_theorem_calls_allowed(context) : context;
  const auto proof_context = with_theorem_calls_allowed(context);

  if (statement.kind == StatementKind::Let) {
    require_unreserved_value_name(statement.name, statement.range,
                                  "local '" + decl.name + "." + statement.name + "'");
    require_value_type(statement.type, structs, statement.range,
                       "local '" + decl.name + "." + statement.name + "'");
    const auto actual = infer_expr(statement.expr, locals, structs, value_context);
    if (!same_type(actual, statement.type)) {
      throw Diagnostic(statement.range, "let type mismatch: expected " + statement.type.display() +
                                            ", found " + actual.display());
    }
    if (is_declared_struct_type(statement.type, structs) &&
        statement.expr->kind != ExprNode::Kind::StructLiteral) {
      throw Diagnostic(statement.range, "struct local '" + decl.name + "." + statement.name +
                                            "' must be initialized with a struct literal");
    }
    insert_symbol(locals, statement.name, statement.type, statement.range, "local");
    assignable_locals.insert(statement.name);
  } else if (statement.kind == StatementKind::Assign) {
    const auto found = locals.find(statement.name);
    if (found == locals.end()) {
      throw Diagnostic(statement.range,
                       "assignment target '" + statement.name + "' is not declared");
    }
    if (assignable_locals.find(statement.name) == assignable_locals.end()) {
      throw Diagnostic(statement.range,
                       "assignment target '" + statement.name + "' is not a mutable local");
    }
    if (is_aggregate_type(found->second, structs)) {
      throw Diagnostic(statement.range, "assignment target '" + statement.name +
                                            "' has aggregate type '" + found->second.display() +
                                            "'; struct assignment is not supported yet");
    }
    const auto actual = infer_expr(statement.expr, locals, structs, value_context);
    if (!same_type(actual, found->second)) {
      throw Diagnostic(statement.range, "assignment type mismatch: expected " +
                                            found->second.display() + ", found " +
                                            actual.display());
    }
  } else if (statement.kind == StatementKind::If) {
    require_type(statement.expr, locals, structs, value_context, TypeKind::Bool,
                 "if statement condition");
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels,
                             structs, context, proof_only_body);
    validate_statement_block(statement.else_branch, decl, locals, assignable_locals, proof_labels,
                             structs, context, proof_only_body);
  } else if (statement.kind == StatementKind::While) {
    require_type(statement.expr, locals, structs, value_context, TypeKind::Bool, "while condition");
    std::unordered_set<std::string> invariant_names;
    for (const auto& invariant : statement.loop_invariants) {
      if (!invariant_names.insert(invariant.name).second) {
        throw Diagnostic(invariant.range, "duplicate loop invariant '" + invariant.name + "'");
      }
      validate_proof_label(invariant.name, invariant.range, proof_labels);
      validate_predicate(invariant, locals, structs, proof_context, "loop invariant");
    }
    reject_loop_body_returns(statement.then_branch);
    validate_statement_block(statement.then_branch, decl, locals, assignable_locals, proof_labels,
                             structs, context, proof_only_body);
  } else if (statement.kind == StatementKind::Assume) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, structs, proof_context, TypeKind::Bool,
                 "assume statement");
  } else if (statement.kind == StatementKind::Assert) {
    validate_statement_label(statement, proof_labels);
    require_type(statement.expr, locals, structs, proof_context, TypeKind::Bool,
                 "assert statement");
  } else if (statement.kind == StatementKind::Return) {
    if (decl.return_type.kind == TypeKind::Void) {
      if (statement.expr) {
        throw Diagnostic(statement.range, "void functions cannot return a value");
      }
      return;
    }
    if (!statement.expr) {
      throw Diagnostic(statement.range, "non-void functions must return a value");
    }
    const auto actual = infer_expr(statement.expr, locals, structs, value_context);
    if (!same_type(actual, decl.return_type)) {
      throw Diagnostic(statement.range, "return type mismatch: expected " +
                                            decl.return_type.display() + ", found " +
                                            actual.display());
    }
  }
}

void validate_struct(const StructDecl& decl, const StructTable& structs,
                     const TheoremTable& theorems) {
  const auto type_params = collect_type_params(decl);
  SymbolTable fields;
  for (const auto& field : decl.fields) {
    require_unreserved_value_name(field.name, field.range,
                                  "field '" + decl.name + "." + field.name + "'");
    require_known_type(field.type, structs, type_params, field.range,
                       "field '" + decl.name + "." + field.name + "'");
    if (field.type.kind == TypeKind::Void) {
      throw Diagnostic(field.range, "field '" + decl.name + "." + field.name +
                                        "' cannot use void as a value type");
    }
    if (!decl.is_container && is_model_type(field.type)) {
      throw Diagnostic(field.range, "field '" + decl.name + "." + field.name +
                                        "' cannot use model type '" + field.type.display() +
                                        "' until aggregate model fields are supported");
    }
    insert_symbol(fields, field.name, field.type, field.range, "field");
  }

  FunctionDecl invariant_context;
  invariant_context.name = decl.name;
  FunctionTable no_functions;
  CallableContext context{no_functions, theorems, invariant_context, false, true};
  std::unordered_set<std::string> invariant_names;
  for (const auto& invariant : decl.invariants) {
    if (!invariant_names.insert(invariant.name).second) {
      throw Diagnostic(invariant.range, "duplicate invariant '" + invariant.name + "'");
    }
    if (type_params.empty()) {
      validate_predicate(invariant, fields, structs, context, "invariant");
    }
  }
}

enum class StructVisitState {
  Visiting,
  Visited,
};

void visit_struct_fields(const StructDecl& decl, const StructTable& structs,
                         std::unordered_map<std::string, StructVisitState>& states) {
  states[decl.name] = StructVisitState::Visiting;

  for (const auto& field : decl.fields) {
    if (!is_declared_struct_type(field.type, structs)) {
      continue;
    }

    const auto state = states.find(field.type.spelling);
    if (state != states.end() && state->second == StructVisitState::Visiting) {
      throw Diagnostic(field.range, "recursive struct value types are not supported yet: field '" +
                                        decl.name + "." + field.name + "' contains '" +
                                        field.type.display() + "' by value");
    }
    if (state == states.end()) {
      visit_struct_fields(*structs.at(field.type.spelling), structs, states);
    }
  }

  states[decl.name] = StructVisitState::Visited;
}

void reject_recursive_struct_values(const StructTable& structs) {
  std::unordered_map<std::string, StructVisitState> states;
  for (const auto& [name, decl] : structs) {
    if (states.find(name) == states.end()) {
      visit_struct_fields(*decl, structs, states);
    }
  }
}

void validate_function(const FunctionDecl& decl, const StructTable& structs,
                       const FunctionTable& functions, const TheoremTable& theorems) {
  CallableContext context{functions, theorems, decl, false, false};
  SymbolTable params;
  for (const auto& param : decl.params) {
    require_unreserved_value_name(param.name, param.range,
                                  "parameter '" + decl.name + "." + param.name + "'");
    require_function_parameter_type(param.type, structs, param.range,
                                    "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.range, "parameter");
  }
  require_function_return_type(decl.return_type, structs, decl.range,
                               "function '" + decl.name + "' return type");

  std::unordered_set<std::string> precondition_names;
  std::unordered_set<std::string> postcondition_names;
  std::unordered_set<std::string> contract_labels;
  for (const auto& precondition : decl.preconditions) {
    if (!precondition_names.insert(precondition.name).second) {
      throw Diagnostic(precondition.range, "duplicate precondition '" + precondition.name + "'");
    }
    contract_labels.insert(precondition.name);
    validate_predicate(precondition, params, structs, context, "precondition");
  }

  SymbolTable post_symbols = params;
  if (decl.return_type.kind != TypeKind::Void) {
    post_symbols["result"] = decl.return_type;
  }
  for (const auto& ensure : decl.ensures) {
    if (!postcondition_names.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate postcondition '" + ensure.name + "'");
    }
    if (!contract_labels.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate contract label '" + ensure.name + "'");
    }
    validate_predicate(ensure, post_symbols, structs, context, "postcondition");
  }

  SymbolTable locals = params;
  std::unordered_set<std::string> assignable_locals;
  std::unordered_set<std::string> proof_labels = contract_labels;
  validate_statement_block(decl.body, decl, locals, assignable_locals, proof_labels, structs,
                           context, false);
  if (decl.return_type.kind != TypeKind::Void && !block_returns(decl.body)) {
    throw Diagnostic(decl.range, "function '" + decl.name + "' must return a value on every path");
  }
}

void validate_theorem(const TheoremDecl& decl, const StructTable& structs,
                      const FunctionTable& functions, const TheoremTable& theorems) {
  FunctionDecl proof_decl;
  proof_decl.name = decl.name;
  proof_decl.params = decl.params;
  proof_decl.return_type = Type{TypeKind::Bool, "bool", {}};
  proof_decl.preconditions = decl.preconditions;
  proof_decl.ensures = decl.ensures;
  proof_decl.body = decl.body;
  proof_decl.location = decl.location;
  proof_decl.range = decl.range;

  CallableContext context{functions, theorems, proof_decl, true, true};
  SymbolTable params;
  for (const auto& param : decl.params) {
    require_unreserved_value_name(param.name, param.range,
                                  "parameter '" + decl.name + "." + param.name + "'");
    require_function_parameter_type(param.type, structs, param.range,
                                    "parameter '" + decl.name + "." + param.name + "'");
    insert_symbol(params, param.name, param.type, param.range, "parameter");
  }

  std::unordered_set<std::string> precondition_names;
  std::unordered_set<std::string> postcondition_names;
  std::unordered_set<std::string> contract_labels;
  for (const auto& precondition : decl.preconditions) {
    if (!precondition_names.insert(precondition.name).second) {
      throw Diagnostic(precondition.range, "duplicate precondition '" + precondition.name + "'");
    }
    contract_labels.insert(precondition.name);
    validate_predicate(precondition, params, structs, context, "precondition");
  }

  SymbolTable post_symbols = params;
  post_symbols["result"] = proof_decl.return_type;
  for (const auto& ensure : decl.ensures) {
    if (!postcondition_names.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate postcondition '" + ensure.name + "'");
    }
    if (!contract_labels.insert(ensure.name).second) {
      throw Diagnostic(ensure.range, "duplicate contract label '" + ensure.name + "'");
    }
    validate_predicate(ensure, post_symbols, structs, context, "postcondition");
  }

  SymbolTable locals = params;
  std::unordered_set<std::string> assignable_locals;
  std::unordered_set<std::string> proof_labels = contract_labels;
  validate_statement_block(proof_decl.body, proof_decl, locals, assignable_locals, proof_labels,
                           structs, context, true);
  if (!block_returns(proof_decl.body)) {
    throw Diagnostic(decl.range, "theorem '" + decl.name + "' must return bool on every path");
  }
}

struct CallEdge {
  std::string callee;
  SourceRange range;
};

using CallGraph = std::unordered_map<std::string, std::vector<CallEdge>>;

void collect_call_edges(const Expr& expr, std::vector<CallEdge>& edges) {
  if (!expr) {
    return;
  }
  if (expr->kind == ExprNode::Kind::Call) {
    edges.push_back(CallEdge{expr->name, expr->range});
    for (const auto& argument : expr->arguments) {
      collect_call_edges(argument, edges);
    }
    return;
  }
  collect_call_edges(expr->condition, edges);
  collect_call_edges(expr->lhs, edges);
  collect_call_edges(expr->rhs, edges);
  for (const auto& field : expr->field_initializers) {
    collect_call_edges(field.expr, edges);
  }
}

void collect_call_edges(const std::vector<NamedPredicate>& predicates,
                        std::vector<CallEdge>& edges) {
  for (const auto& predicate : predicates) {
    collect_call_edges(predicate.expr, edges);
  }
}

void collect_call_edges(const Statement& statement, std::vector<CallEdge>& edges);

void collect_call_edges(const std::vector<Statement>& statements, std::vector<CallEdge>& edges) {
  for (const auto& statement : statements) {
    collect_call_edges(statement, edges);
  }
}

void collect_call_edges(const Statement& statement, std::vector<CallEdge>& edges) {
  collect_call_edges(statement.expr, edges);
  collect_call_edges(statement.loop_invariants, edges);
  collect_call_edges(statement.then_branch, edges);
  collect_call_edges(statement.else_branch, edges);
}

CallGraph build_call_graph(const Module& module) {
  CallGraph graph;
  for (const auto& decl : module.functions) {
    auto& edges = graph[decl.name];
    collect_call_edges(decl.preconditions, edges);
    collect_call_edges(decl.ensures, edges);
    collect_call_edges(decl.body, edges);
  }
  for (const auto& decl : module.theorems) {
    auto& edges = graph[decl.name];
    collect_call_edges(decl.preconditions, edges);
    collect_call_edges(decl.ensures, edges);
    collect_call_edges(decl.body, edges);
  }
  return graph;
}

enum class VisitState {
  Visiting,
  Visited,
};

void visit_call_graph(const std::string& name, const CallGraph& graph,
                      const FunctionTable& functions, const TheoremTable& theorems,
                      std::unordered_map<std::string, VisitState>& states) {
  states[name] = VisitState::Visiting;

  const auto found = graph.find(name);
  if (found != graph.end()) {
    for (const auto& edge : found->second) {
      if (functions.find(edge.callee) == functions.end() &&
          theorems.find(edge.callee) == theorems.end()) {
        continue;
      }
      const auto state = states.find(edge.callee);
      if (state != states.end() && state->second == VisitState::Visiting) {
        throw Diagnostic(edge.range, "recursive function calls are not supported yet");
      }
      if (state == states.end()) {
        visit_call_graph(edge.callee, graph, functions, theorems, states);
      }
    }
  }

  states[name] = VisitState::Visited;
}

void reject_recursive_calls(const Module& module, const FunctionTable& functions,
                            const TheoremTable& theorems) {
  const auto graph = build_call_graph(module);
  std::unordered_map<std::string, VisitState> states;
  for (const auto& decl : module.functions) {
    if (states.find(decl.name) == states.end()) {
      visit_call_graph(decl.name, graph, functions, theorems, states);
    }
  }
  for (const auto& decl : module.theorems) {
    if (states.find(decl.name) == states.end()) {
      visit_call_graph(decl.name, graph, functions, theorems, states);
    }
  }
}

} // namespace

void validate_module(const Module& module) {
  std::unordered_set<std::string> declaration_names;
  std::unordered_set<std::string> struct_names;
  StructTable structs;
  for (const auto& decl : module.structs) {
    require_unreserved_declaration_name(decl.name, decl.range, aggregate_type_label(decl));
    if (!struct_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate aggregate declaration '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    structs[decl.name] = &decl;
  }

  std::unordered_set<std::string> function_names;
  FunctionTable functions;
  for (const auto& decl : module.functions) {
    require_unreserved_declaration_name(decl.name, decl.range, "function '" + decl.name + "'");
    if (!function_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate function '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    functions[decl.name] = &decl;
  }

  std::unordered_set<std::string> theorem_names;
  TheoremTable theorems;
  for (const auto& decl : module.theorems) {
    require_unreserved_declaration_name(decl.name, decl.range, "theorem '" + decl.name + "'");
    if (!theorem_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate theorem '" + decl.name + "'");
    }
    if (!declaration_names.insert(decl.name).second) {
      throw Diagnostic(decl.range, "duplicate top-level declaration '" + decl.name + "'");
    }
    theorems[decl.name] = &decl;
  }

  for (const auto& decl : module.structs) {
    validate_struct(decl, structs, theorems);
  }
  reject_recursive_struct_values(structs);

  for (const auto& decl : module.functions) {
    validate_function(decl, structs, functions, theorems);
  }
  for (const auto& decl : module.theorems) {
    validate_theorem(decl, structs, functions, theorems);
  }
  reject_recursive_calls(module, functions, theorems);
}

} // namespace sigil

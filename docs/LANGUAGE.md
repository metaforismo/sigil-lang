# Sigil Language Notes

Sigil source files use the `.sigil` extension. The current language is small on
purpose: it exists to exercise compiler and proof infrastructure before growing
into a larger systems language.

## Modules

```sigil
module cache;
```

Every file starts with one module declaration.

Top-level struct, theorem, and function names share one declaration namespace.
Built-in type names (`i64`, `bool`, and `void`) are reserved and cannot be
reused as top-level declarations.

Value names are also reserved where they would become source-level proof
symbols. Parameters, local bindings, and struct fields cannot be named
`result`, `i64`, `bool`, or `void`.

## Types

The initial scalar types are:

- `i64`
- `bool`
- `void`

Unknown type names are preserved by the parser so future user-defined types can
be added without redesigning the AST.

Struct types are supported for fields and local values constructed directly
from struct literals. Function parameters and return values are scalar-only for
now, because aggregate function-boundary layout and proof semantics are not
defined yet.

## Structs And Invariants

```sigil
struct CacheLine {
  key: i64;
  value: i64;
  valid: bool;

  invariant valid_key: !valid || key >= 0;
}
```

Struct invariants are parsed and attached to the type. The current checker
validates that they are boolean predicates over declared fields, then registers
them. Struct literal construction emits one proof obligation per declared
invariant. Preservation across future field mutation remains a roadmap item
because the language does not yet expose field assignment.

Field names share the same value-name restrictions as function parameters and
local bindings, because fields become direct symbols while checking invariants.

Struct values can be constructed with named field initializers:

```sigil
let pair: Pair = Pair { left: x, ok: true };
return pair.left;
```

The checker requires every field to be initialized exactly once, rejects unknown
fields, checks each initializer against the declared field type, and checks
field access against the base struct type. The proof planner materializes scalar
fields as ordinary SMT symbols such as `pair.left`, emits invariant obligations
for the constructed value, then assumes the instantiated invariants for later
proof steps. Native lowering deliberately skips functions that manipulate struct
values until the ABI and memory layout rules are explicit.

Struct locals must be initialized directly from struct literals. Struct
assignment, struct equality, and struct-valued conditional expressions are
rejected until Sigil has explicit aggregate copy, structural equality, and
merge semantics.

Struct fields are by value. Recursive struct definitions such as
`Node { next: Node }` are rejected until the language has references or pointer
types.

## Function Contracts

```sigil
fn keep(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  return x;
}
```

`requires` predicates are added to the proof context for the function.
`ensures` predicates become postcondition proof obligations. `result` names the
returned value.

Contract predicates must be boolean. Contract labels share the function's
proof-label namespace, so they must be unique across `requires`, `ensures`, and
explicit body proof labels. `result` is available only in postconditions for
non-void functions. It is a compiler-generated contract symbol, so user
parameters, local bindings, and struct fields cannot be named `result`.

## Proof-Only Theorems

```sigil
theorem add_one_gt for (x: i64)
requires non_negative: x >= 0;
ensures advanced: x + 1 > x;
{
  return x + 1 > x;
}
```

A `theorem` is a proof-only declaration. It has scalar parameters, optional
`requires` and `ensures` contracts, and a body written with the same statement
and expression syntax as functions. The theorem body must return `bool` on
every path. The proof planner adds an implicit `holds` postcondition for every
theorem, proving that the returned boolean is `true`.

Theorem calls are expressions of type `bool`, but they are allowed only in
proof-only positions: struct invariants, function and theorem contracts, loop
invariants, `assume`, `assert`, and theorem bodies. They are rejected in runtime
function value positions such as `let`, assignment, return expressions, and
statement conditions. This keeps theorem reuse available to the prover without
asking the native backend to lower a proof-only declaration.

At a theorem call site, the caller proves the theorem's `requires` clauses.
After that, the theorem's explicit `ensures` clauses and implicit `holds` fact
become assumptions in the caller's proof context. This is the current lemma
reuse mechanism.

## Statements

```sigil
let next: i64 = size + 1;
next = next + 1;
assume cache_bounds: size <= capacity;
assert still_bounded: size <= capacity;
if size >= 0 {
  assert non_negative: size >= 0;
} else {
  assume impossible: false;
}
while next < capacity
invariant lower_bound: next >= 0;
invariant upper_bound: next <= capacity;
{
  next = next + 1;
}
return next;
```

`let` introduces a typed local binding. The binding expression can use
parameters and earlier locals, and the binding becomes a proof fact of the form
`name == expr` for later assertions and returns. Local bindings cannot shadow
parameters or earlier locals, and cannot reuse built-in type names.

`name = expr;` assigns a new value to a previously declared local binding.
Parameters are immutable, undeclared names cannot be assigned, and the assigned
expression must have the same type as the local. The prover represents each
assignment as a new internal version of the local, so facts about earlier values
remain tied to the earlier version.

`assume` extends the local proof context. `assert` creates a proof obligation
from the active context and then becomes available to later obligations.
Explicit `assume name:`, `assert name:`, and loop `invariant name:` labels must
be unique within a function and cannot reuse contract labels, because they
become user-facing proof labels. Unlabeled statements use compiler defaults and
may repeat.
`return expr;` can use parameters and local bindings. `return;` is valid only in
`void` functions. Non-void functions must return a value on every syntactic
control-flow path. For `if` statements, that means both branches must return
unless a later statement returns after the branch. Once a statement guarantees a
return, later statements in the same block are rejected as unreachable.

`if condition { ... } else { ... }` creates two statement branches. The
condition must be `bool`. The then branch is checked under `condition`, and the
else branch is checked under `!condition`. Facts that survive the merge are
guarded by the branch condition, so a fact from only one branch is not treated as
unconditionally true. Local bindings declared inside a branch are scoped to that
branch.

`while condition` requires one or more `invariant name: predicate;` clauses
before the body. Sigil proves each invariant before entering the loop and proves
that one symbolic iteration preserves it. After the loop, the prover assumes the
invariants and the negated loop condition for the loop-exit state. Locals
declared inside the loop body are scoped to the body. Loop bodies cannot contain
`return` statements yet; early loop exits need a real control-flow and
weakest-precondition model before Sigil can prove them honestly.

Function postconditions can mention parameters and `result`, but not body-local
binding names. This keeps contracts independent from implementation-local
details.

Function calls are expressions: `add_one(x)` or `sum(a, b)`. The static
validator resolves the callee across the module, checks arity and argument
types, rejects `void` calls in value position, and rejects direct or indirect
recursive call graphs until the proof planner has a recursive-function model.
Callers prove the callee's `requires` clauses at each call site and may use the
callee's `ensures` clauses as facts about the call result. Theorem calls use the
same modular proof model, but only in proof-only contexts.

The current body language is deliberately tiny. References and memory operations
will require proper control-flow and weakest-precondition generation.

## Expressions

Supported expression forms:

- integer literals: `0`, `42`
- booleans: `true`, `false`
- identifiers: `x`, `result`
- unary operators: `!x`, `-x`
- arithmetic: `+`, `-`, `*`, `/`, `%`
- comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- boolean connectives: `&&`, `||`
- conditionals: `if condition { then_expr } else { else_expr }`
- function calls: `callee(arg1, arg2)`
- theorem calls in proof-only contexts: `lemma(arg1, arg2)`
- struct literals: `TypeName { field: value }`
- field access: `value.field`
- parentheses

Conditional expressions require a `bool` condition, and both branches must have
the same non-void type. They are expressions, not statement blocks: each branch
contains one expression.

Every division or modulo expression creates a compile-time proof obligation
that the divisor is nonzero. The obligation is checked in the expression's
active control-flow context, so a divisor used only inside one conditional
branch can rely on that branch condition. Boolean `&&` and `||` also act as
short-circuit guards for these safety checks: the right side of `a && b` is
checked under `a`, and the right side of `a || b` is checked under `!a`.

These expressions are also the proof language. There is no separate annotation
language.

Integer literal tokens are decimal and must fit in the non-negative `i64` range
`0..9223372036854775807`. A leading `-` is parsed as unary negation, not as part
of the literal token.

The native GCC JIT backend lowers the scalar subset that has a clear source to
native mapping today. It supports `+`, `-`, `*`, comparisons, equality, boolean
operators, conditionals, scalar function calls, locals, assignment, valued
returns, and `void` functions with explicit `return;` or fallthrough. Loops,
division, and modulo remain proof-language constructs until their exact runtime
semantics are pinned down for native lowering.

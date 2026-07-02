# Sigil Language Notes

Sigil source files use the `.sigil` extension. The current language is small on
purpose: it exists to exercise compiler and proof infrastructure before growing
into a larger systems language.

## Modules

```sigil
module cache;
```

Every file starts with one module declaration.

Top-level struct and function names share one declaration namespace. Built-in
type names (`i64`, `bool`, and `void`) are reserved and cannot be reused as
top-level declarations.

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
them. It does not yet prove preservation across constructors or mutating
functions. That preservation check is a core roadmap item.

Field names share the same value-name restrictions as function parameters and
local bindings, because fields become direct symbols while checking invariants.

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

Contract predicates must be boolean. `result` is available only in
postconditions for non-void functions. It is a compiler-generated contract
symbol, so user parameters, local bindings, and struct fields cannot be named
`result`.

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
Explicit `assume name:` and `assert name:` labels must be unique within a
function, because they become user-facing proof labels. Unlabeled statements use
compiler defaults and may repeat.
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
declared inside the loop body are scoped to the body.

Function postconditions can mention parameters and `result`, but not body-local
binding names. This keeps contracts independent from implementation-local
details.

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
operators, conditionals, locals, assignment, and valued returns. Void functions,
loops, division, and modulo remain proof-language constructs until their exact
runtime semantics are pinned down for native lowering.

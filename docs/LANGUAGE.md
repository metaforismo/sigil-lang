# Sigil Language Notes

Sigil source files use the `.sigil` extension. The current language is small on
purpose: it exists to exercise compiler and proof infrastructure before growing
into a larger systems language.

## Modules

```sigil
module cache;
```

Every file starts with one module declaration.

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
postconditions for non-void functions.

## Statements

```sigil
let next: i64 = size + 1;
assume cache_bounds: size <= capacity;
assert still_bounded: size <= capacity;
return next;
```

`let` introduces a typed local binding. The binding expression can use
parameters and earlier locals, and the binding becomes a proof fact of the form
`name == expr` for later assertions and returns. Local bindings cannot shadow
parameters or earlier locals.

`assume` extends the local proof context. `assert` creates a proof obligation
from the active context and then becomes available to later obligations.
`return` can use parameters and local bindings.

Function postconditions can mention parameters and `result`, but not body-local
binding names. This keeps contracts independent from implementation-local
details.

The current body language is deliberately tiny. Reassignment, branches, loops,
references, and memory operations will require proper control-flow and
weakest-precondition generation.

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

These expressions are also the proof language. There is no separate annotation
language.

# Architecture

Sigil starts as a conventional compiler pipeline with a verification stage
inserted before native lowering.

```text
source
  -> lexer
  -> parser
  -> module AST
  -> static validator
  -> proof-obligation planner
  -> SMT-LIB emitter
  -> local checks / Z3
  -> GCC JIT backend
```

## Frontend

The lexer and parser are hand-written C++17. This keeps the grammar easy to
change while the language is still being designed. Parser output is a typed AST
containing structs, fields, invariants, functions, contracts, and body
statements. Numeric literal tokens are converted to exact `i64` AST values at
parse time, and out-of-range literals are rejected with source ranges before
type checking or proof generation. Unterminated struct and statement blocks
report the missing closing brace at EOF instead of falling through to a generic
field or statement diagnostic.

## Static Validation

Before proof obligations are emitted, Sigil validates the current scalar type
system:

- top-level struct and function declarations must have unique names and cannot
  reuse built-in type names;
- parameter, local, and field names cannot reuse built-in type names or the
  compiler-generated `result` symbol;
- struct invariant expressions must be `bool`;
- function preconditions and postconditions must be `bool`;
- function contract labels must be unique across `requires`, `ensures`, and
  explicit body proof labels;
- identifiers must be declared in the active scope;
- function calls must resolve to a module function, use the declared arity and
  argument types, return a value when used as an expression, and avoid direct or
  indirect recursion;
- struct literals must initialize declared fields exactly once, and field access
  must target a field on a struct-typed expression;
- local `let` bindings cannot shadow parameters or earlier locals;
- assignments can only target declared local bindings and must preserve the
  local type;
- conditional expression conditions must be `bool`, and branch types must match;
- statement-level `if` conditions must be `bool`, and branch-local bindings do
  not escape their branch;
- loop conditions and invariants must be `bool`, and loop-body locals do not
  escape their loop body;
- `while` bodies cannot contain `return` statements until the proof planner has
  a full control-flow model for early loop exits;
- explicit `assume`, `assert`, and loop invariant labels must be unique within
  a function and cannot reuse contract labels;
- `result` is only available in postconditions for non-void functions and cannot
  be reused as a parameter, local binding, or field;
- returns must match the declared function return type;
- empty `return;` statements are only valid in `void` functions;
- non-void functions must return a value on every syntactic control-flow path;
- statements after a guaranteed return path are rejected as unreachable;
- unsupported user-defined value types are rejected until the type checker and
  backend know how to represent them.

## Verification Planner

The planner walks each function and builds proof obligations:

- active `requires` predicates become assumptions;
- `let name: type = expr` adds `name` to the symbol table and records
  `name == expr` as an assumption for later obligations;
- function call expressions emit call-site obligations for callee `requires`
  predicates and add callee `ensures` predicates as assumptions over the fresh
  call-result symbol;
- struct literal bindings materialize scalar field facts, and field accesses
  resolve to those field symbols;
- struct literal construction emits invariant obligations for every invariant
  declared on the constructed type;
- `name = expr` creates a fresh internal version of `name` and records that the
  fresh version equals `expr` evaluated in the previous context;
- `assume` statements add local assumptions;
- `assert` statements create obligations;
- division and modulo expressions create `divisor_nonzero` safety obligations at
  the point where the expression is evaluated, with `if`, `&&`, and `||`
  guards reflected in the active assumptions;
- `if` statements build separate then/else proof contexts and merge
  branch-derived facts as guarded assumptions;
- `while` statements create initialization and preservation obligations for
  each user-written invariant, then expose invariant and exit-condition facts at
  the merge point;
- `return expr` records a completed return path with its active assumptions and
  `result == expr`;
- `return;` records a completed void return path without a result binding;
- `ensures` clauses create postcondition obligations for every completed return
  path, plus the fallthrough path of a `void` function when it can reach the end
  without an explicit return.

Every proof obligation carries the source range of the assertion or
postcondition that produced it. Diagnostics and result reporting keep start
locations for quick sorting, but print full ranges when available.

This is not a full weakest-precondition engine yet. It is the first verifiable
spine for contracts written in the language itself.

## Solver Boundary

The SMT emitter serializes each obligation as:

```smt2
(assert assumption_1)
...
(assert (not goal))
(check-sat)
```

An `unsat` result proves that the assumptions imply the goal. A `sat` result is
a counterexample. Anything else is unknown. The CLI can write each query to a
stable `.smt2` artifact path so proof runs can be reproduced outside Sigil.
Expression-level conditionals are represented directly as SMT `ite` terms.

## GCC JIT Backend

CMake detects `libgccjit` and compiles the native backend when available. The
backend lowers a native-supported subset of pure scalar functions into an
in-memory GCC JIT result. It supports `i64` and `bool` parameters, locals,
assignment, scalar function calls, expression and statement conditionals,
`i64`/`bool`/`void` returns, comparisons, boolean operators, and `+`/`-`/`*`
arithmetic.

The ABI smoke path retrieves lowered functions from `gcc_jit_result_get_code`
and invokes a small set of scalar and `void` signatures directly. This keeps
native tests honest: the backend must produce callable code, not only a
compilable IR graph.
When `libgccjit` is enabled, Sigil also requests GCC debug information and
attaches source locations to lowered functions, parameters, expressions,
locals, assignments, branches, jumps, and returns.

Native-lowering reports carry source ranges. Unsupported constructs such as
division and modulo point at the expression that blocked lowering, while
signature and control-flow issues point at the nearest function, parameter, or
statement range.

`sigil compile --save-native-ir <dir>` writes deterministic native-lowering
artifacts beside SMT artifacts. These files list each function's signature,
contracts, lowering status, diagnostic range, body operations, debug-info mode,
and source-to-native debug location map using the same source expression printer
that feeds proof diagnostics. They are intentionally plain text so humans and CI
can compare the solver-visible surface with the native-lowerable surface as the
backend grows.

Contracts, loop invariants, `assume`, and `assert` remain proof-layer
constructs. The native backend erases proof-only constructs after static
validation and proof generation, and currently skips functions containing loops.
Struct values and field access are also skipped by native lowering until layout
and ABI rules are explicit. Division and modulo are deliberately not lowered
yet, because Sigil still needs an explicit source-level semantics that is known
to match the native backend for negative operands and zero divisors.

The project intentionally builds without `libgccjit`, because many development
machines and CI images do not ship it by default.

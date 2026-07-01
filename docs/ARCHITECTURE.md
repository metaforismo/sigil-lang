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
statements.

## Static Validation

Before proof obligations are emitted, Sigil validates the current scalar type
system:

- struct invariant expressions must be `bool`;
- function preconditions and postconditions must be `bool`;
- identifiers must be declared in the active scope;
- local `let` bindings cannot shadow parameters or earlier locals;
- assignments can only target declared local bindings and must preserve the
  local type;
- conditional expression conditions must be `bool`, and branch types must match;
- statement-level `if` conditions must be `bool`, and branch-local bindings do
  not escape their branch;
- `result` is only available in postconditions for non-void functions;
- returns must match the declared function return type;
- unsupported user-defined value types are rejected until the type checker and
  backend know how to represent them.

## Verification Planner

The planner walks each function and builds proof obligations:

- active `requires` predicates become assumptions;
- `let name: type = expr` adds `name` to the symbol table and records
  `name == expr` as an assumption for later obligations;
- `name = expr` creates a fresh internal version of `name` and records that the
  fresh version equals `expr` evaluated in the previous context;
- `assume` statements add local assumptions;
- `assert` statements create obligations;
- `if` statements build separate then/else proof contexts and merge
  branch-derived facts as guarded assumptions;
- `return expr` records `result == expr`;
- `ensures` clauses create postcondition obligations.

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
assignment, expression and statement conditionals, returns, comparisons, boolean
operators, and `+`/`-`/`*` arithmetic.

The ABI smoke path retrieves lowered functions from `gcc_jit_result_get_code`
and invokes a small set of scalar signatures directly. This keeps native tests
honest: the backend must produce callable code, not only a compilable IR graph.

Native-lowering reports carry source ranges. Unsupported constructs such as
division and modulo point at the expression that blocked lowering, while
signature and control-flow issues point at the nearest function, parameter, or
statement range.

Contracts, `assume`, and `assert` remain proof-layer constructs. The native
backend erases them after static validation and proof generation. Division and
modulo are deliberately not lowered yet, because Sigil still needs an explicit
source-level semantics that is known to match the native backend for negative
operands and zero divisors.

The project intentionally builds without `libgccjit`, because many development
machines and CI images do not ship it by default.

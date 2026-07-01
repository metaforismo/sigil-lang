# Architecture

Sigil starts as a conventional compiler pipeline with a verification stage
inserted before native lowering.

```text
source
  -> lexer
  -> parser
  -> module AST
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

## Verification Planner

The planner walks each function and builds proof obligations:

- active `requires` predicates become assumptions;
- `assume` statements add local assumptions;
- `assert` statements create obligations;
- `return expr` records `result == expr`;
- `ensures` clauses create postcondition obligations.

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
a counterexample. Anything else is unknown.

## GCC JIT Backend

CMake detects `libgccjit` and compiles the backend probe when available. The
current backend proves the toolchain link by acquiring a GCC JIT context. Actual
lowering is the next implementation step after the core AST and proof pipeline
settle.

The project intentionally builds without `libgccjit`, because many development
machines and CI images do not ship it by default.

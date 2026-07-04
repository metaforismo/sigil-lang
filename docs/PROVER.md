# Prover Design

Sigil's proof system has three layers.

## 1. Local Checks

The compiler first runs cheap deterministic checks. Today that includes:

- proving a goal if it is already an active assumption;
- proving literal `true`;
- rewriting goals through straight-line equality assumptions produced by `let`
  bindings and assignments, then proving reflexive equalities or rewritten
  active assumptions.

These checks are intentionally small and auditable.

## 2. Z3

If local checks cannot prove an obligation, Sigil emits SMT-LIB and asks Z3.
The proof query is implication by contradiction:

```smt2
(assert A)
(assert B)
(assert (= local expr))
(assert (not Goal))
(check-sat)
```

`unsat` means the goal is proven under the assumptions. `sat` means the goal can
be violated. Solver errors and timeouts should be treated as unknown rather than
silently accepted.

Each obligation keeps the source range of the `assert` or `ensures` clause that
created it. The CLI prints that range with every proof result, which makes
refuted and unknown obligations traceable back to source.

Explicit `assume name:`, `assert name:`, and loop `invariant name:` labels are
validated as unique within each function before proof planning. That keeps
user-facing obligation names and saved SMT artifact names readable as the body
grows.

Local `let` bindings are lowered as equality assumptions. This gives the solver
a simple, checkable representation of straight-line data flow without adding a
separate proof language.

Assignments are lowered into fresh internal symbols. For `y = y + 1`, later
uses of `y` refer to the fresh symbol, while assumptions about the old `y`
continue to refer to the earlier symbol. This keeps mutation explicit in the
SMT-LIB encoding instead of reusing one solver constant for multiple program
states.

Before asking Z3, the local prover performs a small weakest-precondition-style
rewrite over those equality assumptions. If a straight-line mutation chain
reduces an assertion or postcondition to `expr == expr`, Sigil can prove it
locally and deterministically. Branches and loops are still handled by the proof
obligation planner and SMT solver rather than by a full WP calculus.

Function calls are planned modularly. At a call site, Sigil materializes the
callee arguments in the caller's current symbolic context, emits one obligation
for each callee `requires` predicate, introduces a fresh symbol for the call
result, and adds each callee `ensures` predicate as an assumption with
parameters and `result` substituted by the actual arguments and result symbol.
The type checker rejects recursive call graphs for now, so this modular model is
acyclic.

Theorem declarations use the same modular machinery, but they are proof-only.
For proof planning, a theorem is converted into an internal boolean proof
function named `theorem.<name>`. Its explicit `ensures` clauses are checked, and
Sigil adds an implicit `holds` postcondition proving that the theorem's returned
boolean is `true`. When a proof expression calls a theorem, the caller proves
the theorem's `requires` clauses, receives a fresh boolean call-result symbol,
and may use both the explicit theorem postconditions and the implicit `holds`
fact as assumptions. The native backend never lowers theorem declarations.

Struct values are materialized by field. A binding like
`let pair: Pair = Pair { left: x, ok: true };` adds facts for `pair.left == x`
and `pair.ok == true`; later `pair.left` expressions use the scalar field symbol
directly. The planner emits one obligation for each invariant declared on the
constructed struct and then assumes the instantiated invariant for later proof
steps. Mutation-site preservation is not modeled yet because field assignment is
not part of the language.

Conditional expressions are emitted as SMT `ite` terms. For example,
`if x >= 0 { x } else { -x }` becomes `(ite (>= x 0) x (- x))`.

Division and modulo are guarded by safety obligations. Every `/` or `%`
expression creates a `divisor_nonzero` obligation under the active assumptions
at the point where the expression is evaluated. Conditional expressions are
checked branch-sensitively: a divisor used only in the then branch is checked
under the condition, and a divisor used only in the else branch is checked under
the negated condition. Boolean `&&` and `||` guards are also short-circuit aware
for safety checks: the right side of `a && b` is checked under `a`, while the
right side of `a || b` is checked under `!a`.

Statement-level branches are represented with guarded facts at merge points. A
fact learned in the then branch of `if c` is merged as `!c || fact`; a fact
learned in the else branch is merged as `c || fact`. This keeps branch-local
reasoning useful for later obligations without treating one branch's facts as
globally true.

Returns are path-aware. When the planner reaches `return expr`, it records the
current assumptions plus `result == expr` as one completed return path and stops
processing that path. Function `ensures` clauses are checked against every
completed return path, so an early return in one branch cannot be overwritten by
a later return that is reachable only from another branch. Functions with a
single return path keep the stable `fn.name.ensures.N.label` artifact name;
functions with multiple return paths use `fn.name.return.K.ensures.N.label`.

Loops are represented through user-written invariants. For each `while`, Sigil
emits an initialization obligation for every invariant and a preservation
obligation that assumes the invariant and loop condition, symbolically checks
one body iteration, and proves the invariant again. Values assigned in the loop
are treated as fresh loop-exit symbols after the loop; only the invariant and
the negated loop condition survive for those mutated values.

The current implementation calls an external `z3` binary. Set `SIGIL_Z3` to use
a specific executable.

Use `--solver-timeout-ms <ms>` to emit an SMT timeout option for each query. The
timeout is part of dumped and saved SMT-LIB, so standalone solver runs can
reproduce the same budget.

Sigil runs static validation before SMT emission. That matters for soundness:
undeclared identifiers and mismatched predicate types are rejected instead of
being guessed into SMT declarations.

When `--show-model` is enabled and Z3 returns `sat`, Sigil asks a second query
for `(get-model)`. It renders model values that correspond to known Sigil
symbols as a source-level `counterexample`, then prints the raw model as solver
evidence. The model is counterexample evidence, not a proof artifact. The proof
artifact is the SMT-LIB query that produced the result; use `--save-smt <dir>`
to keep those queries on disk.

Use `--save-proof-hints <dir>` to write deterministic handoff files for
obligations that are not proven. A hint includes the source goal, assumptions,
symbols, SMT-LIB query, and a small agent contract. These files are intended for
future lemma search and LLM-assisted triage. They are not proof certificates:
any proposed lemma must be represented back in Sigil or SMT and checked by the
deterministic verifier.

## 3. LLM-Assisted Search

The intended LLM role is lemma discovery and proof search, not final authority.
A future agentic prover may:

- propose loop invariants;
- split large obligations into lemmas;
- suggest arithmetic rewrites;
- summarize counterexamples;
- search for missing preconditions.

Any LLM proposal must be lowered back into SMT or another checkable form. The
compiler must not accept "the model said it is true" as a proof.

## Long-Term Binary Proofs

The long-term target is to reason about lowered code as well as source-level
contracts. Examples include:

- no crash for a bounded input domain;
- no invalid memory access in a data-structure operation;
- a function terminates within a stated cycle bound.

Those claims require a much richer model of generated code, target CPU behavior,
and resource accounting. The repository keeps them in the roadmap rather than
pretending the first compiler scaffold can already do them.

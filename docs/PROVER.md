# Prover Design

Sigil's proof system has three layers.

## 1. Local Checks

The compiler first runs cheap deterministic checks. Today that includes:

- proving a goal if it is already an active assumption;
- proving literal `true`.

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

Local `let` bindings are lowered as equality assumptions. This gives the solver
a simple, checkable representation of straight-line data flow without adding a
separate proof language.

Assignments are lowered into fresh internal symbols. For `y = y + 1`, later
uses of `y` refer to the fresh symbol, while assumptions about the old `y`
continue to refer to the earlier symbol. This keeps mutation explicit in the
SMT-LIB encoding instead of reusing one solver constant for multiple program
states.

Conditional expressions are emitted as SMT `ite` terms. For example,
`if x >= 0 { x } else { -x }` becomes `(ite (>= x 0) x (- x))`.

Statement-level branches are represented with guarded facts at merge points. A
fact learned in the then branch of `if c` is merged as `!c || fact`; a fact
learned in the else branch is merged as `c || fact`. This keeps branch-local
reasoning useful for later obligations without treating one branch's facts as
globally true.

The current implementation calls an external `z3` binary. Set `SIGIL_Z3` to use
a specific executable.

Use `--solver-timeout-ms <ms>` to emit an SMT timeout option for each query. The
timeout is part of dumped and saved SMT-LIB, so standalone solver runs can
reproduce the same budget.

Sigil runs static validation before SMT emission. That matters for soundness:
undeclared identifiers and mismatched predicate types are rejected instead of
being guessed into SMT declarations.

When `--show-model` is enabled and Z3 returns `sat`, Sigil asks a second query
for `(get-model)` and prints that model under the `REFUTED` obligation. The
model is counterexample evidence, not a proof artifact. The proof artifact is
the SMT-LIB query that produced the result; use `--save-smt <dir>` to keep those
queries on disk.

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

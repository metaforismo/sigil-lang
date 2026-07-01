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
(assert (not Goal))
(check-sat)
```

`unsat` means the goal is proven under the assumptions. `sat` means the goal can
be violated. Solver errors and timeouts should be treated as unknown rather than
silently accepted.

The current implementation calls an external `z3` binary. Set `SIGIL_Z3` to use
a specific executable.

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

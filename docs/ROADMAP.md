# Roadmap

## Phase 0: Public Scaffold

- Parse `.sigil` modules.
- Represent contracts and invariants in the AST.
- Emit SMT-LIB proof obligations.
- Run local checks and optional Z3.
- Detect `libgccjit` in CMake.
- Validate scalar proof expressions before SMT emission.
- Report Z3 refutations with optional counterexample models.
- Save emitted SMT-LIB queries as reproducible artifacts.
- Keep CI dependency-light and reproducible.

## Phase 1: Useful Core Language

- Extend the scalar type checker into a real language type checker.
- Add variables, assignment, if/else, loops, and simple structs.
- Generate weakest-precondition obligations for control flow.
- Report source spans for failed or unknown obligations.
- Add counterexample rendering from Z3 models.

## Phase 2: Native Lowering

- Lower pure integer and boolean functions through `libgccjit`.
- Add ABI tests for compiled functions.
- Add debug metadata for mapping native code back to Sigil source.
- Keep solver-visible IR and native-lowered IR aligned.

## Phase 3: Struct Invariant Preservation

- Treat invariants as obligations on construction, mutation, and public exits.
- Add ownership and aliasing rules for low-level memory.
- Support user-defined lemmas for data-structure correctness.
- Build examples for ring buffers, freelists, caches, and intrusive lists.

## Phase 4: Agentic Prover

- Let an LLM propose lemmas, splits, and candidate invariants.
- Validate every proposal with Z3 or another deterministic checker.
- Save proof traces as reproducible artifacts.
- Add budgets, timeouts, and failure modes that are visible to users.

## Phase 5: Binary-Level Experiments

- Export lowered IR and native code facts.
- Model selected target instructions.
- Attempt bounded runtime and crash-safety proofs on small functions.
- Publish negative results and limitations alongside successful proofs.

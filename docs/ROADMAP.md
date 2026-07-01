# Roadmap

This file is the project TODO list. Items move to completed only after code,
tests, and docs are in the repo.

## Completed

- [x] Parse `.sigil` modules.
- [x] Represent contracts and invariants in the AST.
- [x] Emit SMT-LIB proof obligations.
- [x] Run local checks and optional Z3.
- [x] Detect `libgccjit` in CMake.
- [x] Validate scalar proof expressions before SMT emission.
- [x] Report Z3 refutations with optional counterexample models.
- [x] Save emitted SMT-LIB queries as reproducible artifacts.
- [x] Preserve solver timeout budgets in emitted SMT-LIB.
- [x] Support typed local `let` bindings in proof obligations.
- [x] Support expression-level conditionals through SMT `ite`.
- [x] Report source locations for parser, type, and proof results.
- [x] Add full source ranges, not just start locations, to diagnostics.
- [x] Add statement-level `if` and join proof contexts at merge points.
- [x] Add assignment with explicit mutation rules.
- [x] Lower pure integer and boolean functions through `libgccjit`.
- [x] Add ABI tests for JIT-compiled functions.
- [x] Emit native lowering diagnostics with source ranges.
- [x] Keep CI dependency-light and reproducible.

## Immediate Queue

- [ ] Keep solver-visible IR and native-lowered IR aligned.
- [ ] Save lowered IR artifacts beside SMT artifacts.

## Core Language

- [ ] Extend scalar validation into a complete language type checker.
- [ ] Add loops with user-written invariants.
- [ ] Add simple user-defined struct values.
- [ ] Generate weakest-precondition obligations for control flow.
- [ ] Render Z3 counterexamples in Sigil source terms.

## Native Lowering

- [ ] Add debug metadata for mapping native code back to Sigil source.
- [ ] Keep solver-visible IR and native-lowered IR aligned.
- [ ] Save lowered IR artifacts beside SMT artifacts.
- [ ] Add cross-platform backend capability tests.

## Struct Invariant Preservation

- [ ] Treat invariants as obligations on construction, mutation, and public exits.
- [ ] Add ownership and aliasing rules for low-level memory.
- [ ] Support user-defined lemmas for data-structure correctness.
- [ ] Build examples for ring buffers, freelists, caches, and intrusive lists.

## Agentic Prover

- [ ] Let an LLM propose lemmas, splits, and candidate invariants.
- [ ] Validate every proposal with Z3 or another deterministic checker.
- [ ] Save proof traces as reproducible artifacts.
- [ ] Add budgets, timeouts, and failure modes that are visible to users.

## Binary-Level Experiments

- [ ] Export lowered IR and native code facts.
- [ ] Model selected target instructions.
- [ ] Attempt bounded runtime and crash-safety proofs on small functions.
- [ ] Publish negative results and limitations alongside successful proofs.

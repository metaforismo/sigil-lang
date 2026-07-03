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
- [x] Reject out-of-range `i64` integer literals during parsing.
- [x] Report missing struct and statement-block braces at EOF.
- [x] Report Z3 refutations with optional counterexample models.
- [x] Save emitted SMT-LIB queries as reproducible artifacts.
- [x] Save deterministic proof-search hints for unproven obligations.
- [x] Preserve solver timeout budgets in emitted SMT-LIB.
- [x] Render Z3 counterexamples in Sigil source terms.
- [x] Support typed local `let` bindings in proof obligations.
- [x] Support expression-level conditionals through SMT `ite`.
- [x] Report source locations for parser, type, and proof results.
- [x] Add full source ranges, not just start locations, to diagnostics.
- [x] Report missing `sigil check` source paths after options.
- [x] Add statement-level `if` and join proof contexts at merge points.
- [x] Add assignment with explicit mutation rules.
- [x] Validate non-void return coverage across scalar control flow.
- [x] Reject unreachable statements after guaranteed returns.
- [x] Verify postconditions against each completed return path.
- [x] Reserve built-in type names in the top-level declaration namespace.
- [x] Reserve `result` for compiler-generated postcondition values.
- [x] Reserve built-in type names in value namespaces.
- [x] Reject duplicate function contract labels across pre/postconditions.
- [x] Reject contract/body proof-label collisions.
- [x] Reject duplicate explicit proof labels in function bodies.
- [x] Reject loop invariant label collisions in function proof namespaces.
- [x] Support `return;` and path-aware postconditions for `void` functions.
- [x] Generate nonzero-divisor safety obligations for `/` and `%`.
- [x] Honor `if`, `&&`, and `||` guards in expression safety obligations.
- [x] Add loops with user-written invariants.
- [x] Reject early returns inside loops until control-flow proofs support them.
- [x] Add scalar function call expressions with static validation.
- [x] Generate modular call-site proof obligations from callee contracts.
- [x] Add simple struct values and field access.
- [x] Prove declared struct invariants when struct literals are constructed.
- [x] Add local weakest-precondition substitution for straight-line assignments.
- [x] Reject aggregate operations whose copy, merge, equality, or function-boundary
      semantics are not defined yet.
- [x] Reject recursive by-value struct definitions until references exist.
- [x] Lower pure integer and boolean functions through `libgccjit`.
- [x] Lower scalar function calls through `libgccjit`.
- [x] Add ABI tests for JIT-compiled functions.
- [x] Invoke native `libgccjit` functions with mixed scalar signatures.
- [x] Replace native ABI case tables with bounded recursive dispatch up to eight parameters.
- [x] Lower `void` functions through `libgccjit`.
- [x] Invoke native `void` functions through `sigil run`.
- [x] Check `i64` argument ranges for native `sigil run` invocations.
- [x] Emit native lowering diagnostics with source ranges.
- [x] Report successful native invocation source ranges.
- [x] List available functions for unknown `sigil run` targets.
- [x] Show function signatures for native run arity errors.
- [x] Name source parameters in native run conversion errors.
- [x] Keep solver-visible IR and native-lowered IR aligned with native artifacts.
- [x] Save lowered IR artifacts beside SMT artifacts.
- [x] Add debug metadata for mapping native code back to Sigil source.
- [x] Add cross-platform backend capability tests.
- [x] Keep CI dependency-light and reproducible.

## Immediate Queue

- [ ] Extend aggregate typing when ownership, layout, and function-boundary
      semantics are defined.
- [ ] Extend weakest-precondition generation beyond straight-line mutation into
      branch and loop control flow.

## Core Language

- [ ] Extend scalar validation into a complete language type checker.
- [x] Add scalar function call expressions with static validation.
- [x] Add loops with user-written invariants.
- [x] Add simple user-defined struct values.
- [x] Add local weakest-precondition substitution for straight-line assignments.
- [x] Reject aggregate operations whose semantics are not defined yet.
- [x] Reject recursive by-value struct definitions until references exist.
- [ ] Extend weakest-precondition generation beyond straight-line mutation into
      branch and loop control flow.
- [x] Render Z3 counterexamples in Sigil source terms.

## Native Lowering

- [x] Add debug metadata for mapping native code back to Sigil source.
- [x] Keep solver-visible IR and native-lowered IR aligned with native artifacts.
- [x] Save lowered IR artifacts beside SMT artifacts.
- [x] Add cross-platform backend capability tests.
- [x] Lower scalar function calls through `libgccjit`.

## Struct Invariant Preservation

- [x] Treat invariants as obligations on struct literal construction.
- [ ] Treat invariants as obligations on mutation and public exits.
- [ ] Add ownership and aliasing rules for low-level memory.
- [ ] Support user-defined lemmas for data-structure correctness.
- [ ] Build examples for ring buffers, freelists, caches, and intrusive lists.

## Agentic Prover

- [x] Save deterministic proof-search hints for unproven obligations.
- [ ] Let an LLM propose lemmas, splits, and candidate invariants.
- [ ] Validate every proposal with Z3 or another deterministic checker.
- [ ] Save proof traces as reproducible artifacts.
- [ ] Add budgets, timeouts, and failure modes that are visible to users.

## Binary-Level Experiments

- [ ] Export lowered IR and native code facts.
- [ ] Model selected target instructions.
- [ ] Attempt bounded runtime and crash-safety proofs on small functions.
- [ ] Publish negative results and limitations alongside successful proofs.

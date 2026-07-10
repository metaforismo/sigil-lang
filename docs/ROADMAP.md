# Roadmap

This file is the project TODO list. Items move to completed only after code,
tests, and docs are in the repo.

## Current Objective

Bridge the original source-level invariant idea to the working compiler by
building the smallest useful path from reusable lemmas to generic
data-structure specifications. Proof-only theorem declarations, lemma reuse,
generic struct instantiation, proof-level array/slice bounds, and a proof-level
reference scaffold are now in place. Deterministic agent-candidate validation
also exists through `sigil agent-check`, and first-class proof-level container
declarations can now carry array, slice, and reference model fields. Array and
slice models also have immutable proof-level `store` facts, and reference
models have immutable proof-level store facts for validity-preserving value
updates, explicit proof-level epoch tokens, and same-snapshot alias consistency
for valid references. Reference stores now also have an explicit proof-level
write-permission gate. Arrays, slices, and references now share explicit
allocation-identity facts that aliases and immutable stores preserve. Every
modeled read and write now proves a shared allocation-liveness fact.
Allocation-level owner and borrow-state facts are explicit and preserved,
shared/mutable borrow acquire/release transitions are checked, and all modeled
stores require owner presence plus an active mutable borrow.
The next step is allocation/lifetime transitions, provenance, alias
invalidation, and function-boundary semantics.

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
- [x] Save deterministic agent requests and theorem-candidate skeletons for
      unproven obligations.
- [x] Validate theorem-candidate files through a deterministic proof-only
      `sigil agent-check` acceptance gate.
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
- [x] Add proof-only theorem declarations.
- [x] Reuse theorem calls as lemma facts in proof-only expressions.
- [x] Add generic struct declarations with concrete type-argument validation.
- [x] Instantiate generic struct fields and invariants before proof emission.
- [x] Add first-class proof-level container declarations with model fields.
- [x] Add proof-level `Array[T]` and `Slice[T]` model types.
- [x] Emit `index_in_bounds` safety obligations for `at(container, index)`.
- [x] Lower array and slice reads to SMT `select` over abstract backing arrays.
- [x] Add immutable proof-level `store` facts for array and slice models.
- [x] Add proof-level `Ref[T]` model types.
- [x] Emit `memory_valid` safety obligations for `load(ref)`.
- [x] Add modeled address, same-reference, and disjoint-reference predicates.
- [x] Add immutable proof-level `store` facts for reference models.
- [x] Add same-snapshot alias consistency for valid same-address references.
- [x] Add proof-level reference epoch tokens and store epoch advancement.
- [x] Add proof-level reference write-permission facts and store write gates.
- [x] Add cross-model allocation identity and preserve it through aliases and
      immutable stores.
- [x] Add cross-model allocation liveness, access gates, and preservation
      through aliases and immutable stores.
- [x] Add allocation-level owner and borrow-state facts with consistency
      invariants and preservation.
- [x] Add checked shared/mutable borrow and release transitions.
- [x] Gate array, slice, and reference stores on owner presence and an active
      mutable borrow, preserving that state in the successor snapshot.
- [x] Add simple struct values and field access.
- [x] Prove declared struct invariants when struct literals are constructed.
- [x] Add local weakest-precondition substitution for straight-line assignments.
- [x] Extend bounded local weakest-precondition reasoning across branch joins
      and loop-exit invariant summaries.
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
- [x] Save binary-proof experiment fact artifacts beside native IR artifacts.
- [x] Add cross-platform backend capability tests.
- [x] Keep CI dependency-light and reproducible.

## Immediate Queue

- [x] Connect array/slice/reference updates to memory-state and ownership
      rules.
- [ ] Define ownership, allocation, lifetime, and provenance for references.
- [x] Extend weakest-precondition generation beyond straight-line mutation into
      branch and loop control flow.
- [ ] Execute an external agent loop that proposes candidate lemmas from saved
      requests and feeds them through `sigil agent-check`.

## Core Language

- [ ] Extend scalar validation into a complete language type checker.
- [x] Add scalar function call expressions with static validation.
- [x] Add proof-only theorem declarations and theorem-call lemma reuse.
- [x] Add loops with user-written invariants.
- [x] Add simple user-defined struct values.
- [x] Add local weakest-precondition substitution for straight-line assignments.
- [x] Reject aggregate operations whose semantics are not defined yet.
- [x] Reject recursive by-value struct definitions until references exist.
- [x] Add generic struct declarations with clear proof-instantiation rules.
- [ ] Define monomorphized runtime layout rules for generic aggregates.
- [x] Add first-class container declarations after generic instantiation is
      explicit.
- [x] Add arrays and slices with explicit length, bounds, and element read
      facts.
- [x] Add immutable array and slice update facts through SMT array `store`.
- [x] Add reference address and disjointness facts.
- [x] Add same-snapshot alias consistency for valid references.
- [x] Add reference write-permission facts and store write gates.
- [x] Add array and slice allocation-alias facts beyond modeled reference
      addresses.
- [ ] Add slice origin, offset, and overlapping-view facts within one
      allocation.
- [x] Extend weakest-precondition generation beyond straight-line mutation into
      branch and loop control flow.
- [x] Render Z3 counterexamples in Sigil source terms.

## Native Lowering

- [x] Add debug metadata for mapping native code back to Sigil source.
- [x] Keep solver-visible IR and native-lowered IR aligned with native artifacts.
- [x] Save lowered IR artifacts beside SMT artifacts.
- [x] Save binary-proof experiment fact artifacts beside native IR artifacts.
- [x] Add cross-platform backend capability tests.
- [x] Lower scalar function calls through `libgccjit`.

## Struct Invariant Preservation

- [x] Treat invariants as obligations on struct literal construction.
- [x] Reuse proof-only theorems as lemmas inside invariant and contract proofs.
- [x] Instantiate generic struct invariants over concrete field types.
- [x] Treat container invariants as obligations over scalar and model fields.
- [ ] Treat invariants as obligations on mutation and public exits.
- [ ] Add ownership and aliasing rules for low-level memory.
- [ ] Support user-defined lemmas for data-structure correctness.
- [ ] Build examples for ring buffers, freelists, caches, and intrusive lists.

## Agentic Prover

- [x] Save deterministic proof-search hints for unproven obligations.
- [x] Represent reusable source-level lemmas as checked `theorem` declarations.
- [x] Save agent request artifacts with checked-theorem candidate skeletons.
- [x] Feed candidate theorem declarations through the ordinary Sigil parser,
      typechecker, proof planner, and checker before accepting them.
- [ ] Let an LLM propose lemmas, splits, and candidate invariants.
- [ ] Validate every proposal with Z3 or another deterministic checker.
- [ ] Save proof traces as reproducible artifacts.
- [ ] Add budgets, timeouts, and failure modes that are visible to users.

## Memory Model

- [ ] Define reference and pointer types separately from by-value structs.
- [x] Add proof-level `Ref[T]` validity, address, and load facts.
- [x] Add proof-level `Ref[T]` store facts that preserve validity and address.
- [x] Add same-snapshot alias consistency for valid same-address references.
- [x] Add proof-level reference epoch tokens and store epoch advancement.
- [x] Add proof-level reference write-permission facts and store write gates.
- [x] Add abstract allocation identity across arrays, slices, and references.
- [x] Add explicit allocation-liveness facts and gate modeled reads and writes.
- [x] Add owner identity/presence and shared/mutable borrow-state facts.
- [x] Add checked shared/mutable borrow acquire and release transitions.
- [x] Require owner presence and an active mutable borrow for every modeled
      array, slice, and reference store.
- [ ] Model allocation/deallocation transitions, consuming ownership, and alias
      invalidation as explicit proof facts.
- [ ] Prove bounds, initialization, and no-crash properties for arrays and
      slices before native lowering relies on them.
- [ ] Connect source-level memory facts to native IR and binary-proof artifacts.

## Binary-Level Experiments

- [x] Export lowered IR and native proof-experiment facts.
- [ ] Model selected target instructions.
- [ ] Attempt bounded runtime and crash-safety proofs on small functions.
- [ ] Publish negative results and limitations alongside successful proofs.

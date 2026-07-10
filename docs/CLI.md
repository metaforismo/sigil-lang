# CLI Reference

## `sigil check`

```sh
sigil check <file.sigil> [--dump-smt] [--save-smt <dir>]
                  [--save-proof-hints <dir>] [--save-agent-requests <dir>]
                  [--show-model]
                  [--solver-timeout-ms <ms>] [--strict] [--no-z3]
```

`check` parses a Sigil module, validates scalar expression types, builds proof
obligations, emits SMT-LIB, and optionally asks Z3 to discharge obligations that
the local checker cannot prove. Each proof result includes the source range of
the `assert` or `ensures` clause that produced it. Single-line ranges are
printed as `file:line:start-end`.

The summary reports struct, container, theorem, and function counts. Proof-only
theorem obligations use stable names such as
`theorem.add_one_gt.ensures.1.advanced`.
When a function calls a theorem in a proof-only expression, the usual call-site
`requires` obligations are emitted under the caller, and the theorem's
postconditions become assumptions for later obligations in that proof context.
Generic struct instantiations use the same proof artifact flow: concrete fields
are visible in SMT under materialized names such as `box.value`, and invariant
obligations are named from the function, local binding, and invariant label.
Container instantiations use the same flow, but their model fields also expose
SMT facts such as `window.items.len == xs.len` and
`window.items.data == xs.data`, together with allocation identity.
Array and slice model accesses add `memory_live` and `index_in_bounds` safety
obligations and emit SMT array `select` terms for `at(model, index)`. Array and
slice `store(model, index, value)` updates are immutable proof facts: they
preserve length and memory state, require liveness, ownership, and an active
mutable borrow, emit write-bounds obligations, and emit SMT array `store` terms
for the updated backing data.
Reference model loads add `memory_live` and `memory_valid` safety obligations
and expose modeled allocation, liveness, address, validity, write-permission,
epoch, and value symbols in SMT.
Reference `store(ref, value)` updates are immutable proof facts: they require
write-site liveness, ownership, an active mutable borrow, validity, and write
permission; preserve modeled address, validity, write permission, allocation
identity, liveness, and ownership state; update the modeled referenced value;
and advance the modeled epoch. `allocation_id`,
`is_live`, `same_allocation`, and `disjoint_allocation` expose common abstract
allocation facts across array, slice, and reference models.
`owner_id`, `has_owner`, `shared_borrows`, and `has_mut_borrow` expose common
ownership state and deterministic consistency constraints.
Borrow/release transition bindings emit ordered liveness, ownership, and
availability/active obligations before updating a fresh model snapshot.
Stores preserve active mutable-borrow state until `release_mut` produces a
later snapshot.

The proof set also includes safety obligations such as
`fn.name.safety.N.divisor_nonzero` for division and modulo expressions. Those
obligations point at the divisor expression that must be proven nonzero. Guards
from `if`, `&&`, and `||` are reflected in the assumptions for the safety query.
If options are provided without `<file.sigil>`, `check` reports the missing
source path directly.

Options:

- `--dump-smt`: print each emitted SMT-LIB query to stdout.
- `--save-smt <dir>`: write every emitted SMT-LIB query to `<dir>`.
- `--save-proof-hints <dir>`: write one deterministic proof-search handoff
  artifact for each obligation that is not proven. Each hint contains the
  source goal, active assumptions, visible symbols, optional SMT path, embedded
  SMT-LIB, and an explicit agent contract that proposals must be rechecked by
  Sigil and Z3.
- `--save-agent-requests <dir>`: write deterministic agent request artifacts
  and theorem-candidate skeletons for obligations that are not proven. These
  files are work queues for external proof-search agents; they are not proof
  certificates, and candidate lemmas must be inserted into source and rechecked
  with `sigil check --strict`.
- `--show-model`: when Z3 refutes an obligation, ask Z3 for a model, render the
  scalar values as a Sigil source-level `counterexample`, and keep the raw Z3
  model under the refuted result.
- `--solver-timeout-ms <ms>`: emit an SMT timeout option for each query. The
  timeout is preserved in `--dump-smt` output and `--save-smt` artifacts.
- `--strict`: exit non-zero if any obligation is `UNKNOWN`.
- `--no-z3`: skip Z3 and run local checks only.

Example with generic structs and a theorem-backed invariant:

```sh
sigil check examples/generics.sigil --strict --solver-timeout-ms 250 --save-smt build/generic-smt
```

Example with proof-level container model fields:

```sh
sigil check examples/containers.sigil --strict --solver-timeout-ms 250 --save-smt build/container-smt
```

Example with proof-level array and slice bounds:

```sh
sigil check examples/slices.sigil --strict --solver-timeout-ms 250 --save-smt build/slice-smt
```

Example with proof-level array and slice update facts:

```sh
sigil check examples/model_updates.sigil --strict --solver-timeout-ms 250 --save-smt build/model-update-smt
```

Example with proof-level reference validity and alias predicates:

```sh
sigil check examples/memory.sigil --strict --solver-timeout-ms 250 --save-smt build/memory-smt
```

Example with proof-level reference update facts:

```sh
sigil check examples/ref_updates.sigil --strict --solver-timeout-ms 250 --save-smt build/ref-update-smt
```

Example with proof-level reference alias consistency:

```sh
sigil check examples/ref_aliases.sigil --strict --solver-timeout-ms 250 --save-smt build/ref-alias-smt
```

Example with proof-level reference epoch facts:

```sh
sigil check examples/ref_epochs.sigil --strict --solver-timeout-ms 250 --save-smt build/ref-epoch-smt
```

Example with proof-level reference write-permission facts:

```sh
sigil check examples/ref_permissions.sigil --strict --solver-timeout-ms 250 --save-smt build/ref-permission-smt
```

Example with cross-model allocation identity and store preservation:

```sh
sigil check examples/allocation_identity.sigil --strict --solver-timeout-ms 250 --save-smt build/allocation-identity-smt
```

Example with allocation-liveness gates and preservation:

```sh
sigil check examples/allocation_liveness.sigil --strict --solver-timeout-ms 250 --save-smt build/allocation-liveness-smt
```

Example with ownership and borrow-state facts:

```sh
sigil check examples/ownership_state.sigil --strict --solver-timeout-ms 250 --save-smt build/ownership-state-smt
```

Example with checked shared and mutable borrow transitions:

```sh
sigil check examples/borrow_transitions.sigil --strict --solver-timeout-ms 250 --save-smt build/borrow-transition-smt
```

Example with borrow-checked array, slice, and reference updates:

```sh
sigil check examples/memory_state_updates.sigil --strict --solver-timeout-ms 250 --save-smt build/memory-state-update-smt
```

Exit codes:

- `0`: parsing, validation, and proof checking completed with no refutations or
  errors; `UNKNOWN` results are allowed unless `--strict` is set.
- `1`: command-line, parse, validation, or file-read error.
- `2`: at least one obligation was `REFUTED` or `ERROR`, or `--strict` found an
  `UNKNOWN` obligation.
- `3`: `sigil backend`, `sigil compile`, or `sigil run` was run and the binary
  was built without a usable `libgccjit` backend.

Result statuses:

- `PROVEN`: the obligation was discharged by the local checker or by Z3.
- `REFUTED`: Z3 found a model that satisfies the assumptions and violates the
  goal.
- `UNKNOWN`: no checker proved or refuted the obligation.
- `ERROR`: checking could not complete for that obligation.

## `sigil agent-check`

```sh
sigil agent-check <candidate.sigil> [--dump-smt] [--save-smt <dir>]
                  [--show-model] [--solver-timeout-ms <ms>]
                  [--strict] [--no-z3]
```

`agent-check` is the deterministic acceptance gate for theorem-candidate files
written by a human or an external proof-search agent. The command does not trust
the candidate file as a proof certificate and does not mutate project source. It
parses the candidate as ordinary Sigil source, runs static validation, builds
the usual proof obligations, and checks them with the local prover and optional
Z3.

The accepted candidate surface is proof-only: runtime `fn` declarations are
reported and rejected. Struct, container, and generic aggregate declarations are
allowed so a candidate can define proof-level shapes needed by its theorem
declarations.

Options match the proof-related subset of `sigil check`:

- `--dump-smt`: print each emitted SMT-LIB query to stdout.
- `--save-smt <dir>`: write every emitted SMT-LIB query to `<dir>`.
- `--show-model`: keep Z3 counterexample models for refuted obligations.
- `--solver-timeout-ms <ms>`: preserve a per-query timeout in emitted SMT-LIB.
- `--strict`: reject candidates that leave any obligation `UNKNOWN`.
- `--no-z3`: skip Z3 and run local checks only.

Example:

```sh
sigil agent-check examples/agent_candidate.sigil \
  --strict --no-z3 --save-smt build/agent-candidate-smt
```

Exit codes:

- `0`: the candidate stayed inside the proof-only surface and all proof results
  are acceptable under the selected strictness.
- `1`: command-line, parse, validation, or file-read error.
- `2`: the candidate used runtime declarations, had a refuted/error result, or
  `--strict` found an `UNKNOWN` obligation.

## `sigil backend`

```sh
sigil backend
```

Reports whether the current binary was built with `libgccjit` support and can
allocate a GCC JIT context. The command also prints the capability surface that
the current build exposes:

- `compiled-with-libgccjit`: whether this binary was compiled with the backend.
- `jit-context`: whether a GCC JIT context can be allocated now.
- `native-lowering`: whether native lowering can run.
- `abi-invocation`: whether lowered functions can be invoked through the test
  ABI path.
- `debug-info`: whether GCCJIT debug information is requested for lowered code.
- `native-ir-artifacts`: whether deterministic native-lowering artifacts can be
  emitted.
- `binary-proof-artifacts`: whether deterministic binary-proof experiment facts
  can be emitted.

## `sigil compile`

```sh
sigil compile <file.sigil> [--dump-native-ir] [--save-native-ir <dir>]
              [--dump-binary-facts] [--save-binary-facts <dir>]
```

Parses and validates a module, then asks the GCC JIT backend to lower every
native-supported function into an in-memory JIT result. The command reports each
function as `lowered` or `skipped`. When a function is skipped, the report
includes an `at:` source range for the construct that made native lowering
unsupported.

Options:

- `--dump-native-ir`: print deterministic native-lowering artifacts for every
  function. Each artifact includes whether GCCJIT debug information was
  requested and a `debug-locations` map from native-lowered nodes back to Sigil
  source ranges.
- `--save-native-ir <dir>`: write one native-lowering artifact per function to
  `<dir>`, using stable names such as `fn.add_one.native-ir.txt`.
- `--dump-binary-facts`: print deterministic binary-proof experiment facts for
  every function.
- `--save-binary-facts <dir>`: write one binary-proof experiment artifact per
  function to `<dir>`, using stable names such as
  `fn.add_one.binary-facts.txt`. These artifacts record native-lowering status,
  the linked native-IR artifact name, source contracts, source body surface, and
  explicit negative claims such as `cycle-bound-proven no`.

The current native subset supports pure scalar functions over `i64` and `bool`
using:

- parameters and local `let` bindings;
- assignment to declared locals;
- statement and expression conditionals;
- unary `!` and `-`;
- arithmetic `+`, `-`, and `*`;
- comparisons, equality, `&&`, and `||`;
- `void` functions with explicit `return;` or fallthrough;
- proof-only `assume` and `assert` statements, which are erased before native
  lowering.

Proof-only `theorem` declarations are validated before native lowering and then
ignored by the native backend. They do not produce native IR or binary-facts
artifacts.

Functions containing `while` loops are checked by `sigil check`, but are
skipped by native lowering for now. Division and modulo are intentionally
skipped until Sigil pins down the exact source semantics and proves they match
the native lowering.

Binary-facts artifacts are scaffolding for future binary-level solvers. They do
not contain machine-code bytes, target instruction semantics, crash-safety
proofs, or cycle-bound proofs yet.

Exit codes:

- `0`: at least one function was lowered and GCC JIT compilation succeeded.
- `1`: command-line, parse, validation, or file-read error.
- `2`: `libgccjit` was available, but no function could be lowered or GCC JIT
  compilation failed.
- `3`: the binary was built without a usable `libgccjit` backend.

## `sigil run`

```sh
sigil run <file.sigil> <function> [args...]
```

Parses and validates a module, lowers it with `libgccjit`, retrieves the named
function from the JIT result, and invokes it through the native ABI. Arguments
are parsed from the function signature: `i64` parameters accept signed decimal
integer strings in the `int64_t` range, and `bool` parameters accept `true`,
`false`, `1`, or `0`. Argument-conversion errors include both the argument
position and the source parameter name.
Successful invocations include an `at:` source range for the invoked function.
If the requested function is not present, the error lists the module's available
function names in source order.
If the requested function has the wrong number of arguments, the error includes
the source-level function signature.
If lowering or invocation fails after parsing, the output includes an `at:`
source range when Sigil can identify a relevant parameter, statement, or
expression.

Example:

```sh
sigil run examples/native.sigil add_one 41
```

The command currently supports `i64`, `bool`, and `void` return values with up
to eight scalar parameters. That is enough to exercise the first ABI contract
without pretending Sigil has a complete FFI or runtime yet.

Exit codes:

- `0`: the function was compiled, found in the JIT result, invoked, and returned
  a supported value.
- `1`: command-line, parse, validation, argument-conversion, or file-read error.
- `2`: `libgccjit` was available, but lowering, JIT compilation, symbol lookup,
  or invocation failed.
- `3`: the binary was built without a usable `libgccjit` backend.

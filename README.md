# Sigil

Sigil is an experimental systems language and compiler for code that carries its
own proof obligations.

The compiler frontend parses ordinary language declarations plus `requires`,
`ensures`, `assert`, `assume`, and `invariant` clauses written in the same
expression syntax as the program. It emits verification conditions as SMT-LIB,
can ask Z3 to discharge them at compile time, and includes a conditional
`libgccjit` backend that lowers a small scalar subset into an in-memory GCC JIT
module.

The long-term direction is a compiler where low-level data-structure
correctness, cache invariants, crash-safety properties, and eventually bounded
runtime claims can be expressed as code and checked before shipping a binary.
This repository is the first public, working scaffold for that path. It is not a
production verifier yet.

## Current Status

- Hand-written lexer and parser for `.sigil` modules.
- First-class syntax for struct invariants, function preconditions,
  postconditions, assumptions, assertions, and returns.
- Typed local `let` bindings that become proof facts for later assertions and
  returns.
- Function call expressions with static callee, arity, argument-type, and
  acyclic-call-graph validation.
- Proof-only `theorem` declarations with scalar parameters, contracts, boolean
  bodies, and reusable lemma facts.
- Theorem calls are allowed only in proof-only expressions such as contracts,
  invariants, `assume`, `assert`, and theorem bodies; they are rejected in
  runtime value positions.
- Generic struct declarations with type parameters, concrete type arguments,
  arity checks, and duplicate-parameter validation.
- Generic struct instantiations substitute concrete field types before
  typechecking field initializers, field access, and invariant predicates.
- Simple struct value literals and field access expressions over declared
  struct fields.
- Struct literal construction emits proof obligations for declared struct
  invariants.
- Generic struct invariant obligations are emitted over instantiated field
  symbols, so `Box[bool].value` reaches SMT as `Bool`, not as an opaque type
  parameter.
- First-class `container` declarations on the generic aggregate foundation,
  with proof-level `Array[T]`, `Slice[T]`, and `Ref[T]` fields that materialize
  allocation/liveness/length/data/address/validity/write/value/epoch facts in
  SMT.
- Proof-level `Array[T]` and `Slice[T]` model types with `len(value)` and
  `at(value, index)` intrinsics.
- `at` emits ordered allocation-liveness, index-in-bounds, and
  memory-initialized proof obligations. Together they are the compile-time
  no-crash gate for modeled container reads.
- Immutable proof-level `store(model, index, value)` facts for `Array[T]` and
  `Slice[T]`: stores require a live owned allocation under an active mutable
  borrow, preserve length and memory state, emit write-bounds obligations, and
  lower data and initialization masks to SMT array `store`.
- Proof-level `Ref[T]` model types with `is_valid(ref)`, `can_write(ref)`,
  `addr(ref)`, `epoch(ref)`, `load(ref)`, `same_ref(left, right)`, and
  `disjoint(left, right)` intrinsics.
- `load` emits allocation-liveness and memory-valid proof obligations before
  exposing the modeled referenced value.
- Valid same-epoch, same-address `Ref[T]` snapshots of the same element type
  imply equal modeled loaded values in SMT.
- Immutable proof-level `store(ref, value)` facts for `Ref[T]`: stores require
  a live owned allocation under an active mutable borrow plus reference
  validity and write permission. They preserve modeled address, validity,
  permission, and ownership state, update the value, and advance the epoch.
- Cross-model allocation identity through `allocation_id(value)`,
  `same_allocation(left, right)`, and `disjoint_allocation(left, right)` for
  arrays, slices, and references. Aliases and immutable stores preserve the
  deterministic allocation token.
- Cross-model snapshot provenance through `epoch(value)` and
  `same_snapshot(left, right)`. Fresh allocations start at epoch zero; aliases,
  views, borrows, and owner moves preserve epochs; stores and deallocation
  advance them. Snapshot equality requires both allocation and epoch equality.
- Cross-model allocation liveness through `is_live(value)`. Every array/slice
  access and every reference load/store must prove liveness; aliases and
  immutable stores preserve the liveness fact.
- Allocation-level ownership and borrow-state facts through `owner_id`,
  `has_owner`, `shared_borrows`, and `has_mut_borrow`. The prover enforces
  nonnegative shared counts, mutable/shared exclusion, owner-ID consistency,
  and preservation through aliases and stores.
- Checked immutable borrow transitions through `borrow_shared`,
  `release_shared`, `borrow_mut`, and `release_mut`. Each transition proves
  liveness, owner presence, and its operation-specific availability/active
  condition before updating the borrow snapshot.
- Writes are connected to those transitions: a mutable borrow can be acquired,
  preserved through array, slice, or reference `store`, and explicitly
  released in a later proof-state snapshot.
- Consuming `move_owner` and `deallocate` transitions with static use-after-move
  rejection and ordered liveness, ownership, borrow-free, and conservative
  allocation-uniqueness obligations. Deallocation yields a dead tombstone and
  possible aliases block the transition.
- Fresh initialized `allocate_array`, `allocate_slice`, and `allocate_ref`
  constructors. They establish live, owned, borrow-free state, typed initial
  contents, deterministic lifetime-token freshness, and fresh current reference
  addresses; sized allocations must prove a nonnegative length.
- Raw `allocate_uninit_array` and `allocate_uninit_slice` constructors plus
  `is_initialized(model, index)`. Raw masks start false, model stores set exactly
  their physical write index true, and aliases, views, borrows, moves, and
  tombstones preserve mask snapshots.
- Explicit initialization contracts at function boundaries. Array and slice
  parameters have unconstrained masks; callees declare the slots they read with
  `requires is_initialized(model, index)`, and modular calls prove those
  predicates over the caller's current constructor/store/view snapshot.
- Assignment to previously declared locals, lowered through versioned proof
  symbols so old and new values stay distinct.
- Bounded control-flow weakest-precondition reasoning: the local prover splits
  branch-join `ite` goals path by path and composes loop-exit invariants for
  conjunctive postconditions without replacing Z3 arithmetic reasoning.
- Expression-level `if condition { then } else { else }` conditionals that lower
  to SMT `ite`.
- Statement-level `if`/`else` branches with branch-local proof contexts and
  guarded facts at merge points.
- `while` loops with user-written invariants, initialization and preservation
  proof obligations, and loop-exit facts.
- Void functions with explicit `return;` and path-aware postcondition checks.
- Static validation for predicate types, identifier scope, duplicate symbols,
  reserved declaration and contract names, explicit proof-label uniqueness,
  return types, non-void return coverage, and unreachable statements after
  guaranteed returns.
- Verification-condition generation for function assertions and return-path
  postconditions.
- Modular call-site proof obligations: callers prove callee `requires`
  predicates and then assume callee `ensures` predicates on the call result.
- Theorem proof obligations use `theorem.name.*` artifact names and include an
  implicit `holds` postcondition proving that the theorem's returned boolean is
  true.
- Branch- and short-circuit-aware arithmetic safety obligations for division and
  modulo divisors.
- SMT-LIB emission with optional Z3 execution through `z3` or `SIGIL_Z3`.
- Source-level counterexample rendering for refuted Z3 models.
- Deterministic proof-search hint artifacts for obligations that are not proven.
- Deterministic agent handoff artifacts with theorem-candidate skeletons for
  unproven obligations.
- Deterministic `agent-check` validation for theorem-candidate files: candidates
  are parsed, typechecked, planned, and proven as ordinary proof-only Sigil
  modules before they can be treated as accepted lemmas.
- A budgeted `agent-refine` loop for external proposers. It enforces attempt,
  proposer, and solver budgets; preserves contracts and proof surfaces; rejects
  new assumptions; and saves replayable candidate, status, and SMT traces.
- Allocation-relative slice views with checked range construction, compositional
  offsets, half-open overlap predicates, and offset-aware reads and stores.
- CMake detection for `libgccjit`; builds without it and reports backend status.
- Native lowering for pure `i64`/`bool` functions using function calls, `let`,
  assignment, conditionals, arithmetic `+`/`-`/`*`, comparisons, boolean
  operators, and `i64`/`bool`/`void` returns.
- ABI smoke tests that invoke JIT-compiled scalar and `void` functions with up
  to eight scalar parameters.
- Native-lowering diagnostics that report source ranges for unsupported
  constructs.
- Native IR artifacts that list signatures, contracts, body operations, and
  lowering status for comparison with SMT artifacts.
- GCCJIT debug-info requests plus deterministic source-to-native debug location
  maps in saved native artifacts.
- Binary-proof experiment fact artifacts that link to native IR and explicitly
  record that crash-safety and cycle-bound claims are not proven yet.
- Native and binary artifacts carry per-function source proof obligations,
  identify memory-safety gates, preserve solver goals and source ranges, and
  explicitly mark source proof as not run by `compile`.
- Cross-platform backend capability tests for builds with and without
  `libgccjit`.
- CI that exercises the portable compiler core, solver-backed Z3 smoke checks,
  and a Linux `libgccjit` native-lowering smoke path.

## Example

```sigil
module arithmetic;

fn keep(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  let y: i64 = if x >= 0 { x + 1 } else { 1 };
  assert y_above_x: y >= x;
  return y;
}
```

Run it:

```sh
cmake -S . -B build -DSIGIL_ENABLE_GCCJIT=OFF
cmake --build build
ctest --test-dir build --output-on-failure
./build/sigil check examples/arithmetic.sigil --dump-smt
```

For a fuller local setup on macOS:

```sh
brew install z3 libgccjit clang-format
cmake -S . -B build -DSIGIL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If Z3 is installed and available on `PATH`, `sigil check` asks it to prove any
obligation that the local syntactic prover cannot discharge. To use a specific
binary:

```sh
SIGIL_Z3=/path/to/z3 ./build/sigil check examples/arithmetic.sigil --strict
```

Save SMT artifacts and show counterexample models:

```sh
./build/sigil check examples/cache.sigil --strict --solver-timeout-ms 250 --save-smt build/smt
./build/sigil check examples/theorems.sigil --no-z3 --save-smt build/theorem-smt
./build/sigil check examples/generics.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/containers.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/slices.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/slice_views.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/model_updates.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/memory.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/ref_updates.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/ref_aliases.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/ref_epochs.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/ref_permissions.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/allocation_identity.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/allocation_liveness.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/ownership_state.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/borrow_transitions.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/memory_state_updates.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/consuming_deallocation.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/fresh_allocation.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/initialization_safety.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/function_boundary_initialization.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/model_epochs.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/control_flow_wp.sigil --strict --solver-timeout-ms 250
./build/sigil check examples/assignments.sigil --no-z3 --save-proof-hints build/proof-hints
./build/sigil check examples/assignments.sigil --no-z3 --save-agent-requests build/agent-requests
./build/sigil agent-check examples/agent_candidate.sigil --strict --no-z3 --save-smt build/agent-candidate-smt
./build/sigil agent-refine path/to/source.sigil --agent-command path/to/proposer --max-attempts 3 --agent-timeout-ms 30000 --solver-timeout-ms 250 --save-trace build/agent-trace
./build/sigil compile examples/native.sigil --save-native-ir build/native-ir --save-binary-facts build/binary-facts
./build/sigil check examples/refuted.sigil --strict --show-model
```

For refuted obligations, `--show-model` prints both Sigil-level values and the
raw Z3 model.

Check whether the native backend was compiled with `libgccjit`:

```sh
./build/sigil backend
```

The backend report lists the current build's JIT context, native lowering, ABI
invocation, debug-info, and native artifact capabilities.

Compile the native-lowerable subset into an in-memory GCC JIT module:

```sh
./build/sigil compile examples/native.sigil
./build/sigil compile examples/native.sigil --save-native-ir build/native-ir
```

Saved native artifacts include the debug-info mode and source ranges for the
function, parameters, contracts, statements, and expression nodes that feed the
native lowering.

Run a native-lowered function through the JIT ABI:

```sh
./build/sigil run examples/native.sigil add_one 41
```

`sigil check` also runs static validation before building proof obligations:
contract expressions must be boolean, identifiers must be declared, `result`
cannot be reused as a parameter or local binding, return expressions must match
the function return type, non-void functions cannot use empty returns,
unreachable statements after guaranteed returns are rejected, and unsupported
value types are rejected before SMT is emitted.

Full command details are in [docs/CLI.md](docs/CLI.md).

## Language Shape

Sigil intentionally keeps the proof surface close to the program surface:

- `invariant` attaches a predicate to a struct.
- `theorem` declares a proof-only lemma in Sigil syntax.
- `struct Box[T]` declares a generic struct whose concrete uses write
  `Box[i64]`, `Box[bool]`, or another fully supplied type argument list.
- `container Window[T]` declares a proof-level aggregate that can carry model
  fields such as `Slice[T]` and invariants over those fields.
- `requires` and `ensures` attach contracts to a function.
- `assume` introduces a fact into the current proof context.
- `assert` creates a proof obligation.
- `result` names the returned value in postconditions and is reserved there.

More detail is in [docs/LANGUAGE.md](docs/LANGUAGE.md).

## Architecture

```text
.sigil source
  -> lexer/parser
  -> typed module model
  -> verification-condition planner
  -> SMT-LIB emitter
  -> local syntactic prover or Z3
  -> GCC JIT native lowering
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/PROVER.md](docs/PROVER.md). CLI behavior is documented in
[docs/CLI.md](docs/CLI.md).

## Roadmap

The next hard pieces are, in order:

- path-sensitive alias invalidation and aggregate return/effect semantics built
  on the current allocation and snapshot provenance tokens;
- aggregate layout, copy, aliasing, and function-boundary semantics;
- a memory model that connects reference and container facts to low-level data
  structures;
- richer proposer policies and remote model adapters around the deterministic
  `agent-refine` executable protocol;
- binary-level proof experiments for bounded runtime and crash-safety claims.

The roadmap is tracked in [docs/ROADMAP.md](docs/ROADMAP.md).

## Contributing

This is early-stage compiler research code. Small, test-backed changes are
preferred. See [CONTRIBUTING.md](CONTRIBUTING.md) and
[SECURITY.md](SECURITY.md).

## License

Apache-2.0. See [LICENSE](LICENSE).

# Architecture

Sigil starts as a conventional compiler pipeline with a verification stage
inserted before native lowering.

```text
source
  -> lexer
  -> parser
  -> module AST
  -> static validator
  -> proof-obligation planner
  -> SMT-LIB emitter
  -> local checks / Z3
  -> GCC JIT backend
```

## Frontend

The lexer and parser are hand-written C++17. This keeps the grammar easy to
change while the language is still being designed. Parser output is a typed AST
containing structs, containers, fields, invariants, proof-only theorems,
functions, contracts, and body statements. Struct and container declarations may
carry generic type parameters, and type nodes preserve nested type arguments
such as `PairBox[i64, bool]`, `Slice[i64]`, `Array[bool]`, and `Ref[i64]`.
Numeric literal tokens are converted to exact `i64` AST values at parse time,
and out-of-range literals are rejected with source ranges before type checking
or proof generation. Unterminated aggregate and statement blocks report the
missing closing brace at EOF instead of falling through to a generic field or
statement diagnostic.

## Static Validation

Before proof obligations are emitted, Sigil validates the current scalar type
system:

- top-level struct, container, theorem, and function declarations must have
  unique names and cannot reuse built-in type names;
- generic aggregate type parameter names must be unique and cannot reuse
  built-in type names;
- `Array`, `Slice`, and `Ref` are reserved proof model type names;
- `Array[T]`, `Slice[T]`, and `Ref[T]` must have one scalar element type
  argument;
- parameter, local, and field names cannot reuse built-in type names or the
  compiler-generated `result` symbol;
- non-generic aggregate invariant expressions must be `bool`;
- generic aggregate invariant expressions are validated at each concrete
  instantiation after type parameters are substituted;
- function preconditions and postconditions must be `bool`;
- function contract labels must be unique across `requires`, `ensures`, and
  explicit body proof labels;
- identifiers must be declared in the active scope;
- function parameters can be scalar values or proof-level array/slice/reference
  models; ordinary struct parameters and aggregate returns are rejected until
  function-boundary layout and proof semantics are explicit;
- function calls must resolve to a module function, use the declared arity and
  argument types, return a value when used as an expression, and avoid direct or
  indirect recursion;
- theorem calls must resolve to a module theorem, use the declared arity and
  argument types, and appear only in proof-only expressions;
- `len(model)`, `at(model, index)`, and `store(model, index, value)` are
  built-in array/slice model intrinsics; `len` returns `i64`, `at` returns the
  model element type, and `store` returns the same model type after checking
  the stored value type;
- `is_valid(ref)`, `can_write(ref)`, `addr(ref)`, `load(ref)`,
  `store(ref, value)`, `epoch(ref)`, `same_ref(left, right)`, and
  `disjoint(left, right)` are built-in reference model intrinsics;
- `allocation_id(value)`, `same_allocation(left, right)`, and
  `disjoint_allocation(left, right)` are built-in allocation-identity
  intrinsics for array, slice, and reference proof models;
- `is_live(value)` is a built-in allocation-liveness intrinsic for every memory
  proof model;
- `owner_id`, `has_owner`, `shared_borrows`, and `has_mut_borrow` expose common
  allocation-level ownership state;
- `borrow_shared`, `release_shared`, `borrow_mut`, and `release_mut` return
  updated memory-model snapshots after checked transition guards;
- aggregate literals must use valid generic arity, initialize declared fields
  exactly once, and field access must target a field on an aggregate-typed
  expression;
- container fields may use `Array[T]`, `Slice[T]`, or `Ref[T]` proof models;
  ordinary struct fields still reject model types;
- local `let` bindings cannot shadow parameters or earlier locals;
- aggregate-typed locals must be initialized directly from aggregate literals,
  except proof model locals may be initialized from model aliases, and array,
  slice, or reference model locals may be initialized from materialized
  proof-level `store` updates;
- assignments can only target declared local bindings and must preserve the
  local type; struct assignment is rejected until aggregate copy semantics are
  defined;
- conditional expression conditions must be `bool`, and branch types must match;
- conditional expressions and equality comparisons cannot produce or compare
  aggregate values until merge and structural equality semantics are defined;
- recursive by-value aggregate fields are rejected until the language has
  explicit reference or pointer types;
- statement-level `if` conditions must be `bool`, and branch-local bindings do
  not escape their branch;
- loop conditions and invariants must be `bool`, and loop-body locals do not
  escape their loop body;
- `while` bodies cannot contain `return` statements until the proof planner has
  a full control-flow model for early loop exits;
- explicit `assume`, `assert`, and loop invariant labels must be unique within
  a function and cannot reuse contract labels;
- `result` is only available in postconditions for non-void functions and cannot
  be reused as a parameter, local binding, or field;
- returns must match the declared function return type;
- empty `return;` statements are only valid in `void` functions;
- non-void functions must return a value on every syntactic control-flow path;
- statements after a guaranteed return path are rejected as unreachable;
- unsupported user-defined value operations are rejected until the type checker,
  prover, and backend know how to represent them.

## Verification Planner

The planner walks each function and builds proof obligations:

- active `requires` predicates become assumptions;
- `let name: type = expr` adds `name` to the symbol table and records
  `name == expr` as an assumption for later obligations;
- function call expressions emit call-site obligations for callee `requires`
  predicates and add callee `ensures` predicates as assumptions over the fresh
  call-result symbol;
- theorem declarations are planned before functions as proof-only boolean
  subjects named `theorem.<name>`, with explicit postconditions plus an implicit
  `holds` postcondition;
- theorem call expressions in proof contexts emit call-site obligations for
  theorem `requires` predicates and add explicit theorem `ensures` plus
  implicit `holds` as reusable assumptions;
- aggregate literal bindings materialize scalar field facts, with generic type
  parameters substituted by concrete type arguments before symbols are recorded;
- field accesses resolve to those field symbols;
- struct literal construction emits invariant obligations for every invariant
  declared on the constructed type after generic substitution;
- container literal construction emits the same invariant obligations, and
  model fields are materialized by component facts such as `value.len`,
  `value.data`, `value.offset`, `value.alloc`, `value.live`, `value.addr`,
  `value.valid`, `value.write`, `value.owner`, `value.has_owner`, `value.shared`,
  `value.mut_borrow`, `value.value`, and `value.epoch`;
- `name = expr` creates a fresh internal version of `name` and records that the
  fresh version equals `expr` evaluated in the previous context;
- `assume` statements add local assumptions;
- `assert` statements create obligations;
- division and modulo expressions create `divisor_nonzero` safety obligations at
  the point where the expression is evaluated, with `if`, `&&`, and `||`
  guards reflected in the active assumptions;
- array and slice `at(container, index)` expressions create `memory_live` then
  `index_in_bounds` safety obligations and lower to SMT `select` over an
  allocation-wide abstract backing array at `container.offset + index`;
- `slice_view(source, start, length)` bindings create `memory_live` then
  `view_in_bounds` obligations, compose the source offset with `start`, and
  preserve allocation, backing data, liveness, and ownership state;
- `same_view` and `overlaps` lower to allocation-relative half-open range facts
  for equally typed slices;
- array and slice `store(container, index, value)` bindings create
  `memory_live`, `ownership_present`, `mutable_borrow_active`, and
  `index_in_bounds` safety obligations, preserve the source length, offset,
  allocation, liveness, and ownership state, and lower the updated data fact to
  SMT array `store` at the allocation-relative physical index;
- allocation intrinsics lower every model's deterministic `.alloc` component
  to identity or inequality facts across arrays, slices, and references;
- ownership intrinsics lower deterministic owner-presence, owner-ID, shared
  count, and mutable-borrow components with common consistency invariants;
- borrow transition bindings create `memory_live`, `ownership_present`, and
  operation-specific safety obligations, preserve non-borrow state, and update
  shared/mutable state in fresh proof symbols;
- reference `load(ref)` expressions create `memory_live` and `memory_valid`
  safety obligations and lower to the modeled referenced value;
- reference `can_write(ref)` expressions lower to deterministic proof-level
  write-permission symbols;
- reference `epoch(ref)` expressions lower to deterministic proof-level memory
  snapshot tokens;
- same-type reference snapshots gain deterministic alias-consistency
  assumptions so valid refs with equal modeled epochs and addresses have equal
  modeled values in the same proof context;
- reference `store(ref, value)` bindings create `memory_live`,
  `ownership_present`, `mutable_borrow_active`, `memory_valid`, and
  `memory_write` safety obligations, preserve modeled address, validity, write
  permission, allocation identity, liveness, and ownership state, replace the
  modeled referenced value, and advance the modeled epoch;
- `if` statements build separate then/else proof contexts and merge
  branch-derived facts as guarded assumptions;
- the local control-flow WP pass splits merged `ite` goals, specializes guarded
  facts per branch, and requires both paths to prove the selected goal;
- `while` statements create initialization and preservation obligations for
  each user-written invariant, then expose invariant and exit-condition facts at
  the merge point;
- loop-exit invariants are tagged as summaries so conjunctive postconditions
  can be decomposed and checked locally against them;
- `return expr` records a completed return path with its active assumptions and
  `result == expr`;
- `return;` records a completed void return path without a result binding;
- `ensures` clauses create postcondition obligations for every completed return
  path, plus the fallthrough path of a `void` function when it can reach the end
  without an explicit return.

Every proof obligation carries the source range of the assertion or
postcondition that produced it. Diagnostics and result reporting keep start
locations for quick sorting, but print full ranges when available.

The local prover runs weakest-precondition-style rewrites for straight-line
equalities, bounded branch splitting for merged `ite` values, and conjunction
decomposition over loop-exit summaries. These rules prove simple control-flow
contracts without invoking Z3, but deliberately leave arithmetic induction,
invariant discovery, and unbounded implication search at the solver boundary.

## Solver Boundary

The SMT emitter serializes each obligation as:

```smt2
(assert assumption_1)
...
(assert (not goal))
(check-sat)
```

An `unsat` result proves that the assumptions imply the goal. A `sat` result is
a counterexample. Anything else is unknown. The CLI can write each query to a
stable `.smt2` artifact path so proof runs can be reproduced outside Sigil.
Expression-level conditionals are represented directly as SMT `ite` terms.

For obligations that remain unproven, the CLI can also write proof-search hint
artifacts. These are deterministic handoff files containing the source goal,
assumptions, symbols, optional SMT path, embedded SMT-LIB, and the rule that any
agent proposal must be checked by Sigil and Z3 before it can influence
compilation.

The CLI can also write agent request artifacts and theorem-candidate skeletons.
These files form the first durable agentic SMT loop boundary: an external agent
can read a request, propose Sigil theorem declarations or source changes, and
then hand those changes back to the normal parser, typechecker, proof planner,
and Z3 path. The compiler does not trust or ingest agent output directly.

`sigil agent-check` makes that boundary scriptable. It runs a theorem-candidate
file through the same frontend and proof planner, saves optional SMT artifacts,
and reports `accepted` only when the proof-only candidate surface is respected
and the selected verification policy accepts every obligation. It is a
validation command, not an import command: accepted candidates must still be
reviewed and wired into source explicitly.

`sigil agent-refine` orchestrates that recheck for complete candidate modules.
It invokes a model-agnostic external proposer through a fixed argv protocol,
enforces process-group wall-time and attempt budgets, rejects contract or proof
surface weakening and new assumptions, allows additional checked proof steps,
and accepts only a strict full-module proof. Request, candidate, proposer-log,
proof-ledger, and SMT artifacts make each checker decision replayable without
trusting the proposer. Candidate paths are cleared before invocation and the
trace is atomically persisted after each decision, so stale or interrupted
agent runs cannot silently cross the acceptance boundary. External process
orchestration currently requires POSIX process-group semantics.

## GCC JIT Backend

CMake detects `libgccjit` and compiles the native backend when available. The
backend lowers a native-supported subset of pure scalar functions into an
in-memory GCC JIT result. It supports `i64` and `bool` parameters, locals,
assignment, scalar function calls, expression and statement conditionals,
`i64`/`bool`/`void` returns, comparisons, boolean operators, and `+`/`-`/`*`
arithmetic.

The ABI smoke path retrieves lowered functions from `gcc_jit_result_get_code`
and invokes a small set of scalar and `void` signatures directly. This keeps
native tests honest: the backend must produce callable code, not only a
compilable IR graph.
When `libgccjit` is enabled, Sigil also requests GCC debug information and
attaches source locations to lowered functions, parameters, expressions,
locals, assignments, branches, jumps, and returns.

Native-lowering reports carry source ranges. Unsupported constructs such as
division and modulo point at the expression that blocked lowering, while
signature and control-flow issues point at the nearest function, parameter, or
statement range.

`sigil compile --save-native-ir <dir>` writes deterministic native-lowering
artifacts beside SMT artifacts. These files list each function's signature,
contracts, lowering status, diagnostic range, body operations, debug-info mode,
and source-to-native debug location map using the same source expression printer
that feeds proof diagnostics. They are intentionally plain text so humans and CI
can compare the solver-visible surface with the native-lowerable surface as the
backend grows. Each artifact also contains the function's planned source proof
obligations and a separately counted memory-safety subset with stable names,
solver goals, and source ranges. The section is marked `not-run-by-compile`.

`sigil compile --save-binary-facts <dir>` writes a second deterministic artifact
family for binary-level proof experiments. These files point back to the native
IR artifact name, record whether a function is a lowered candidate, and state
that machine-code bytes, target instruction semantics, crash-safety proofs, and
cycle-bound proofs are not available yet. The goal is to give future external
binary provers a stable handoff format without making claims the compiler cannot
check. Binary artifacts repeat the source obligation ledger and explicitly set
`source-proof-proven no`; a successful native lowering is not a proof result.

Contracts, loop invariants, `assume`, `assert`, and `theorem` declarations
remain proof-layer constructs. The native backend erases proof-only constructs
after static validation and proof generation, ignores theorem declarations, and
currently skips functions containing loops. Struct values and field access are
also skipped by native lowering until layout and ABI rules are explicit.
Container values are likewise proof-layer aggregate models and have no native
layout yet. Division and modulo are deliberately not lowered yet, because Sigil
still needs an explicit source-level semantics that is known to match the native
backend for negative operands and zero divisors.

The project intentionally builds without `libgccjit`, because many development
machines and CI images do not ship it by default.

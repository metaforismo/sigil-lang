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

Generic struct values use the same field-materialization path after concrete
type arguments are substituted. A binding such as
`let box: Box[bool] = Box[bool] { value: flag };` declares `box.value` as an
SMT `Bool`, while `Box[i64]` declares the same field name pattern as `Int` in
that obligation's symbol table. Generic invariants are emitted only after this
substitution step, so theorem calls and arithmetic predicates see the concrete
field types selected by the struct literal.

Container declarations reuse the aggregate proof path, but allow model fields.
For a binding such as:

```sigil
let window: Window[i64] = Window[i64] { items: xs, index: index };
```

the scalar field creates `window.index == index`, while the slice field creates
component facts such as `window.items.len == xs.len` and
`window.items.data == xs.data`, plus the shared allocation-identity fact.
Container invariant obligations use the
`fn.name.container.local.invariant.N.label` artifact namespace, making them
distinct from ordinary struct invariant obligations while still following the
same deterministic proof flow.

Array and slice model parameters are materialized as four proof symbols:
`value.alloc`, `value.live`, `value.len`, and `value.data`. The allocation and
length symbols are `Int`, liveness is `Bool`, and data is an SMT array from
integer indices to the concrete element sort. For example, `Slice[i64]`
creates `(Array Int Int)` data, while `Array[bool]` creates `(Array Int Bool)`
data. `len(xs)` lowers to `xs.len`, `is_live(xs)` lowers to `xs.live`, and
`at(xs, i)` lowers to `(select xs.data i)`.

Every `at(container, index)` expression first emits a `memory_live` obligation
whose goal is `is_live(container)`, followed by an `index_in_bounds` obligation
at the access site. The bounds goal is:

```sigil
index >= 0 && index < len(container)
```

This mirrors division and modulo safety obligations: bounds are not assumed
silently, and callers must prove them from preconditions, invariants, branch
guards, or earlier facts. The model currently gives every array and slice a
non-negative length axiom.

`store(xs, i, value)` is modeled as an immutable array/slice update. When a
store is bound to a model local or model container field, the planner creates
component facts for the new model:

```sigil
updated.len == xs.len
updated.alloc == xs.alloc
updated.live == xs.live
updated.data == store(xs.data, i, value)
```

The write also emits `memory_live` followed by `index_in_bounds` at the store site.
Reads from the updated model then lower to `select(updated.data, index)`, so Z3
can prove standard array-theory facts such as reading the same index that was
just written. This is still not runtime mutation: the allocation token is an
abstract identity, and the liveness bit is an explicit proof fact rather than
allocation creation/destruction, ownership, slice-range, or native-memory
semantics.

Reference model parameters are materialized as `ref.addr`, `ref.valid`,
`ref.write`, `ref.value`, `ref.epoch`, `ref.alloc`, and `ref.live`. `addr(ref)` lowers to
the address symbol, `is_valid(ref)` lowers to the validity symbol,
`can_write(ref)` lowers to the write-permission symbol, `load(ref)` lowers to
the value symbol, `epoch(ref)` lowers to the epoch symbol, and
`allocation_id(ref)` lowers to the allocation symbol, and `is_live(ref)` lowers
to the liveness symbol. Every `load` emits `memory_live` followed by
`memory_valid`, whose goals are `is_live(ref)` and `is_valid(ref)` at the access
site. Function-entry references share an internal `__sigil_entry_epoch`, and
model aliases preserve the source epoch and write permission. Address
predicates are purely modeled facts today:
`same_ref(a, b)` lowers to `addr(a) == addr(b)`, and `disjoint(a, b)` lowers to
`addr(a) != addr(b)`.

All three memory proof models use the same allocation-identity surface.
`same_allocation(a, b)` lowers to `a.alloc == b.alloc`, while
`disjoint_allocation(a, b)` lowers to `a.alloc != b.alloc`. These predicates
accept any pair of `Array[T]`, `Slice[T]`, or `Ref[T]` values, even across
different element types, because the identity token is deliberately separate
from the value's typed view. Model aliases and both store forms preserve it and
the independent liveness bit.

They also share four ownership-state symbols: `.owner` (`Int`), `.has_owner`
(`Bool`), `.shared` (`Int`), and `.mut_borrow` (`Bool`). The planner adds these
invariants for every materialized memory model:

```sigil
shared_borrows(value) >= 0
!has_mut_borrow(value) || shared_borrows(value) == 0
!has_owner(value) || owner_id(value) != 0
```

Aliases and both immutable store forms copy the four symbols unchanged. This
models allocation state, not linear handle ownership: checked borrow/release
transitions update fresh snapshots, while move semantics remain separate work.

Each borrow transition emits three ordered safety obligations:

1. `memory_live` proves `is_live(source)`.
2. `ownership_present` proves `has_owner(source)`.
3. An operation guard proves `shared_borrow_available`,
   `shared_borrow_active`, `mutable_borrow_available`, or
   `mutable_borrow_active`.

The resulting snapshot preserves all non-borrow components. Shared acquire and
release use `target.shared == source.shared + 1` and `- 1`; mutable acquire and
release set `target.mut_borrow` to `true` and `false`. The registered consistency
invariants ensure successful transitions cannot create negative shared counts
or simultaneous shared and mutable borrows.

For same-type reference snapshots in one proof context, the planner also emits
deterministic alias-consistency assumptions:

```sigil
!left.valid || !right.valid ||
left.epoch != right.epoch ||
left.addr != right.addr ||
left.value == right.value
```

This is a snapshot fact, not a complete memory-state rule. It proves that two
valid `Ref[T]` values with the same modeled epoch and address load the same
modeled value, but it does not propagate a later `store(ref, value)` through
older aliases.

`store(ref, value)` is modeled as an immutable reference update. When a store is
bound to a model local or model container field, the planner creates component
facts for the new reference:

```sigil
updated.addr == ref.addr
updated.valid == ref.valid
updated.write == ref.write
updated.alloc == ref.alloc
updated.live == ref.live
updated.owner == ref.owner
updated.has_owner == ref.has_owner
updated.shared == ref.shared
updated.mut_borrow == ref.mut_borrow
updated.value == value
updated.epoch == ref.epoch + 1
```

The write emits `memory_live`, `memory_valid`, and `memory_write` safety
obligations at the store site. Loading from the updated reference then reads `updated.value`, so
the local weakest-precondition substitution can prove straight-line facts such
as `load(store(ref, value)) == value` once the store has been materialized.

The reference model is intentionally not a full memory semantics. It has no
allocation creation or destruction, lifetime transitions, borrow, field projection, byte
layout, native memory mutation, ownership, or native-code provenance model yet. The
write-permission bit is a proof fact for the current write gate, not a complete
borrowing discipline. Allocation identity alone does not imply liveness, and a
true liveness fact currently comes from contracts rather than checked
allocation/deallocation transitions. Those pieces must be added before Sigil
can claim low-level memory safety beyond explicit liveness/validity/write
obligations, epoch ordering, and same-snapshot alias consistency.

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

Use `--save-agent-requests <dir>` to write the first agentic SMT loop hook. For
each unproven obligation, Sigil writes:

- an `agent-request` artifact with the obligation, goal, assumptions, symbols,
  SMT-LIB query, acceptance gate, and the matching candidate filename;
- a `theorem-candidate` `.sigil` scaffold that is intentionally not accepted as
  a proof until an agent or human replaces the placeholder theorem and wires the
  checked lemma back into source.

The compiler never consumes these artifacts as authority. They are a durable
queue for external agents. Acceptance still means ordinary Sigil source parses,
typechecks, and proves under the deterministic local checker and Z3.

Use `sigil agent-check <candidate.sigil>` to run that acceptance gate directly
on a candidate file. The command deliberately accepts only the proof-only
candidate surface: theorem declarations, plus any struct declarations needed to
type those theorem declarations. Runtime functions are rejected so an agent
cannot smuggle executable code through a lemma workflow. Accepted still means
all obligations are acceptable under the selected checker settings; with
`--strict`, even one `UNKNOWN` result rejects the candidate.

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

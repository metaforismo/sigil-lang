# Sigil Language Notes

Sigil source files use the `.sigil` extension. The current language is small on
purpose: it exists to exercise compiler and proof infrastructure before growing
into a larger systems language.

## Modules

```sigil
module cache;
```

Every file starts with one module declaration.

Top-level struct, container, theorem, and function names share one declaration
namespace. Built-in type names (`i64`, `bool`, `void`, `Array`, `Slice`, and
`Ref`) are reserved and cannot be reused as top-level declarations.

Value names are also reserved where they would become source-level proof
symbols. Parameters, local bindings, and aggregate fields cannot be named
`result`, `i64`, `bool`, `void`, `Array`, `Slice`, or `Ref`.

## Types

The initial scalar types are:

- `i64`
- `bool`
- `void`

User-defined struct and container types can be named directly, and generic
aggregate instantiations use square-bracket type arguments:

```sigil
Box[i64]
PairBox[i64, bool]
```

Unknown type names are still preserved by the parser so future user-defined
types can be added without redesigning the AST, but the static checker rejects
unknown concrete types before proof generation.

Struct types are supported for fields and local values constructed directly
from struct literals. Function parameters and return values are scalar-only for
now, because aggregate function-boundary layout and proof semantics are not
defined yet.

## Structs And Invariants

```sigil
struct CacheLine {
  key: i64;
  value: i64;
  valid: bool;

  invariant valid_key: !valid || key >= 0;
}
```

Struct invariants are parsed and attached to the type. The current checker
validates that they are boolean predicates over declared fields, then registers
them. Struct literal construction emits one proof obligation per declared
invariant. Preservation across future field mutation remains a roadmap item
because the language does not yet expose field assignment.

Field names share the same value-name restrictions as function parameters and
local bindings, because fields become direct symbols while checking invariants.

Struct values can be constructed with named field initializers:

```sigil
let pair: Pair = Pair { left: x, ok: true };
return pair.left;
```

The checker requires every field to be initialized exactly once, rejects unknown
fields, checks each initializer against the declared field type, and checks
field access against the base struct type. The proof planner materializes scalar
fields as ordinary SMT symbols such as `pair.left`, emits invariant obligations
for the constructed value, then assumes the instantiated invariants for later
proof steps. Native lowering deliberately skips functions that manipulate struct
values until the ABI and memory layout rules are explicit.

Struct locals must be initialized directly from struct literals. Struct
assignment, struct equality, and struct-valued conditional expressions are
rejected until Sigil has explicit aggregate copy, structural equality, and
merge semantics.

Struct fields are by value. Recursive struct definitions such as
`Node { next: Node }` are rejected until the language has references or pointer
types.

## Generic Structs

```sigil
struct Box[T] {
  value: T;
}

struct PairBox[A, B] {
  left: A;
  right: B;
}
```

Generic structs declare one or more type parameters after the struct name. Each
concrete use must provide exactly that many type arguments:

```sigil
let box: Box[i64] = Box[i64] { value: x };
let pair: PairBox[i64, bool] = PairBox[i64, bool] { left: x, right: ok };
```

The checker rejects missing type arguments, extra type arguments, type
arguments on non-generic structs, and type arguments on built-in scalar types.
Type parameter names must be unique and cannot reuse built-in type names.

Generic field types are substituted at each concrete instantiation before
field initializers, field access, and invariants are checked. This means a
generic invariant can mention a field whose final type is known only when the
struct literal is constructed:

```sigil
struct NonNegativeBox[T] {
  value: T;

  invariant value_non_negative: value >= 0;
}

let box: NonNegativeBox[i64] = NonNegativeBox[i64] { value: x };
```

The `NonNegativeBox[i64]` construction is valid when `value >= 0` typechecks
over an `i64` field. A `NonNegativeBox[bool]` construction is rejected because
the instantiated invariant would compare a `bool` field with an integer.
Generic structs are a proof-instantiation foundation, not a complete container
or monomorphized runtime system yet. Native lowering still skips aggregate
values until layout and ABI rules are explicit.

## Container Declarations

```sigil
container Window[T] {
  items: Slice[T];
  index: i64;

  invariant index_non_negative: index >= 0;
  invariant index_within_items: index < len(items);
}
```

Containers are proof-level aggregate declarations for data-structure models.
They share generic type arguments, named field initializers, field access, and
invariant checking with structs, but they may also contain proof model fields:
`Array[T]`, `Slice[T]`, and `Ref[T]`. Ordinary structs still reject those model
fields until aggregate model semantics are explicit.

When a container literal is constructed, scalar fields materialize as ordinary
SMT symbols, while model fields materialize by their proof components. For
example:

```sigil
let window: Window[i64] = Window[i64] { items: xs, index: index };
assert len_visible: len(window.items) == len(xs);
```

The proof planner records facts such as `window.items.len == xs.len`,
`window.items.data == xs.data`, and `window.index == index`. It then emits one
obligation for each declared container invariant and assumes the instantiated
invariants for later proof steps.

Containers do not define runtime layout, allocation, aliasing, mutation,
ownership, or ABI passing yet. Native lowering skips functions that manipulate
container values until those semantics are designed.

## Array And Slice Models

`Array[T]` and `Slice[T]` are built-in proof model types. They can be used as
function and theorem parameters when `T` is `i64` or `bool`:

```sigil
fn read_slice(xs: Slice[i64], index: i64) -> i64
requires live: is_live(xs);
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == at(xs, index);
{
  return at(xs, index);
}
```

`len(value)` returns an `i64` length. Sigil treats model lengths as
non-negative proof facts. `at(value, index)` returns the element type `T` and
creates compile-time `memory_live` and `index_in_bounds` proof obligations:

```sigil
index >= 0 && index < len(value)
```

The SMT model uses an abstract backing array and lowers `at(xs, i)` to a
solver-level `select`.

`store(value, index, element)` is an immutable proof-level update. It requires
the source allocation to be live, owned, and under an active mutable borrow.
The operation returns the same model type as `value`, preserves the model
length and ownership state, emits an `index_in_bounds` obligation for the write
index, and lowers the backing data to solver-level `store`:

```sigil
fn write_then_read(xs: Slice[i64], index: i64, value: i64) -> i64
requires live: is_live(xs);
requires owned: has_owner(xs);
requires exclusive: has_mut_borrow(xs);
requires in_bounds: index >= 0 && index < len(xs);
ensures exact: result == value;
{
  let updated: Slice[i64] = store(xs, index, value);
  assert length_preserved: len(updated) == len(xs);
  return at(updated, index);
}
```

Model stores must currently be materialized in a `let` binding or container
field before later `len` or `at` facts use them. Plain model aliases, such as
`let alias: Slice[i64] = xs;`, are also materialized as length/data component
facts.

Every array, slice, and reference model also carries an abstract allocation
identity. `allocation_id(value)` exposes that identity as `i64`.
`same_allocation(left, right)` and `disjoint_allocation(left, right)` compare
allocation identities and may mix array, slice, and reference model arguments.
Plain aliases and immutable `store` updates preserve the source allocation
identity. This is the first common provenance fact across the three proof
models and keeps the proof IR explicit until Sigil has first-class temporary
model values.

`is_live(value)` exposes a separate Boolean liveness fact for every array,
slice, and reference model. `at`, array/slice `store`, `load`, and reference
`store` each emit a `memory_live` obligation. Stores then emit
`ownership_present` and `mutable_borrow_active` before their bounds, validity,
or write-permission obligations. Aliases and immutable stores preserve
liveness. Liveness is intentionally independent from allocation identity:
knowing an allocation token does not prove that it is live.

Every memory model also exposes allocation-level ownership and borrow state:
`owner_id(value)` is an integer owner token, `has_owner(value)` reports whether
an owner exists, `shared_borrows(value)` is the active shared-borrow count, and
`has_mut_borrow(value)` reports an active mutable borrow. Sigil assumes three
consistency invariants for every model value: shared counts are nonnegative, a
mutable borrow excludes shared borrows, and owner presence implies a nonzero
owner token. Aliases and immutable stores preserve all four facts.

Borrow state changes through immutable, model-producing transitions:

- `borrow_shared(value)` requires liveness, owner presence, and no mutable
  borrow, then increments `shared_borrows`.
- `release_shared(value)` requires liveness, owner presence, and at least one
  shared borrow, then decrements `shared_borrows`.
- `borrow_mut(value)` requires liveness, owner presence, zero shared borrows,
  and no mutable borrow, then activates `has_mut_borrow`.
- `release_mut(value)` requires liveness, owner presence, and an active mutable
  borrow, then clears `has_mut_borrow`.

Transitions preserve allocation identity, liveness, owner identity/presence,
container data/length, and reference address/validity/value/permission/epoch.
Like `store`, a transition result must be materialized in a model `let` or
container model field. These are checked proof-state snapshots, not linear
move semantics: the source model remains visible until a later language layer
defines consuming ownership and alias invalidation.

Both container and reference stores preserve an active mutable borrow in the
successor snapshot. Code may establish that state directly in a function
contract or acquire it with `borrow_mut`, perform one or more stores, and clear
it with `release_mut`. A store without owner presence or an active mutable
borrow leaves the corresponding safety obligation unresolved.

This is intentionally a proof model, not a runtime memory model. It does not
create or destroy allocations, transition an allocation between live and dead,
establish ownership, define view ranges, prove non-overlap inside one
allocation, or define native layout yet.
Aggregate returns are still rejected, and native lowering skips functions that
take array or slice model parameters.

## Reference Model

`Ref[T]` is a built-in proof model for scalar references:

```sigil
fn read_ref(ptr: Ref[i64]) -> i64
requires live: is_live(ptr);
requires valid: is_valid(ptr);
ensures exact: result == load(ptr);
{
  return load(ptr);
}
```

`is_live(ptr)` exposes the allocation-liveness bit. `is_valid(ptr)` exposes the
modeled reference-validity bit. `can_write(ptr)` exposes the modeled
write-permission bit. `load(ptr)` returns the referenced scalar value and emits
`memory_live` followed by `memory_valid`, proving both facts at the access site.
`addr(ptr)` returns the modeled integer address.
`epoch(ptr)` returns the modeled memory-snapshot token for the reference.
Function-entry references share an internal entry epoch, and model aliases
preserve the source epoch and write permission. `same_ref(left, right)` and
`disjoint(left, right)` compare modeled addresses. The common
`allocation_id`, `same_allocation`, and `disjoint_allocation` intrinsics compare
the allocation token independently of a reference's numeric address.

When two `Ref[T]` snapshots with the same element type are both valid and have
the same modeled epoch and address in one proof context, Sigil assumes their
modeled loaded values are equal. This lets ordinary contracts prove
source-level alias facts without a separate annotation language:

```sigil
fn same_ref_loads_match(left: Ref[i64], right: Ref[i64]) -> i64
requires left_live: is_live(left);
requires right_live: is_live(right);
requires left_valid: is_valid(left);
requires right_valid: is_valid(right);
requires same: same_ref(left, right);
ensures exact: result == load(right);
{
  return load(left);
}
```

`store(ptr, value)` is an immutable proof-level reference update. It returns
the same `Ref[T]` model type and emits `memory_live`, `ownership_present`,
`mutable_borrow_active`, `memory_valid`, and `memory_write` obligations in that
order. It preserves the modeled address, validity, write permission,
allocation identity, liveness, and ownership state, replaces the modeled
referenced value, and advances the modeled epoch by one:

```sigil
fn write_then_load(ptr: Ref[i64], value: i64) -> i64
requires live: is_live(ptr);
requires owned: has_owner(ptr);
requires exclusive: has_mut_borrow(ptr);
requires valid: is_valid(ptr);
requires writable: can_write(ptr);
ensures exact: result == value;
{
  let updated: Ref[i64] = store(ptr, value);
  assert still_valid: is_valid(updated);
  assert still_writable: can_write(updated);
  assert same_address: addr(updated) == addr(ptr);
  assert next_epoch: epoch(updated) == epoch(ptr) + 1;
  return load(updated);
}
```

As with array and slice stores, reference stores must currently be materialized
in a `let` binding or container field before later facts use them.

`Ref[T]` is a verification scaffold. The `can_write` bit is an additional
write-permission fact; it does not replace owner presence or the mutable-borrow
gate. Sigil does not allocate or free memory, transition lifetimes, prove
pointer provenance, propagate stores through old aliases, invalidate aliases,
mutate native memory, or define a native layout. Allocation identities are
abstract proof tokens: they do not by themselves prove that an allocation
exists, remains live, owns an address, or is disjoint from another address
range. The separate liveness bit must be established by a contract, but
currently has no allocation/deallocation transition semantics. Epochs are
proof tokens for snapshots, not a runtime memory representation. Like array
and slice models, reference values
are allowed as function and theorem parameters and as container model fields,
but not as ordinary struct fields or return values yet.

## Function Contracts

```sigil
fn keep(x: i64) -> i64
requires non_negative: x >= 0;
ensures preserved: result >= 0;
{
  return x;
}
```

`requires` predicates are added to the proof context for the function.
`ensures` predicates become postcondition proof obligations. `result` names the
returned value.

Contract predicates must be boolean. Contract labels share the function's
proof-label namespace, so they must be unique across `requires`, `ensures`, and
explicit body proof labels. `result` is available only in postconditions for
non-void functions. It is a compiler-generated contract symbol, so user
parameters, local bindings, and struct fields cannot be named `result`.

## Proof-Only Theorems

```sigil
theorem add_one_gt for (x: i64)
requires non_negative: x >= 0;
ensures advanced: x + 1 > x;
{
  return x + 1 > x;
}
```

A `theorem` is a proof-only declaration. It has scalar parameters, optional
`requires` and `ensures` contracts, and a body written with the same statement
and expression syntax as functions. The theorem body must return `bool` on
every path. The proof planner adds an implicit `holds` postcondition for every
theorem, proving that the returned boolean is `true`.

Theorem calls are expressions of type `bool`, but they are allowed only in
proof-only positions: struct invariants, function and theorem contracts, loop
invariants, `assume`, `assert`, and theorem bodies. They are rejected in runtime
function value positions such as `let`, assignment, return expressions, and
statement conditions. This keeps theorem reuse available to the prover without
asking the native backend to lower a proof-only declaration.

At a theorem call site, the caller proves the theorem's `requires` clauses.
After that, the theorem's explicit `ensures` clauses and implicit `holds` fact
become assumptions in the caller's proof context. This is the current lemma
reuse mechanism.

## Statements

```sigil
let next: i64 = size + 1;
next = next + 1;
assume cache_bounds: size <= capacity;
assert still_bounded: size <= capacity;
if size >= 0 {
  assert non_negative: size >= 0;
} else {
  assume impossible: false;
}
while next < capacity
invariant lower_bound: next >= 0;
invariant upper_bound: next <= capacity;
{
  next = next + 1;
}
return next;
```

`let` introduces a typed local binding. The binding expression can use
parameters and earlier locals, and the binding becomes a proof fact of the form
`name == expr` for later assertions and returns. Local bindings cannot shadow
parameters or earlier locals, and cannot reuse built-in type names.

`name = expr;` assigns a new value to a previously declared local binding.
Parameters are immutable, undeclared names cannot be assigned, and the assigned
expression must have the same type as the local. The prover represents each
assignment as a new internal version of the local, so facts about earlier values
remain tied to the earlier version.

`assume` extends the local proof context. `assert` creates a proof obligation
from the active context and then becomes available to later obligations.
Explicit `assume name:`, `assert name:`, and loop `invariant name:` labels must
be unique within a function and cannot reuse contract labels, because they
become user-facing proof labels. Unlabeled statements use compiler defaults and
may repeat.
`return expr;` can use parameters and local bindings. `return;` is valid only in
`void` functions. Non-void functions must return a value on every syntactic
control-flow path. For `if` statements, that means both branches must return
unless a later statement returns after the branch. Once a statement guarantees a
return, later statements in the same block are rejected as unreachable.

`if condition { ... } else { ... }` creates two statement branches. The
condition must be `bool`. The then branch is checked under `condition`, and the
else branch is checked under `!condition`. Facts that survive the merge are
guarded by the branch condition, so a fact from only one branch is not treated as
unconditionally true. Local bindings declared inside a branch are scoped to that
branch. For scalar assignments merged through `ite`, the bounded local WP rule
specializes each path and requires both selected goals to close.

`while condition` requires one or more `invariant name: predicate;` clauses
before the body. Sigil proves each invariant before entering the loop and proves
that one symbolic iteration preserves it. After the loop, the prover assumes the
invariants and the negated loop condition for the loop-exit state. Locals
declared inside the loop body are scoped to the body. Loop bodies cannot contain
`return` statements yet; early loop exits need a real control-flow and
weakest-precondition model before Sigil can prove them honestly. Conjunctive
postconditions can be composed from separate loop-exit invariant summaries;
initialization and preservation remain independent obligations.

Function postconditions can mention parameters and `result`, but not body-local
binding names. This keeps contracts independent from implementation-local
details.

Function calls are expressions: `add_one(x)` or `sum(a, b)`. The static
validator resolves the callee across the module, checks arity and argument
types, rejects `void` calls in value position, and rejects direct or indirect
recursive call graphs until the proof planner has a recursive-function model.
Callers prove the callee's `requires` clauses at each call site and may use the
callee's `ensures` clauses as facts about the call result. Theorem calls use the
same modular proof model, but only in proof-only contexts.

The current body language is deliberately tiny. Aggregate mutation, early loop
exit, and runtime memory operations still require richer control-flow and
weakest-precondition generation.

## Expressions

Supported expression forms:

- integer literals: `0`, `42`
- booleans: `true`, `false`
- identifiers: `x`, `result`
- unary operators: `!x`, `-x`
- arithmetic: `+`, `-`, `*`, `/`, `%`
- comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- boolean connectives: `&&`, `||`
- conditionals: `if condition { then_expr } else { else_expr }`
- function calls: `callee(arg1, arg2)`
- theorem calls in proof-only contexts: `lemma(arg1, arg2)`
- model intrinsics: `len(xs)`, `at(xs, index)`,
  `store(xs, index, value)`
- reference intrinsics: `is_valid(ptr)`, `can_write(ptr)`, `load(ptr)`,
  `addr(ptr)`, `store(ptr, value)`, `same_ref(left, right)`,
  `disjoint(left, right)`
- cross-model allocation intrinsics: `allocation_id(value)`,
  `is_live(value)`, `same_allocation(left, right)`,
  `disjoint_allocation(left, right)`
- ownership intrinsics: `owner_id(value)`, `has_owner(value)`,
  `shared_borrows(value)`, `has_mut_borrow(value)`
- borrow transitions: `borrow_shared(value)`, `release_shared(value)`,
  `borrow_mut(value)`, `release_mut(value)`
- aggregate literals: `TypeName { field: value }` and
  `TypeName[i64, bool] { field: value }`
- field access: `value.field`
- parentheses

Conditional expressions require a `bool` condition, and both branches must have
the same non-void type. They are expressions, not statement blocks: each branch
contains one expression.

Every division or modulo expression creates a compile-time proof obligation
that the divisor is nonzero. The obligation is checked in the expression's
active control-flow context, so a divisor used only inside one conditional
branch can rely on that branch condition. Boolean `&&` and `||` also act as
short-circuit guards for these safety checks: the right side of `a && b` is
checked under `a`, and the right side of `a || b` is checked under `!a`.

These expressions are also the proof language. There is no separate annotation
language.

Integer literal tokens are decimal and must fit in the non-negative `i64` range
`0..9223372036854775807`. A leading `-` is parsed as unary negation, not as part
of the literal token.

The native GCC JIT backend lowers the scalar subset that has a clear source to
native mapping today. It supports `+`, `-`, `*`, comparisons, equality, boolean
operators, conditionals, scalar function calls, locals, assignment, valued
returns, and `void` functions with explicit `return;` or fallthrough. Loops,
division, and modulo remain proof-language constructs until their exact runtime
semantics are pinned down for native lowering.

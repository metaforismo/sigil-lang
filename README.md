# Sigil

Sigil is an experimental systems language and compiler for code that carries its
own proof obligations.

The compiler frontend parses ordinary language declarations plus `requires`,
`ensures`, `assert`, `assume`, and `invariant` clauses written in the same
expression syntax as the program. It emits verification conditions as SMT-LIB,
can ask Z3 to discharge them at compile time, and includes a conditional
`libgccjit` backend probe for native lowering work.

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
- Assignment to previously declared locals, lowered through versioned proof
  symbols so old and new values stay distinct.
- Expression-level `if condition { then } else { else }` conditionals that lower
  to SMT `ite`.
- Statement-level `if`/`else` branches with branch-local proof contexts and
  guarded facts at merge points.
- Static validation for predicate types, identifier scope, duplicate symbols,
  and return types.
- Verification-condition generation for function assertions and postconditions.
- SMT-LIB emission with optional Z3 execution through `z3` or `SIGIL_Z3`.
- CMake detection for `libgccjit`; builds without it and reports backend status.
- CI that exercises the portable compiler core and a solver-backed Z3 smoke
  path.

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
./build/sigil check examples/refuted.sigil --strict --show-model
```

Check whether the native backend was compiled with `libgccjit`:

```sh
./build/sigil backend
```

`sigil check` also runs static validation before building proof obligations:
contract expressions must be boolean, identifiers must be declared, return
expressions must match the function return type, and unsupported value types are
rejected before SMT is emitted.

Full command details are in [docs/CLI.md](docs/CLI.md).

## Language Shape

Sigil intentionally keeps the proof surface close to the program surface:

- `invariant` attaches a predicate to a struct.
- `requires` and `ensures` attach contracts to a function.
- `assume` introduces a fact into the current proof context.
- `assert` creates a proof obligation.
- `result` names the returned value in postconditions.

More detail is in [docs/LANGUAGE.md](docs/LANGUAGE.md).

## Architecture

```text
.sigil source
  -> lexer/parser
  -> typed module model
  -> verification-condition planner
  -> SMT-LIB emitter
  -> local syntactic prover or Z3
  -> GCC JIT backend work
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/PROVER.md](docs/PROVER.md). CLI behavior is documented in
[docs/CLI.md](docs/CLI.md).

## Roadmap

The next hard pieces are:

- type checking beyond the current scalar proof-expression validator;
- weakest-precondition generation for real control flow;
- preservation checks for struct invariants across constructors and mutators;
- a proof-assistant loop where LLMs propose lemmas and Z3 validates them;
- real `libgccjit` lowering for a useful subset of functions;
- binary-level proof experiments for bounded runtime and crash-safety claims.

The roadmap is tracked in [docs/ROADMAP.md](docs/ROADMAP.md).

## Contributing

This is early-stage compiler research code. Small, test-backed changes are
preferred. See [CONTRIBUTING.md](CONTRIBUTING.md) and
[SECURITY.md](SECURITY.md).

## License

Apache-2.0. See [LICENSE](LICENSE).

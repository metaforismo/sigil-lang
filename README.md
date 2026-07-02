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
- Assignment to previously declared locals, lowered through versioned proof
  symbols so old and new values stay distinct.
- Expression-level `if condition { then } else { else }` conditionals that lower
  to SMT `ite`.
- Statement-level `if`/`else` branches with branch-local proof contexts and
  guarded facts at merge points.
- `while` loops with user-written invariants, initialization and preservation
  proof obligations, and loop-exit facts.
- Static validation for predicate types, identifier scope, duplicate symbols,
  reserved contract names, return types, non-void return coverage, and
  unreachable statements after guaranteed returns.
- Verification-condition generation for function assertions and return-path
  postconditions.
- Branch- and short-circuit-aware arithmetic safety obligations for division and
  modulo divisors.
- SMT-LIB emission with optional Z3 execution through `z3` or `SIGIL_Z3`.
- Source-level counterexample rendering for refuted Z3 models.
- CMake detection for `libgccjit`; builds without it and reports backend status.
- Native lowering for pure `i64`/`bool` functions using `let`, assignment,
  conditionals, arithmetic `+`/`-`/`*`, comparisons, boolean operators, and
  returns.
- ABI smoke tests that invoke JIT-compiled scalar functions and check returned
  `i64`/`bool` values.
- Native-lowering diagnostics that report source ranges for unsupported
  constructs.
- Native IR artifacts that list signatures, contracts, body operations, and
  lowering status for comparison with SMT artifacts.
- GCCJIT debug-info requests plus deterministic source-to-native debug location
  maps in saved native artifacts.
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

Run a native-lowered scalar function through the JIT ABI:

```sh
./build/sigil run examples/native.sigil add_one 41
```

`sigil check` also runs static validation before building proof obligations:
contract expressions must be boolean, identifiers must be declared, `result`
cannot be reused as a parameter or local binding, return expressions must match
the function return type, unreachable statements after guaranteed returns are
rejected, and unsupported value types are rejected before SMT is emitted.

Full command details are in [docs/CLI.md](docs/CLI.md).

## Language Shape

Sigil intentionally keeps the proof surface close to the program surface:

- `invariant` attaches a predicate to a struct.
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

The next hard pieces are:

- type checking beyond the current scalar proof-expression validator;
- weakest-precondition generation for real control flow;
- preservation checks for struct invariants across constructors and mutators;
- a proof-assistant loop where LLMs propose lemmas and Z3 validates them;
- binary-level proof experiments for bounded runtime and crash-safety claims.

The roadmap is tracked in [docs/ROADMAP.md](docs/ROADMAP.md).

## Contributing

This is early-stage compiler research code. Small, test-backed changes are
preferred. See [CONTRIBUTING.md](CONTRIBUTING.md) and
[SECURITY.md](SECURITY.md).

## License

Apache-2.0. See [LICENSE](LICENSE).

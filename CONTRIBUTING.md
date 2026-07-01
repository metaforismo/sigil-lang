# Contributing

Sigil is intentionally small at the moment. Contributions should make the
compiler more precise, more testable, or more honest about what it proves.

## Development Setup

```sh
cmake -S . -B build -DSIGIL_ENABLE_GCCJIT=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Use `-DSIGIL_WARNINGS_AS_ERRORS=ON` before submitting larger C++ changes.

If you have `libgccjit` installed, configure without disabling it:

```sh
cmake -S . -B build-gccjit
cmake --build build-gccjit
./build-gccjit/sigil backend
./build-gccjit/sigil compile examples/native.sigil
./build-gccjit/sigil run examples/native.sigil add_one 41
```

If you have Z3 installed, run at least one strict solver-backed smoke check:

```sh
./build/sigil check examples/arithmetic.sigil --strict
```

## Project Rules

- Do not mark an obligation as proved unless the checker can reproduce the
  proof.
- Keep LLM output advisory; machine validation must be done by the verifier.
- Add tests for parser, proof planning, and emitted SMT when changing language
  syntax.
- Prefer narrow, reviewable changes over broad rewrites.
- Keep public claims conservative. Unknown proof results are a valid outcome.

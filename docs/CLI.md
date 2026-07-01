# CLI Reference

## `sigil check`

```sh
sigil check <file.sigil> [--dump-smt] [--save-smt <dir>] [--show-model]
                  [--solver-timeout-ms <ms>] [--strict] [--no-z3]
```

`check` parses a Sigil module, validates scalar expression types, builds proof
obligations, emits SMT-LIB, and optionally asks Z3 to discharge obligations that
the local checker cannot prove. Each proof result includes the source range of
the `assert` or `ensures` clause that produced it. Single-line ranges are
printed as `file:line:start-end`.

Options:

- `--dump-smt`: print each emitted SMT-LIB query to stdout.
- `--save-smt <dir>`: write every emitted SMT-LIB query to `<dir>`.
- `--show-model`: when Z3 refutes an obligation, ask Z3 for a model and print it
  under the refuted result.
- `--solver-timeout-ms <ms>`: emit an SMT timeout option for each query. The
  timeout is preserved in `--dump-smt` output and `--save-smt` artifacts.
- `--strict`: exit non-zero if any obligation is `UNKNOWN`.
- `--no-z3`: skip Z3 and run local checks only.

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

## `sigil backend`

```sh
sigil backend
```

Reports whether the current binary was built with `libgccjit` support and can
allocate a GCC JIT context.

## `sigil compile`

```sh
sigil compile <file.sigil>
```

Parses and validates a module, then asks the GCC JIT backend to lower every
native-supported function into an in-memory JIT result. The command reports each
function as `lowered` or `skipped`.

The current native subset supports pure scalar functions over `i64` and `bool`
using:

- parameters and local `let` bindings;
- assignment to declared locals;
- statement and expression conditionals;
- unary `!` and `-`;
- arithmetic `+`, `-`, and `*`;
- comparisons, equality, `&&`, and `||`;
- proof-only `assume` and `assert` statements, which are erased before native
  lowering.

Division and modulo are intentionally skipped until Sigil pins down the exact
source semantics and proves they match the native lowering.

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
are parsed from the function signature: `i64` parameters accept integer strings,
and `bool` parameters accept `true`, `false`, `1`, or `0`.

Example:

```sh
sigil run examples/native.sigil add_one 41
```

The command currently supports `i64` and `bool` return values and up to two
scalar parameters. That is enough to exercise the first ABI contract without
pretending Sigil has a complete FFI or runtime yet.

Exit codes:

- `0`: the function was compiled, found in the JIT result, invoked, and returned
  a scalar value.
- `1`: command-line, parse, validation, argument-conversion, or file-read error.
- `2`: `libgccjit` was available, but lowering, JIT compilation, symbol lookup,
  or invocation failed.
- `3`: the binary was built without a usable `libgccjit` backend.

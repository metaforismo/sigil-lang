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

The proof set also includes safety obligations such as
`fn.name.safety.N.divisor_nonzero` for division and modulo expressions. Those
obligations point at the divisor expression that must be proven nonzero. Guards
from `if`, `&&`, and `||` are reflected in the assumptions for the safety query.
If options are provided without `<file.sigil>`, `check` reports the missing
source path directly.

Options:

- `--dump-smt`: print each emitted SMT-LIB query to stdout.
- `--save-smt <dir>`: write every emitted SMT-LIB query to `<dir>`.
- `--show-model`: when Z3 refutes an obligation, ask Z3 for a model, render the
  scalar values as a Sigil source-level `counterexample`, and keep the raw Z3
  model under the refuted result.
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

## `sigil compile`

```sh
sigil compile <file.sigil> [--dump-native-ir] [--save-native-ir <dir>]
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

Functions containing `while` loops are checked by `sigil check`, but are
skipped by native lowering for now. Division and modulo are intentionally
skipped until Sigil pins down the exact source semantics and proves they match
the native lowering.

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

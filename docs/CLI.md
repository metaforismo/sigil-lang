# CLI Reference

## `sigil check`

```sh
sigil check <file.sigil> [--dump-smt] [--save-smt <dir>] [--show-model]
                  [--solver-timeout-ms <ms>] [--strict] [--no-z3]
```

`check` parses a Sigil module, validates scalar expression types, builds proof
obligations, emits SMT-LIB, and optionally asks Z3 to discharge obligations that
the local checker cannot prove.

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
- `3`: `sigil backend` was run and the binary was built without a usable
  `libgccjit` backend.

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

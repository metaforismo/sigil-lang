# Security Policy

Sigil is experimental compiler and verifier infrastructure. Do not use it yet as
the only safety boundary for production systems.

## Supported Versions

Only the `main` branch is supported while the project is pre-release.

## Reporting a Vulnerability

Please open a private security advisory on GitHub if the issue involves:

- verifier unsoundness that reports invalid code as proved;
- command execution or file-write behavior reachable through source input;
- unsafe handling of solver or future LLM provider output;
- build-system behavior that unexpectedly executes untrusted code.

For ordinary parser bugs, crashes on non-sensitive input, or documentation
issues, open a public issue.

## Security Model

The current compiler treats Z3 as the validating authority for SMT obligations.
Future LLM-assisted proof search must remain advisory: any lemma, rewrite, or
proof hint proposed by an LLM has to be reduced to a checkable artifact before it
can affect a compile result.

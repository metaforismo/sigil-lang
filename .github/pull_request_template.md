## Summary

## Verification

- [ ] `cmake -S . -B build -DSIGIL_ENABLE_GCCJIT=OFF`
- [ ] `cmake --build build`
- [ ] `ctest --test-dir build --output-on-failure`

## Proof-Soundness Notes

Describe any verifier behavior changed by this PR. If an obligation can now be
reported as proven, explain what deterministic checker validates it.

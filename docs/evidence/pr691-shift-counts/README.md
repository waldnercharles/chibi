# PR691 exact shift-count correction

This is a preserved-source development checkpoint on PR691's requested head
`17b7c4d0d491cbd50dcc64e8268d3efef8980271`, stacked on PR687
`ab8c4a58f6cabc8b74e9d94c37fde41cb634c2ac`. The source/test correction is
`1a51623ad2964c779f3bcbbb21672f550426a8e5`, tree
`8b387e254c34e7d1b75ab03653dd6803424d7adf`.

The correction removes early count folding, clamping and AST replacement.
Every untyped count gets its independent conversion-operand type; its expression
survives exact validation and folding. Runtime lowering still performs its usual
count conversion. Negative exact shifts now return an explicit fold error that
produces a diagnostic, including a negative count below int64. The final folding
assertion remains. Exact intermediate arithmetic, final representability,
no mixed integer operands, unsigned runtime counts, invalid constant/zero-divisor/
unsigned-negation rejection and wrap-only conversion labels remain in force.

## Reproduced controls

The C pico_unit selector `--integer-shift-counts [case]` uses the existing
production source-package preparation, lowering, prepared serialization/loading
and invocation path. Accepted results are literal JSON, not compiler-calculated
expected expressions. Each execution verifies actual backend entries, no other
backend entries, no fallback and no counter overflow. Both bytecode and SLJIT
execute on this macOS arm64 host. Negative cases pin the intended diagnostic and
source location.

| Case | Original 17b7c4d0 with final fixtures | Fixed |
| --- | --- | --- |
| `(int8(1) << 200) >> 199` | Both backends return 1, expected literal 2 | 2 |
| `(1 << 200) >> 199` returning int8 | Wrong literal-range rejection | 2 |
| Dynamic `a << ((9223372036854775807 + 1) - 9223372036854775807)`, a=1 | Orphan intermediate wrongly range-rejected | 2 |
| `int8(1) << (0 - 9223372036854775807 - 2)` | Assertion at semantic_constant.c:166; exit134 | Negative-count diagnostic at2:38 |
| `int8(0) << 4097` | Wrongly accepted | Count-too-large diagnostic at2:37 |
| `int8(1) >> 4097` | Wrongly accepted | Count-too-large diagnostic at2:37 |
| Zero-left shift4096 plus right shift4096 | 0 | 0 |
| Dynamic a=1 with constant200, unsigned parameter200, constant5000 | 0 in each | 0 in each |
| Dynamic-left below-int64 negative count | Existing int64 range rejection at2:39 | Same rejection |

The below-int64 assertion and orphan-subtree hypotheses are therefore reproduced
bugs at the requested original head, not merely source inferences. Zero-left
4097 independently tests the count limit without triggering a magnitude limit.
Dynamic5000 remains a runtime shift, outside the exact constant-shift limit.

`before-*.log` preserves the first execution outputs; `baseline-final-*.log`
preserves a second original-head run with the final fixture source and backend
counter checks. The original dynamic-negative fixture initially requested the
more specific negative diagnostic; inspection showed its existing range
rejection is correct and sufficient, so the final fixture preserves it. Three
initial fixed-run fixture columns were corrected to the actual intended count
nodes. These fixture corrections did not change any value or rejection policy.
The final baseline has six failures (including the abort) and three passes.
`final-fixtures.patch` reproduces that test-only change on17b7c4d0.

## Build, review and clean-candidate checks

Normal standalone CMake Debug, Homebrew Clang22.1.8, assertions enabled, two
build jobs. The existing pinned dependency archive was read from a shared cache;
each checkout used its own build directory. No SDK options or seed inputs changed.

```sh
cmake -S test/language -B build-pr691-debug \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCHIBI_THIRD_PARTY_CACHE="$PINNED_DEPENDENCY_CACHE"
cmake --build build-pr691-debug --target test_language --parallel 2
CHIBI_LANGUAGE_BUILD_DIR=build-pr691-debug python3 tools/test_policy.py tool language-integer-shift-counts
CHIBI_LANGUAGE_BUILD_DIR=build-pr691-debug python3 tools/test_policy.py tool language-semantic
CHIBI_LANGUAGE_BUILD_DIR=build-pr691-debug python3 tools/test_policy.py tool language-numeric-exact-core
CHIBI_LANGUAGE_BUILD_DIR=build-pr691-debug python3 tools/test_policy.py tool language-integer-semantics
```

The pinned release dependency digest is
`602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b`.
Unlike the newer integration base, this preserved PR691 SDK has no authored
asset-generation dependency. Its checked prepared21 seed matches its manifest
sources:13390570 bytes, SHA256
`58d17229e51162cc713a09ff8330911b6d3f210ff9b12e91619ed9da7af77c06`;
bootstrap.json SHA256
`389d01225837b69fc874ba82be0dd4f6a831ed0ff3329147b1d99e5e54f7523a`.
No recovery or generation was needed. The initial sandbox could not expose `ps`
to the supervisor, so no tests started; normal escalation then enabled its
required process cleanup. No supervisor bypass was used.

Independent read-only agents inspected the native build route and the full final
source/test diff. The source reviewer found no remaining correctness blocker
and judged these focused checks sufficient for this bounded checkpoint. Review
specifically prompted the contextual-count fixture and explicit backend counters.
Review was independent; execution was performed by the implementation owner.

All selected commands exited0 on clean source commit1a51623ad:

| Selector | Cases passed | Assertions |
| --- | ---: | ---: |
| integer-shift-counts | 9 | 46 |
| semantic | 65 | 867 |
| numeric-exact-core | 22 | 567 |
| integer-semantics | 1 aggregate | 325 |

The existing integer-semantics selector separately checks its recorded cases
against the checked authored compiler. The new shift selector intentionally
checks stage zero only; it does not establish authored-compiler parity.
Changed C files pass clang-format22 dry-run; `git diff --check` passes.
`final.json` binds the clean source commit/tree to commands and exit codes.
Logs separate test wall time from configuration/build time. Published logs
replace local checkout paths with `$CHECKOUT`/`$BASELINE` and strip trailing
whitespace; original output hashes
and normalized hashes are recorded in `receipts.json`. Original logs and both
owned build directories remain in the task workspace for integration handoff.

## Integration handoff and limitations

The integration owner should apply the bounded source/test commit to its chosen
combined source, preserving existing float/named/array fixtures and Graph source
inventory. This checkpoint changes no compiler Chibi source, seed, runtime pin,
release, PR readiness or integration checkout. No duplicate PR was opened.
The four production files have a net reduction; the remaining changes are
regression dispatch, tests and this evidence.

Remote inspection during handoff found PR691 had independently advanced to
`738441a42b80d68b30f700701edc918e9747218a` ("Fold constant shifts with their
written count"). This branch remains pinned to17b7c4d0 and has not modified
PR691. Source inspection of738441a4 shows it skips clamping only for a constant
left operand, retaining the dynamic-left replacement and contextual branch;
it does not add the negative-fold diagnostic. That newer head was not executed
here. Reconcile it deliberately rather than blindly applying both repairs.

GitHub returned no PR-triggered Actions for17b7c4d0. The original PR body reports
ungrouped failures and Graph compile-command parity discrepancies; the passing
selected checks above do not resolve those limitations. The independent
`bounded_compiler/Expression.chibi:36` int64+int dependency still needs explicit
`int64(digit)` in the integration work. It was not changed here.

The five broad Make groups, whole-tree format, Graph parity, current8d5
integration, authored-source regeneration, sanitizers, Wasm, Linux LLVM native,
Windows and GPU coverage were not run. This is a reviewed development checkpoint,
not a merge/release/readiness or full-parity claim.

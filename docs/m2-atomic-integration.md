# Atomic M2 integration checkpoint

Owner branch: `codex/m2-atomic-integration-20261024`, separate worktree `chibi-m2-integration`.

## Frozen inputs and composition

- Refreshed accepted main: `220dc2170dce1a69091fc5c06313ec2dc27fcdd5`, including #684, #685 and #678.
- Published M2: `ab8c4a58f6cabc8b74e9d94c37fde41cb634c2ac`, already containing runtime #683 and the reviewed array receiver-watch source. Neither is duplicated.
- Floating handoff: `e436d48bfd4a016070ef439e637ca1412648dd46`, its three-file M2 patch and C/pico public fixtures. This is scalar admission/conversion, not complete typed-constant parity.
- Native-name fixture+implementation source: `1108e4310dc862bf536bc860eb1dfa7e1a055f0b`, retaining the preserved `f57675ec2` baseline evidence separately.

The merge preview found four textual conflicts: compiler_host.c/.h, test/language/main.c, and package_compiler_chibi.c. Host conflicts retain current-main load-values, target/profile and storage accounting APIs and add M2 root inspection. Main's compiler_host archive, private source/process/dl includes, SCALAR_CASES embedding and source-only asset generation stay intact.

C scalar tests retain explicit Cases.ExpectedValue and FOLDED_SOURCES.entry expectations through module_agrees/case_calls, four-way literal comparisons, the lazy-arm observation before freeing IR, and PIC only for observed wrappers. Preserve all five exact-fold cases, overflow rejection, ordinary Folds/FoldConstant reuse, M2's 160-probe grid and existing finite deadlines. No alternate scalar evaluator is introduced.

The native-name delta associates already evaluated values with ports before the floating widening loop. Float conversions consume those port-associated arguments. Address-slot base b stays after the widening temporaries. Typed integer conversion branches remain M2's.

## Typed floating constant design (reported before long builds)

The ordinary semantic owner is Compile.chibi. Extend its declaration admission and FoldConstant handling for typed floating literals, already declared constants, parentheses, unary signs and scalar conversion nodes. Recursive constant dependencies still go through FoldConstant and its existing Waiting/Failed/Folded outcomes; integer Evaluate/exact arithmetic remains unchanged. Existing Steps.chibi constant-reference emission already supports storage plus raw bits.

Use Number.chibi's literal parser and FloatFromDouble ties-to-even logic, with narrow bit-format conversion support for widening/narrowing, signs, infinity and NaN classification. Do not create a second AST evaluator or evaluate constants specially in Scalar.chibi. Keep floating declarations out of integer-only ConstantExpression/Folds admission; named-floating composites must not enter ExactStored/integer Evaluate. Their ordinary runtime expression path remains available. No new mixed floating expression, extended-precision or binary-floating constant arithmetic policy is selected.

Tests must retain the visible positive constant-round-even case and both typed-constant narrowing negatives, with exact diagnostic categories and locations when implemented. Add literal conversion boundary and named-constant composition regressions and retain the existing mutation receipts. Any genuinely unspecified rule is an isolated parent question, not an invented conversion rule.

## Holds and validation

This source composition targets prepared21/protocol9 while the checked bootstrap and runtime pin remain main's existing artifacts. Do not relabel the format20 artifact or copy M2's now-stale generated artifact into this combined source tree. Scratch generation is permitted only after local source freeze; publishing/replacing checked artifacts requires parent review and authorization. Source checks do not establish recovery or release readiness.

PR688 at `761ec5848631269890e93ad88834dffcd3a159e2` is not adopted. If a real load/invoke/recovery test encounters capacity, preserve exact before/after candidates and treat that dependency as a separate logical step. No quota increase or unrelated historical ABI branch import is allowed.

Builds use existing dependencies and at most two jobs; tests use normal supervision without warning/sanitizer suppression. Mac cannot execute the Linux x86-64 native harness; its exact-head native gates remain required. Combined asset-host/scoped-runtime, bootstrap/fixedpoint/recovery and final root review remain separate gates. No release, merge-to-main or author-branch update is performed here.

## Initial source-build result

Default standalone language build reached asset generation and failed with `Prepared module core:strings: unknown prepared format version; rebuild prepared code`: current runtime21 cannot load the deliberately preserved checked format20 compiler. This is an artifact compatibility gate, not a PR688 capacity failure. No artifact was replaced or relabeled.

A default-on `CHIBI_SDK_BUILD_ASSET_COMPILER_HOST` option permits a source/bootstrap-test configuration to omit that circular generation dependency. This worktree uses `cmake -S test/language -B build-language -DCHIBI_SDK_BUILD_ASSET_COMPILER_HOST=OFF`, then normal `make language-build JOBS=2`; configuration/build passed in 1.4s/8.0s after reusable dependencies were built. Default behavior, archive ownership and source-only asset generation are unchanged; the asset-host gate is explicitly unrun/pending here and must be restored for full validation after compatible scratch generation.

Changed C fixtures compile with warnings as errors. Imported floating fixtures needed explicit designated `constant` initializers; no warning suppression was added. Their failure loop now releases host/result after each invocation. A syntax-only x86_64 Mac-header compilation with the native test guard enabled passes, but this is not Linux execution or a Linux build claim. Typed-floating constant implementation has not started at this source-composition checkpoint.

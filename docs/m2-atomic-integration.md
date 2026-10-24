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

## Corrective source checkpoint after parent review

The asset-host option above was NEW in 2920296, in `sdk/CMakeLists.txt` only.
It was not a pre-existing supported acceptance configuration. This corrective
checkpoint removes both its declaration and gate, restoring that file exactly
to main220dc. The OFF build and test logs remain historical diagnostic evidence;
they do not establish full integration acceptance. The cached option is removed
with `cmake -S test/language -B build-language -U CHIBI_SDK_BUILD_ASSET_COMPILER_HOST`.
Existing `chibi-build-host`, `chibi-compiler-host`, `semantic.c.o` and
`package_compiler_chibi.c.o` targets build with two jobs and warnings enabled.
No replacement production switch or Graph/CMake asymmetry is introduced.

The uncommitted typed-constant implementation is now removed from the active
source tree and retained locally, unapplied, for review:
`.chibi/m2-integration/typed-float-review.patch`, SHA-256
`3a528233425246e2c6ab3d252596f5d3eaacbec7f7a9cfbfd15e92dc2d1d214c`.
It applies to 2920296 and contains exactly Compile.chibi (137 changed lines)
and Number.chibi (44 additions), 178 insertions/3 deletions total. Full local
source snapshots are retained alongside it. It has not been published or
expanded after the review hold. No generation has been run.

The exact design is the ordinary FoldConstant extension described above:
primitive floating declaration admission; waiting/private named constants;
contextual literals; unary sign; explicit scalar conversion; existing integer
FoldConstant before integer-to-floating conversion. ConstantExpression remains
integer-only. Number.FloatingBits converts represented bits and delegates finite
normal double narrowing to existing FloatFromDouble; it handles zero, overflow,
infinity/NaN classification and float-subnormal widening separately. No new
constant AST walker, binary-floating evaluator, Scalar special case or ABI is
introduced. The latest integer-operand range correction in this patch has not
been executed. This remains a proposal, not accepted source.

### Literal fixture contract

`float_conversion_cases.h` retains the original `[16777216.0]` positive oracle,
all prior fixtures, and adds these independent fixed expectations:

| Source boundary | Fixed expected result |
| --- | --- |
| double16777219 narrowed to float | 16777220.0 (upper even neighbour) |
| double-16777217 narrowed to float | -16777216.0 |
| forward reference from double to a float1.5 constant | 1.5 |
| named float1.5 in ordinary body addition | 3.0 |
| explicit double1e300 narrowing | positive infinity classification |
| unary negative of named float zero, then widening | negative-zero classification |
| exact decimal2^-149 stored as float, then widened | exact double2^-149 |
| exact decimal2^-150 explicitly narrowed | positive zero (even tie) |

These cases make no floating binary-expression constant-initializer claim.
Infinity/zero predicates run in ordinary function bodies. Existing C-fed packed
conversion tests separately retain fixed hexadecimal/subnormal/boundary values,
both signed infinities and NaN classification. No expected result is computed
by a production cast or by either compiler.

The two constant narrowing negatives now require their actual intended semantic
boundary. A named double assigned to a float constant must reach the ordinary
constant conversion error at the reference `Wide`, line3 column17, using the
existing `a constant of another type needs a conversion` category. Returning a
named double from a float function must reach existing return-type checking at
line3 column40, `returned value differs from the result type`. Stage zero still
pins its independent incompatible-type diagnostic at3:17/3:47. These categories
follow the approved explicit-narrowing requirement and existing semantic owners;
unsupported-type, unrelated-error or acceptance results cannot satisfy them.

The original wrong-production controls remain preserved unchanged at float
handoff e436d48b, under `docs/evidence/float-conversion/`: broad admission,
zero-instead-of-widening emission and unrelated-error mutations. They are
historical receipts, not new-head mutation runs. New fixture execution and fresh
mutations are pending a compatible, reviewed scratch artifact and test driver.
Only fixture object compilation is claimed for this corrective checkpoint.

### Exact artifact identity, not substitution

Both artifacts use outer `CHIBICLO` version1 and contain12 modules. Their inner
prepared headers are unsigned LEB128 encoded; the prepared version is not the
outer closure version or bootstrap.json's version5. Every module was decoded
read-only using the on-disk row lengths and header encoding. Full offsets,
headers and identities are in [artifact-identities.json](evidence/m2-integration/artifact-identities.json).

| Git snapshot, same path `tools/compiler/bootstrap/compiler.chibip` | Bytes | SHA-256 | Inner prepared version |
| --- | ---: | --- | ---: |
| main220dc, merge2920296 and current worktree | 12644080 | d2fd2b2d9aacd7add71b4c70ce04a74ca72159a17e3220b1102020b8bd3414f7 | 20 |
| M2ab8c4a58 | 13390570 | 58d17229e51162cc713a09ff8330911b6d3f210ff9b12e91619ed9da7af77c06 | 21 |

For core:strings at byte68, the first four encoded header fields are
`c5a4c1caa4a892a4431408a0d182d6f1b4db8f9701` on main and
`c5a4c1caa4a892a4431508a0d182d6f1b4db8f9701` on M2. They decode to
magic0x4348494249505245, version20/21, pointer width8 and
ABI0x971f6da71ac0a8a0. Equal ABI fields do not make different prepared schemas
compatible. Current runtime source requires prepared21.

The normal merge would have carried M2's changed artifact. Before committing
2920296, I explicitly restored both `compiler.chibip` and `bootstrap.json` from
main to honor the artifact-publication hold. Thus 2920296 preserves main's bytes;
it is not an untouched automatic merge result. There has been no hash relabel,
new artifact substitution or claim that the preserved seed matches M2 source.

[Before implementation](evidence/m2-integration/typed-constant-before.txt) records
ordinary conversion tests passing and typed constants red as unsupported. The
[initial local probe](evidence/m2-integration/typed-constant-initial-probe.txt)
accepted the original rounding positive but remained red because negative
categories were then pending. Both used the earlier diagnostic-only asset-OFF
configuration; neither represents this corrected candidate or final acceptance.
The source patch's latest integer-range correction and new fixtures remain
unexecuted. PR688, including d9032490, remains separate and unapplied. The known
blocker is version compatibility, with no capacity-failure evidence.

### Review findings on the held local patch

A fresh read-only reviewer found two blockers. First, existing
EvaluateConversion calls FoldConstant then ExactStored; permitting floating
FoldConstant results would let `uint64(Wide)` reinterpret the bits of
`Wide: double : 1.5` as4609434218613702656. Integer constant conversion needs
an explicit floating-operand rejection before that path, plus ordinary/wrap
negative controls. This is not permission to implement float-to-integer folding.
Second, conversion-operand inference does not recursively peel unary signs and
parentheses, so `double(-(1.5))` incorrectly selects an integer fold. The existing
inference helper needs a bounded correction with nested-sign/parenthesis cases.
Neither correction has been made while the proposal is held.

Reviewer also requests constant negative overflow, near-maximum rounding into
infinity, and a separately authored-only unsupported binary-initializer control.
The shared stagezero/stageone table cannot assume equal acceptance for forms
that stagezero supports but this bounded authored compiler deliberately does
not yet implement. These remain review tasks, not silently broadened semantics.

After removal of the option, normal `make language-build JOBS=2` was rerun and
exited2 at the required authored asset generation, with the same precise
`Prepared module core:strings: unknown prepared format version; rebuild prepared code`
diagnostic. This confirms the normal gate is restored. It is a known version
block, not denial or uncertain execution, and no capacity workaround was tried.
A compatible scratch artifact, full driver execution and recovery remain
pending parent source review and explicit generation authorization.

## Reviewed typed-constant repair after 7977e09e

Parent authorized repair of the two blockers, without broader floating arithmetic
or integer folding changes. The revised proposal is now applied to the source
checkpoint after independent review; the original failed proposal and both
historical test logs above remain preserved unchanged.

Only two production files change relative to7977e09e: Compile.chibi and
Number.chibi. The exact two-file patch is retained locally at
`.chibi/m2-integration/typed-float-revised.patch`, SHA-256
`ddafca8f17b14a08d2dc5f276e19d39676accb2c22351a941f490df814842136`.
It is reproducible with `git diff 7977e09e <this-source-commit> --
tools/compiler/package_compiler/Compile.chibi tools/compiler/package_compiler/Number.chibi`.
It has197 insertions and10 deletions. Source composition still uses the inputs
and explicit artifact hold documented above.

The repaired integer conversion boundary rejects a floating operand before
FoldConstant/ExactStored, with a diagnostic at the operand. Evaluate's named
constant path independently requires integer storage, including when reached
through a shift count. No floating bit pattern can become an exact integer
through either newly reachable path. Integer arithmetic, range checks and wrap
calculation are unchanged; float-to-integer constant conversion remains an
explicit unsupported boundary, not a new conversion implementation.

UntypedStorage now peels only grouping and unary +/- while classifying a
floating literal leaf. It then chooses double. Its existing integer ParseNumber
still receives the original unparenthesized node, preserving integer operand
range/type behavior. This repairs double(-(1.5)), float(+((1.5))) and
double(-(-1.5)) without introducing another expression evaluator. ConstantType
still leaves contextual literals untyped. The finite floating representation
conversion helper is unchanged from the original proposal.

The fixed positive C fixtures add expected-1.5/1.5/1.5 results for those three
forms, negative overflow classification, and the exact decimal midpoint
340282356779733661637539395458142568448.0 (2^128-2^103), which rounds into positive
float infinity. The prior lower/upper rounding, subnormal, alias and signed-zero
oracles remain unchanged. Five authored-only sample rejections require:

- uint64(Wide), for named double1.5: floating-to-integer constant conversion
  unsupported, at3:22.
- int(wrap: Wide): same category at3:22.
- uint64(double(1.5)): same category at2:22.
- 1 << Wide: integer-expression category at3:17.
- A floating binary declaration initializer: unsupported initializer at2:15.

These negatives use the existing authored sample runner in both compiler
backends, with literal messages/coordinates and no new test framework. They are
not placed in the shared stagezero-parity table because stagezero supports some
forms deliberately outside this source slice. Removing the production guards
would cause an acceptance/category mismatch; reverting float-leaf inference
would reject positive literal cases. Those mutation runs have not yet executed.

[Independent review](evidence/m2-integration/typed-float-revised-review.txt)
verified the exact patch hash, closed both blockers, checked source columns and
the overflow midpoint, and found no remaining source-design blocker. This is
approval of source design/fixture coverage, not execution acceptance.

Executed: existing chibi-build-host and chibi-compiler-host targets, and changed
semantic.c/package_compiler_chibi.c object targets build with two jobs and
warnings enabled. Formatting and diff whitespace checks pass. Embedding Chibi
source in a compiled C object does NOT semantically compile or execute it.

Pending: new literal/rejection execution on bytecode and SLJIT, actual mutation
controls, integer/exact-fold/native-name/array-watch regressions, ordinary asset
host generation, coordinated selfbuild/fixedpoint/recovery and Linux native
checks. The ordinary build remains blocked by the verified preserved
prepared20 artifact versus source/runtime21; no new build rerun is claimed to
resolve that dependency. No bootstrap generation, asset-disable option, PR688
adoption, runtime pin change or release occurred. Parent owns authorization of
the next isolated scratch generation step from a frozen source identity.

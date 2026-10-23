# Incoming compiler32 native catalog review

Source-only; immutable incoming tree 2acb927a94319b4bfff42fa56d5af613e4b664da, base d0de4a84ecf50badcab661411470bdf6f3da5fa2, compared with typed tree 0b5c20a7222a9d171bfcbe35570c184a78f4c8d1. Exactly 32 changed paths relative to the base. No import, active-source/index/metadata mutation, build, test or generation. Candidate runtime eligibility remains held after the owner's pre-test build failure. Current typed compiler parity and generated artifact identities are not invalidated by this future integration review.

## Required overlap resolution: package assertions

Do not replace typed Steps.chibi wholesale. Typed tree Steps.chibi1593–1595 recognizes an unqualified, unresolved package assert as a language intrinsic, requiring no native capability. Incoming Steps.chibi2054–2060 instead requires FindNative success and an actual intrinsic ordinal. Incoming Compile.chibi175–187 LegacyNatives annotates existing assert-named rows but does not supply the absent pure-catalog row. A direct replacement rejects ordinary assert(...) in the typed Assets/metadata/compiler packages with Unknown function, despite stable entry signatures.

Preserve both distinct routes: unshadowed unqualified package assert lowers directly to ops.guard without a native row/ID; richer qualified or aliased assertion calls lower only when their selected actual linked binding declares that intrinsic. Preserve lexical authored-function precedence and package import privacy; do not treat an imported package member named assert as the intrinsic. Do not add a synthetic assertion native, native capability ID, effect row, or host pack to the typed pure catalog. The rich adapter's actual ID+ABI lookup should remain authoritative for rich aliases; source spelling alone must not establish rich intrinsic identity.

This is a confirmed integration regression, not a new defect in the already-held standalone candidate contract. It is the same explicit typed assertion overlap identified in the earlier rich-schema review.

## compiler_target.c/.h: keep incoming additive rich API

Pure compiler_target_json at incoming341–351 and compiler_target_effects_json358–367 use input_new(catalog,NULL,false). The callable predicate80–85 is unchanged. Native selection/order, Native5/Port4 fields, six target arguments, effect array order, actual IDs and signature ABIs therefore remain unchanged for the same pure catalog. add_target298–330 retains the same prepared layout/instruction facts; no wire version or runtime service change is introduced here. Temporary selected-native vectors are freed after JSON construction. The incoming file does not manufacture assertion bindings or inject game capabilities into a pure catalog.

The additive compiler_behavior_catalog_input_json562–571 uses rich=true and actual application bindings. Keep the matching header declaration and caller routing together. Rich input has 18 arguments, CatalogNative9 and CatalogPort10; old pure Compile/CompileClosure remain12 args and effect-aware variants13 (Compile.chibi2219–2301). Do not send the rich schema to the typed asset facade or overwrite the typed values-only target facts with application catalog facts.

Selection96–121 keeps callable function indices, then appends unsupported functions/actions. All associated native/port/ID/effect/type-field/operator/maker projections share that selected index space. native_intrinsic166–174 consults native_capabilities_find(actual ID, actual signature ABI), preserving qualified/source aliases independently of canonical spelling. direct_native134–163 narrowly admits synchronous simple actions; unsupported rows remain visible to name resolution rather than silently falling through to another declaration. Fixed nominal aliases require exported matching storage/size/layout and bounded underlying traversal41–77. Defaults are copied scalar bits/length-aware text, not retained native addresses178–247. No additional actionable defect found in this reviewed adapter.

## Resolution, privacy and imports

Incoming Body.chibi1281–1347: local package authored functions precede catalog exports; exact canonical native names precede loose catalog-source matches; ambiguous loose matches return -2. References.chibi CalledFunction delegates using the original qualifier spelling. Ordinary AliasPlace imports still use exact package callable lookup; LibraryPlace is explicitly separate. CallStep checks PrivateAccess before dispatch, treats a qualified native namespace only when its prefix is unbound and not a known qualifier, and rejects a missing member under a real package alias rather than falling through to native lookup. Keep this whole lookup chain with its call-site updates, not isolated helper cherry-picks.

Body.chibi1525–1596 retains package privacy and explicitly treats source-library members as exported under that source model. Behavior SourceNatives marks actual parsed callable identities source-owned so native resolution excludes stale source rows. Source library calls establish dependencies from lowered Invocation rows; strict imports and cycle handling stay authored policy. No new actionable lookup/privacy defect found in the portions reviewed; this is not an execution proof of all source contracts.

## Preserve typed runtime and source inventory separately

compiler_host.c/.h and native_capabilities/execution files are not incoming changes relative to d0. Their apparent deletions when comparing whole trees against typed are absence of migration work, not instructions to remove it. Keep typed compiler_host_load_values, values-only native packs and pure scopes, cancellation/budget bridge, storage getter/decoded-host accounting and target-query helpers. Keep the compiled BootstrapContract source and no-KDL bootstrap integration. Incoming compiler_target.c's additive binding-aware API does not itself replace these facilities.

No ABI20 bump is justified by private rich compiler structs or additive native JSON producer API alone. A future combined source freeze requires source reproduction and both typed target resources regenerated against the final compiler; existing current-tree parity remains valid for its own recorded sources. The owner pre-test failure and known runtime eligibility hold remain outstanding. Do not infer compatibility or publication readiness from this source review.

## Focused future validation contracts (not executed)

- Pure catalog unchanged: zero synthetic Assert/native IDs; ordinary unshadowed assert true/false with zero/failing message; authored local assert shadow; imported private/public assert member; named arguments and supplied effect rows.
- Actual linked assertion aliases: different source/canonical names with real assertion ID+ABI, non-assert native sharing an assert-like spelling, unsupported action, source-owned native row exclusion.
- Strict package member failures/alias shadowing and loose rich ambiguity; array-method/native indices after unsupported rows append.
- Existing typed facade Build helpers, 12/13 argument CompileClosure routes, native64/wasm32 target identity and pure execution rejection; final compiler fixed-point/compiled [5,2] contract. Select existing affected dispatchers after source freeze and coordinator approval.

## Immutable source hashes

- src/toolchain/compiler_target.c: 8b2d14611567f15b42c0cce47891f0469667b8fdf177a02a5a982255e8320430
- src/toolchain/compiler_target.h: 765514a313dbecd487f4b0ea074b8dc4398dd044abefbc7961853bd4d0e2ae6a
- tools/compiler/package_compiler/Compile.chibi: d8f2e0e8698de16da6369cc78ae05e1a6e29aea14287dd65474f7f086cacbaae
- tools/compiler/package_compiler/Body.chibi: e2edc329baddb5c16bfe005f94863b59df052e103444843cdf68764fd44799bc
- tools/compiler/package_compiler/Steps.chibi: fb8bef6bc05fbdb3ee137b7576d7d97d2acf2861138b79267943c9c8bda08813
- tools/compiler/package_compiler/References.chibi: 182d47aefbb51e7ae97e7a373b18010c4606a2d2b331c3c0cfd27b7953302bde
- tools/compiler/package_compiler/Behavior.chibi: bf1fc927fc873a13d9880046a002f646b2af2e4494c65cc8778e5b1ba87f592d

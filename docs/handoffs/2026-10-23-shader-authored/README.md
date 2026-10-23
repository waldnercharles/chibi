# WIP authored shader source handoff — 2026-10-23

Repository: https://github.com/waldnercharles/chibi.git
Branch: `codex/chibi-shader-authored-handoff-20261023`
Source commit: `5cb470525ef8cef278de756326eb13501d203953`
Exact source tree: `b9492169c5aaa89409e242860c2bac9085c0d025`
Parent/base source commit: `1c1599fa1ca2405e0ee1d6a57dc7a9146db3b252` (tree `e4efa14d334f7dc5b6d465bf1906f77120992084`).
That source commit preserves merge parents d34def720 and d0de4a84. The separate
native evidence checkpoint is `1f82a73a7e0f76002e4fe9c9021176da24c090e4` on `codex/chibi-shader-foundation-d0-handoff-20261023`.
The original published PR663 branch remains unchanged at d34def720.

## Active source versus records

The seven active paths in source-manifest.json are the exact current working
source overlay: two modified task design files, three additional contracts/fit
reports, Facts.chibi and SourceMap.chibi. They are applied source at their normal
repository paths, not patches hidden in this directory. authored-source.patch
is a portable copy of that seven-file overlay against the exact base tree.
The checkpoint tip adds only this handoff directory. The source_commit above
reconstructs the active source without evidence additions.

No compiler package hooks, inventories, pins, bootstrap or resources were changed
for these new modules. Current C inspection policy is still production authority.
SourceMap becomes authoritative only with a single integrated prepared-service
cutover that removes the C policy; no semantic fallback or second evaluator.

## Compiler agreement and reviews

Frozen Facts Git blob `1f3e18588dc51ed73c9ad3f4f17d63f771907003`, SHA256
`c4c507730160898a9848051282a79784d0d6dc599773f044cfc66c31412d8e51` has explicit
compiler-owner semantic SOURCE agreement for the initial flat Float3/imported
TintGain/two calls/immutable alias/AddMultiply/exact constants/ranged rejection
slice. Agreement covers shared source/native Function rows with real IDs,
linked intrinsic/kind/direct metadata, final transitive effects, imported source
helper bodies, source-order arguments separate from parameter/field destinations,
typed defaults, resolved operation/conversion and operand/result types, nominal
field identities, opaque Origin.node anchors, ranged unsupported syntax and
owned result lifetime. It approves declarations/consumer contract ONLY. It is
not an Analyze implementation, hook permission, nested-record proof, artifact
or material acceptance. Private sparse optimization facts cannot substitute.

Frozen SourceMap blob `bc3e72e1d50292543b9723e9f6156f8c6d03a75f`, SHA256
`aecf1c34a92b204b28e4735b110083438de0cd602b052cbc3a0c3172b8db44a7` implements
pure validation, source/call/result/node lookup, ordered document equality and
nonzero generation/count checks. Independent immutable-blob review found no
concrete defect. Queries assume Validate accepted the tables. Float evaluation,
raw lanes, resolver pointers and native memory bounds remain outside this policy.
Bounded document-pair checks plus an authored all-match/count reducer avoid
serializing two8MiB closures beyond the existing64MiB escaped-JSON host limit.

Both modules are UNCOMPILED, UNEXECUTED and UNWIRED. Only source review,
whitespace checks and Git blob/SHA256 comparisons ran. See receipts/; C foundation
passes in the native branch do not provide authored-policy coverage.

## Missing implementation and dependencies

1. The ordinary compiler's complete owned Analyze producer/export and the
   first actual facts-driven shader consumer/lowering remain absent. Cloud owns
   compiler/LLVM; no local producer work is included or requested. Preserve
   shared semantic decisions; never recover meaning from bytecode/debug slots
   or private Value/Name/Work/SemanticFact records.
2. Ordinary nested declarations, layout/type requirements, contextual nominal
   construction and authored projection/call/return/import behavior required by
   Sample.uv, Parameters.tint and Surface fields remain absent.
3. The SourceMap prepared closure, stable root/entry IDs, ABI/layout-compatible
   inventory/hash witness, SDK tooling facade/install and runtime-only consumer
   proof with compiler/source discovery unavailable remain absent. No source
   compiler fallback. No generated inspection binary is local-only: none exists.
4. The agreed typed game:assets SourceMaterial/ShaderUniform constructor and
   authored finalization dispatch are not implemented here. Obtain the separate
   typed-assets checkpoint and resolve its own remaining gates; historical
  31098151b is contract context, not a tested current-main composition.
5. No real authored material fixture compiles/renders; no source-driven CPU
   inspection of that material, same-generation CPU/GPU proof or Studio
   integration exists. Flat facts/schema/provenance probes are not acceptance.

Existing compiler/coordinator checkpoints to verify independently are
0b1492796941d6bd4159dc8b920f7ca8fe35c2c9,
8f6545bd8221748f3708036607762209a6f4e0ab and
93d1a5d9cf46b1dfdbdb024207d9dbc6e3278ae7. These branches were not imported or
composed into this snapshot. Other owners' typed/SDK/CI candidates remain separate.
Historical host binaries, object libraries, installed SDKs and build directories
are machine-specific and excluded. Original absolute paths in receipts are
forensic labels only, not prerequisites for cloud execution.

## Next steps and holds

Verify remote commits/trees/manifests first. Review actual compiler inputs and
ordinary nested semantics; package the policy closure with its exact witness,
wire the mechanical runtime facade and replace C policy atomically. Extend the
existing C/pico --shader-source-debug route through the real prepared service;
it currently tests only the C foundation. Then implement typed finalization,
one Chibi lowering to existing owned shader IR, and real rendering/CPU source
inspection from the same immutable generation. Run appropriate required gates
on exact candidates and preserve independent platform/GPU/Studio gaps.

PR663 remains draft. No merge/release/default/ABI/deployment denial is cleared.
No builds/tests/generation or additional implementation ran for this handoff.
Local owner will make no further overlapping implementation changes after
handoff and will hold the candidate while cloud confirmation is pending.

# WIP typed engine handoff — 2026-10-23

The complete active engine source is the exact Git tree `6ef6b76e5c7e83b328e2973cc15c42fc79804043`, preserved on `codex/typed-assets-source-handoff-20261023`. Its single-parent source commit is `2227496827abb8ef59578216b65573f8cc6fae39`, parent `30cd287d1b86250d13b42c3716a94d747fa87a9b`. This evidence branch is a child of that source checkpoint. Use the source checkpoint for implementation; archival evidence here is not active source.

The original worktree still has HEAD30cd and pending MERGE_HEAD `fa1b7b2bb297eacbbed6aec6f03c4260355db97c`; its index remains exact6ef, with no unstaged/untracked active source. No merge was completed. Preserve the pending parent distinctly from the later d0 ancestry forecast. fa1b is an ancestor of published d0, also reachable from coordinator checkpoint93d1a5d9cf46b1dfdbdb024207d9dbc6e3278ae7.

## Status

- Native: passed632 cases /236737 assertions, exact6ef.
- Materials: passed110 /270494, exact6ef.
- Projects: FAILED, exit2. `public_asset_constructors_reach_native_readers`, validate_project.c279: Constructor scenario reports scene has no entities. Final selector27pass/1fail/329assertions. Full log and original JSON receipt are preserved; projects log SHA25688e66e3227f4a5c0b0f86005abe8d209c794493fd3bbbcd885ca51df0cc35695.
- Language-all and broad tools: UNRUN on exact6ef. Earlier selectors are narrower historical evidence only.
- Source reviewed with final protocol9/ancestry audits; this does not clear the failed required group or unfinished acceptance. No new build/test or fix was run for handoff.

The source tree includes compiler bootstrap19 inputs/artifact, native64 and wasm32 compiled asset37 resources, target facts, resource facades and provenance. Prepared ABI20 remains distinct from host protocol9. Native build-only .inc files are regenerated from the tracked prepared artifact by existing native build rules; large ignored .inc and native products are not shipped as handoff source.

## Studio

Repository https://github.com/waldnercharles/chibi-studio.git. Complete active source commit `466e2cc3545927261cf68f88d54caed0093f7cc4`, tree `3352628508469dbce5db9c24bf0b7df027f124e7`, parents `521b666a62406fabae8e1d23bad00765717b3635` and `df050886dd7721db63b40fdf5435f1cb20ceeb34`. Preserved on its typed development branch and WIP handoff branch; verify remote refs before taking over. Studio has no untracked or ignored local source. Scratch resolution copies are partial historical evidence, not a complete substitute.

Latest narrower paired evidence used SDK68e625, production engine tree0b5 and protocol8, not a final6ef SDK. Studio model1/261 and SDK-values12/163 passed. The tested Studio tree equals335, but postcommit SDK/build metadata currency, full cold/native/Web/hosted and platform acceptance remain pending.

## Evidence and unapplied drafts

`active-source-manifest.json` describes every active source file with Git object, byte count and SHA256. `portable-original-manifest.json` maps ignored source, patches, review receipts and logs into `portable-original/.chibi/...` with original byte hashes. Absolute machine paths inside historical receipts are provenance only and need relocation; the new manifests use portable relative paths. Historical timing holds are not current policy; no global test wall-clock ceiling remains.

`draft-catalog.json` keeps unapplied proposals, historical snapshots and deliberately broken negative controls separate with their original base/source receipts. Do not apply them as a combined patch. SDK warm/HOST2/native-pair/Project and later compiler drafts belong to separate owner handoffs and are not composed here. Compiler/coordinator remote checkpoints0b1492796941d6bd4159dc8b920f7ca8fe35c2c9,8f6545bd8221748f3708036607762209a6f4e0ab and93d1a5d9cf46b1dfdbdb024207d9dbc6e3278ae7 are separate source/evidence states.

## Dependencies, limits and next steps

Cloud must rebuild native/SDK/Web products and verify matching current source, target, ABI/catalog, bootstrap and resource identities. Local dist/sdk-release and build directories are excluded machine-specific dependencies, not portable acceptance. SDK/compiler runtime publication and immutable Studio workflow binding remain separate. Local starter/Terrain Lab projects still use old manifests and the old working SDK; they are preserved, not migrated. The source-map recipe is retained; use separately validated candidates before replacing originals. Retired game repositories remain read-only.

Recover and investigate the Constructor failure at its actual cause, then run affected and required groups on a coherent exact candidate. Finish remaining project routes, final paired installed/relocated SDK and Studio/browser acceptance, and source/ancestry review before any merge. Preserve explicit review/server/publication holds, normal native defaults, ownership/effect limits and denied deployments. Do not use this checkpoint as release, merge, default, LLVM, pointer or upload approval.

I will make no further overlapping implementation changes after handoff. The original candidate and worktrees remain held while cloud verifies the remote checkpoint and confirms takeover.

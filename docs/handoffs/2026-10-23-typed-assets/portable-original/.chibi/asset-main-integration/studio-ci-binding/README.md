# Normal Studio CI binding proposal

Prepared only; the workflow patch is not applied. Previous 521 proposal, receipt, YAML and publication evidence are preserved under `historical-521/`.

## Preferred immutable binding

Use actual Studio merge commit `466e2cc3545927261cf68f88d54caed0093f7cc4`, tree `3352628508469dbce5db9c24bf0b7df027f124e7`, with parents 521b666 and df050886. The Studio worktree is clean. Its committed tree exactly equals the Studio tree in the sealed narrow native pass: SDK values 12 cases / 163 assertions and model 1 case / 261 assertions against native SDK `68e625a9c39975180f01590d5a8d4a5ff58c1266dc276cf7a1072a232eae97ad`. Read `receipt.json` for exact logs, hashes, source snapshots and command outcomes; no new execution occurred in this review.

The packaged SDK came from engine tree `0b5c20a7222a9d171bfcbe35570c184a78f4c8d1`; the later execution tree `966dbaccf829cdbb53d2a2988080a38d46aa4a74` changes only the existing SDK test oracle. The package remains marked dirty against engine HEAD 30cd287, so this is local source-tree evidence, not a clean published engine/main claim.

Commit 521b666 remains a historical minimal production-compatible alternative: production, build settings and assets are byte-identical between 521 and 466. Prefer 466 because it is the actual completed ordinary merge, includes eligible PR92 acceptance coverage, and its exact tree received the sealed narrow validation. No future commit identity is needed.

## Normal job and publication dependency

The active Linux `studio` job still checks out floating remote main. That currently resolves to df050886, which requests SDK `asset_text`, includes its retired reader, and has starter `project.kdl`; current engine exports `asset_documents` and expects version33 compiled Chibi projects. The proposed one-line patch pins only this Linux checkout to 466. Default Studio acceptance is OFF; normal CI builds the production target and starts a private copy of starter. Windows Studio is paused with `if:false`; optional paired workflow and selectors remain unchanged.

GitHub currently returns HTTP 422, No commit found for SHA 466e2cc. No advertised branch points to 466. Publish the reviewed existing commit and verify GitHub resolves its exact commit/tree before applying the patch; the known-unavailable pin must not be applied yet. No push or publication was attempted. Public Studio checkout requires no new secret.

The engine's final source and compiler/asset resources must also be reviewed and committed before normal CI's clean SDK source identity check can pass. Native narrow evidence does not establish Linux hosted startup, final cold Studio, Web or other platform acceptance.

## Artifacts and preservation

- `ci-pin-tested-studio-after-publication.patch`: proposed one-line immutable 466 ref.
- `ci.before.yml` / `ci.proposed.yml`: exact staged workflow input and proposal.
- `receipt.json`: engine workflow blob/hash, actual Studio commit/tree/parents, source equivalence, sealed evidence and remote availability.
- `pin-publication.*` / `studio-remote-heads.txt`: raw read-only remote checks.

No active engine/Studio source, index or merge metadata changed. No compiler-owner file was edited. No build, test, generation, application launch, push or publication occurred.

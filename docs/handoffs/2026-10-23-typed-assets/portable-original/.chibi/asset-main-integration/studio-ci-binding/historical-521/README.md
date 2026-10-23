# Normal Studio CI binding proposal

Prepared source-only; the patch is not applied. Read `receipt.json` for immutable inputs, local commit provenance and raw remote-availability evidence.

## Finding

The active engine CI candidate's Linux `studio` job checks out `waldnercharles/chibi-studio` without `ref`, then packages the clean engine SDK, builds the default Studio target and opens a private copy of `projects/starter`. Its source receipt records the selected Studio revision but does not make that floating checkout compatible. Remote Studio main currently resolves to `df050886dd7721db63b40fdf5435f1cb20ceeb34`; it still requests SDK component `asset_text`, includes `support/asset_text.h`, and has `projects/starter/project.kdl`. The candidate engine exports `asset_documents` and expects version33 compiled Chibi projects.

The smallest compatible existing immutable consumer commit is `521b666a62406fabae8e1d23bad00765717b3635`, tree `19c1bbfb599f7c8a6a0ba92e063b37e60c078524`. Its production/build/project/web contents are identical to the pending resolved Studio merge tree `3352628508469dbce5db9c24bf0b7df027f124e7`. That merge adds only acceptance README, dispatcher and sdk_values coverage; acceptance defaultsOFF and normal CI does not build its target. Therefore521 is sufficient for the normal job's production/startup contract. The final merge is not required for this pin. Its tree identity is not an available commit SHA and must not be supplied to checkout.

## Publication blocker

Read-only GitHub API lookup of521 returnsHTTP422: No commit found for SHA. No advertised remote branch points to that SHA. The patch is a concrete proposal using a real local tested commit, but is not runnable until that commit is published and GitHub confirms the exact commit/tree. Do not apply a known-unavailable checkout pin. No publication was attempted.

## Minimal patch

`ci-pin-tested-studio-after-publication.patch` inserts one literal `ref:521...` in the normal Linux Studio checkout. It changes no selector, script, optional paired workflow or paused job. Windows Studio has `if:false` and is left untouched. The optional paired workflow already accepts an explicit full Studio commit; its existing publication and verification requirements remain.

After approved publication of521, recheck GitHub availability/tree, apply this one-line patch to the exact recorded workflow input (or refresh against subsequent workflow edits), and use the parent-selected CI validation. The engine source candidate and generated compiler/asset resources must also be reviewed and committed: normal CI requires its packaged SDK source to equal clean `github.sha`. This proposal neither publishes nor replaces that engine evidence. The public Studio repository is readable with the workflow's read-only token; no new secret is required.

No active engine/Studio source, index or merge metadata was changed. No build, test, generation, launch, push or publication occurred. The proposed job's Linux startup is unvalidated here; the parent model261 pass is not a Linux/cold/Web claim.

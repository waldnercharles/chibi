# WIP recovery-only: shared-sequence-slices

Preserve existing source, not an accepted implementation. Date:2026-10-23.

Original base: `8b62b1cce22ee2f339b01eeecec4c952e769294d`. Pending merge parent: `17aa9ad383fddb7eeb223e2f1ee3445d0648670c`.
Raw working source tree: `3c2ae5c66d3310477810817e96a18968ffb96cf6`. Raw source commit: `2bb6acd7c44a3b913d2dcd862daacafb2e644cdc`.

This branch adds only this evidence directory to its original source.
The manifest preserves original stage entries, untracked paths, source ABI and
single-parent recovery snapshots. Pending merge ancestry is retained on separate
base refs; no merge or conflict resolution was performed.

The shared-array draft implements superseded reference semantics (source ABI22
with bundled ABI21 artifact). The string prototype has11 unresolved index paths
and33 preserved conflict-stage entries; no matching validation is claimed.
Keep both separate from active collection source97c. No tests or builds ran for
this handoff; historical claims belong to their exact earlier sources only.

Active source, prior test failures, reviews and complete receipts are on
`codex/wip-collections-handoff-20261023` under `evidence/collections-handoff-20261023`.
Use that branch's README, snapshot-index and manifests; do not apply old drafts
wholesale. Existing acceptance/merge/release and combined-source compatibility
holds remain. No machine binaries, ignored authored source, or secrets were
needed. Original worktrees and dirty state remain unchanged.

Local implementation and validation are held pending cloud verification. I will
make no further overlapping implementation changes after handoff.

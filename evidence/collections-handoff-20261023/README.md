# WIP recovery-only: shared-array-objects

Preserve existing source, not an accepted implementation. Date:2026-10-23.

Original base: `329d8fb046612caa17f4aa99ee61f96512a2fa60`. Pending merge parent: `f04d9d37822edb44a8fb1d1f6f68e5546eb8e049`.
Raw working source tree: `77d42a44b0583cfcf45a03c37f4c38477071e935`. Raw source commit: `65bb34921cb7af2c194c33bfc93b4e35f7915997`.

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

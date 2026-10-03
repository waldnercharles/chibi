Summary: Share immutable compiled third-party dependencies across c-blueprints worktrees, reduce build/test output, and migrate relevant checkouts without losing local edits.

Context: Dependency sources already use shared pristine snapshots and APFS clones. Most compiled dependencies still build in each checkout. Ordinary builds also trigger runtime packaging. Existing worktrees have divergent CMake definitions and some have staged or unstaged changes.

System impact: Package identities determine reuse from dependency source and patch content plus compiler, SDK, architecture, flags and platform. Package publication is locked and atomic. Application generated state remains private. Full command logs stay on disk while routine terminal output is concise. Build failures retain nonzero exit codes and useful diagnostics.

Changes:
- CMake dependency definitions and configure helpers: build or reuse shared native/web packages automatically.
- Makefile and output helper: concise summaries, retained full logs, bounded diagnostics and verbose override; avoid redundant package profiles.
- Infrastructure tests: package reuse, invalidation, publication/concurrency and output/failure behavior.
- Global ccache: compare APFS cloning against compression before enabling.
- Desktop settings: enable supported cleanup controls, retain current worktree locations.
- Existing worktrees: inventory and snapshot local state, apply compatible focused changes, validate preservation.

Verification: Focused tests; native make build and make test; serial and worker make web; reuse from a second checkout; identity separation and concurrency; migration state checks.

Validation completed: default native build, full native test suite and whole-function audit, both web builds and their combined package, development build/staging/headless bootstrap, cross-checkout package reuse, private fallback configuration, cache identity/publication tests, quiet-output retention tests, and packaging notice regression checks.

Deployment: integrate the scoped commit into main while preserving unrelated local files. Existing branch migrations retain their original dependency declarations, HEADs and staging areas, with external backups and immediate drift checks. Custom external dependency edits keep private builds. A temporary checkout that disappeared during validation is excluded rather than recreated.

Desktop outcome: Claude PR auto-archive is enabled. Codex retention could not be changed because the computer-control tool rejects access to Codex settings. Worktree locations remain as configured.

Workspace pooling and automatic build retirement are assessments in this request. Claude can replace allocation using WorktreeCreate; Codex exposes post-creation setup. Safe retirement requires exact generated paths, activity/build leases and source-state preservation, not branch names alone.

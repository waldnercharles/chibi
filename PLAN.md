Summary: Add a shared workspace allocator for Codex and Claude, with deterministic retirement of verified generated build files.

Context: Shared dependency packages and quiet builds are already integrated. The desktop apps have independent worktree creation and lifecycle rules. Existing checkouts contain local changes, nested worktrees and external dependency references, so they cannot safely enter an automatic cleanup pool by directory name alone.

Design:
- A local Python manager owns new task worktrees, a small pool of active tasks, unique branches, and session ownership. Task paths remain stable, including after completion; a different task never inherits an old chat's path.
- Supported session/tool hooks enforce ownership. A launcher opens the allocated checkout as a local project. Native app worktree buttons retain their own behavior. Hook installation preserves existing settings and does not approve Codex hook trust.
- One inherited OS lock covers a complete make invocation, including recursive builds, tests and runtimes. Configuration registers only new managed build directories.
- Retirement keeps three recent workspace build sets and considers older sets after seven idle days. It verifies generated output hashes and identities, rejects active sessions/processes, and preserves unknown files, source, branches and shared caches. Existing builds are excluded.
- A user LaunchAgent runs deterministic cleanup without model calls. Configuration and hook definitions are backed up before installation.

Implementation: tools/workspaces.py, tools/workspace_retirement.py, installer, Makefile/configure integration, focused process/Git/filesystem safety tests, and a user guide.

Validation: temporary Git repositories exercise capacity, branch/path stability, conflicting sessions, handoff/revocation, active locks, child inheritance, concurrent allocation, dry runs, changed or unknown artifacts, symlink and nested-worktree rejection. Test Make goal/variable/recursive propagation, installer merging and idempotency, and a real managed build followed by a retirement dry run. Install only after these checks pass.

Delivery: integrate the scoped changes while preserving unrelated main-checkout edits. Install the shared launcher, merge hooks, and load the cleanup LaunchAgent. Document any app version or hook-trust step that cannot be completed through supported interfaces.

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

Completed: 38 workspace/retirement/installer/Make checks pass, together with the existing configure, quiet-output and runtime-wiring checks. A real managed native build and full `make test` pass, including the whole-function audit. A dry run and applied retirement agree on 752 generated files (708,692,904 logical bytes); unknown files and CMake/Ninja configuration survive, and a normal controller rebuild succeeds afterward.

Installed: shared CLI, five hooks per app with original settings verified intact, repository capacity four, and hourly LaunchAgent using keep-three/seven-idle-day retention. The first scheduled execution exits successfully. Claude CLI was updated through its existing Homebrew channel to 2.1.285. Its desktop command requires an interactive CLI account sign-in, which remains a user action; Codex requires review of the new hook definitions in `/hooks`. Neither trust nor authentication was bypassed. Existing checkouts and builds remain outside this manager. Validation records are saved under `~/.codex/worktree-cleanup/2026-10-03/workspace-pool-validation`.

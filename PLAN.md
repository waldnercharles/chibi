Summary: Support the apps' normal built-in worktree workflows through automatic session hooks. The custom launcher is optional and is not the default workflow.

Implementation:
- Discover the exact Git checkout on session/prompt/tool hooks for registered repositories, including the primary checkout and detached linked worktrees.
- Keep app-created paths, branches, task counts and archival under app control. Track activity for multiple sessions independently, with Stop/SessionEnd handling across handoffs and deleted paths.
- Keep existing build ownership, full Make invocation locks, generated-output manifests and conservative retirement checks. Do not adopt old build directories or reset user branches.
- Migrate compatible older worktrees' build instrumentation while preserving local changes and indexes; do not modify tracked files from session hooks.
- Preserve existing global hook definitions/settings. Install updated helper code without bypassing app trust. Native Claude use does not require the custom CLI launcher or its sign-in.

Validation: Focused native-session and retirement tests cover detached and primary checkouts, multiple apps, stopped turns, deleted/recreated paths, nested unrelated repositories, old build preservation and cleanup locks. Run real detached-checkout hook/build checks and verify migration preservation. App trust/loading is reported separately from direct hook-handler tests.

Completed validation: 18 core tests, 22 retirement tests plus one focused stale-record regression, 12 Make integration tests and 5 installer tests passed. A Codex-created detached checkout completed its first ordinary controller build before any session hook, reused the shared package and recorded 380 generated outputs. Direct hook payloads verified independent app sessions and teardown from a deleted cwd while preserving source status, index and detached HEAD. Installed helpers match source; existing app settings and the scheduled job are unchanged. Desktop UI hook dispatch and trust remain unverified.

New worktrees need a branch containing the integration. Local main has it; the cached origin/main ref remains older because this work has not been pushed. Use the normal starting-branch selector to choose local main. Hooks observe older checkouts without silently rewriting their tracked build files.

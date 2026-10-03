Summary: Support the apps' normal built-in worktree workflows through automatic session hooks. The custom launcher is optional and is not the default workflow.

Implementation:
- Discover the exact Git checkout on session/prompt/tool hooks for registered repositories, including the primary checkout and detached linked worktrees.
- Keep app-created paths, branches, task counts and archival under app control. Track activity for multiple sessions independently, with Stop/SessionEnd handling across handoffs and deleted paths.
- Keep existing build ownership, full Make invocation locks, generated-output manifests and conservative retirement checks. Do not adopt old build directories or reset user branches.
- Migrate compatible older worktrees' build instrumentation while preserving local changes and indexes; do not modify tracked files from session hooks.
- Preserve existing global hook definitions/settings. Install updated helper code without bypassing app trust. Native Claude use does not require the custom CLI launcher or its sign-in.

Validation: Focused native-session and retirement tests cover detached and primary checkouts, multiple apps, stopped turns, deleted/recreated paths, nested unrelated repositories, old build preservation and cleanup locks. Run real detached-checkout hook/build checks and verify migration preservation. App trust/loading is reported separately from direct hook-handler tests.

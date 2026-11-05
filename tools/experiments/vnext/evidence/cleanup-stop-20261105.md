# Cleanup stop handling checkpoint

This fixes the experimental Linux Python process supervisor, not compiler throughput or a language feature. Stop signals arriving during cleanup remain terminal, both caller handlers are restored before handoff, and the timer inherits blocked stop signals so a failed join cannot reopen that handoff race. Existing deadlines, group identity, pidfd cleanup and the one-second cleanup budget remain unchanged.

Four focused unittest invocations were independently checked: seven cleanup subcases and two live-timer handoff subcases pass with the fix. The unchanged original fails all seven successor-selection assertions; the preceding candidate fails both inherited-worker-mask assertions. These are nine positive subcases and nine counterfactual subcases, not eighteen selectors. All four outer owners reached ECHILD without survivors, interruption, timeout or cleanup error.

The exact source changes and receipts passed independent review. See cleanup-stop-20261105.json for source hashes, bounded diagnostic results and limits. Broader baseline/frontend/bridge regressions and optional real-controller integration remain pending. No compiler, backend or generated native program was executed by these focused controls. This checkpoint does not establish ABI, platform, throughput, CI or merge acceptance.

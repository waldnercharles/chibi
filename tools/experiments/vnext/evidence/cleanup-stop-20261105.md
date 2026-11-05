# Cleanup stop handling checkpoint

This fixes the experimental Linux Python process supervisor, not compiler throughput or a language feature. Stop signals arriving during cleanup remain terminal, both caller handlers are restored before handoff, and the timer inherits blocked stop signals so a failed join cannot reopen that handoff race. Existing deadlines, group identity, pidfd cleanup and the one-second cleanup budget remain unchanged.

Four focused unittest invocations were independently checked: seven cleanup subcases and two live-timer handoff subcases pass with the fix. The unchanged original fails all seven successor-selection assertions; the preceding candidate fails both inherited-worker-mask assertions. These are nine positive subcases and nine counterfactual subcases, not eighteen selectors. All four outer owners reached ECHILD without survivors, interruption, timeout or cleanup error.

The exact source changes and receipts passed independent review. See cleanup-stop-20261105.json for source hashes, bounded diagnostic results and limits. The subsequent broader regression result is recorded below; optional real-controller integration remains pending. No compiler, backend or generated native program was executed by these focused controls. This checkpoint does not establish ABI, platform, throughput, CI or merge acceptance.

## Broader regression follow-up

The unchanged baseline, frontend and bridge suites have now completed: 27/27, 29/29, and 28 passes plus nine explicitly expected optional skips, respectively. Total: 93 selectors, 84 passes, nine skips, zero failures or errors. Each suite retained its 119-second work limit and one-second cleanup allowance. Actual work times were 14.247, 5.572 and 2.179 seconds.

Independent final readback checked exact selectors and skip reasons, normal outer and owned exits, clean process ownership, and source/runtime fences. The nine skipped controls require explicit compiler/tool identities, which this Python regression phase intentionally did not supply. They are not counted as passes. The new focused cleanup and live-timer tests ran normally within the bridge suite. No compiler, backend or generated native program was executed; source-native replay remains separate.

# Manual verification log

Requirements in `docs/spec.md` that need a running window, recorded when a human
checked them.

`tools/spec_coverage.py` treats an entry here the same as a citing check in
`test/self_check.c`, so a phase cannot be marked done by calling its UI
requirements "manual" and leaving no evidence. Every entry names the commit it
was verified against, so a claim that has aged past the code it covers is
visible rather than implied.

Rules:

1. Add an entry only after a human has actually exercised the requirement.
2. Record the commit that was running at the time, not the current HEAD.
3. A requirement that later gains a headless check loses its entry here. The
   coverage tool reports an ID that has both, because the manual record then
   rots unnoticed.
4. Re-verify and add a new row when the code behind a requirement changes
   substantially. Keep the old row; this is a log, not a status table.

## Phase 1

| ID | Verified | Commit | What was exercised |
| --- | --- | --- | --- |
| `UI-6` | 2026-08-18 | 4172695 | Toolbar shows Run, Pause, Resume, Step, Stop, the entry-point picker, and a live state and step-count readout. |
| `UI-7` | 2026-08-18 | 4172695 | Actions invalid in the current run state are disabled and give a reason on hover. Pause was found permanently disabled during this check and fixed; re-verified afterwards. |
| `UI-8` | 2026-08-18 | 4172695 | Print String output reaches the output log with its originating node, and clicking an entry selects and reveals that node. |
| `UI-10` | 2026-08-18 | 4172695 | Pin displays show run-recorded values while a run is live and preview values otherwise. |
| `UI-18` | 2026-08-18 | 4172695 | Run, Step, Pause and Stop leave the undo stack unchanged. |
| `UI-19` | 2026-08-18 | 4172695 | Revealing a node centres the canvas on it. |
| `UI-36` | 2026-08-18 | 4172695 | An action blocked by run state reports that reason rather than failing silently. |
| `UI-40` | 2026-08-18 | 4172695 | The output log offers Clear, disabled when the log is empty. |
| `UI-41` | 2026-08-18 | 4172695 | Editing during a live run stops the run and applies the edit. Verified against the crash found on Run, Reset, Undo, which is fixed. |

Notes on this round: `RUN-3` and `RUN-4` were verified manually as well, by
stepping an Event to ForLoop to Delay to Print String graph and confirming each
press activates exactly one node across the Delay suspension. Both also carry
headless checks, so they are not listed above.

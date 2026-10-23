# Source review status

This handoff summary records earlier source reviews; it is not a newly executed review/test attestation.

- ECS retained cell identity, permanent invalidation, storage accounting and idempotent release were reviewed.
- Targets are installed before eager task execution, deduplicated and independently inherited by children. Empty target lists preserve prior eager behavior. Foreign-world cells cancel the new task without invalidating the foreign cell.
- Scheduler checks cover pending requests that never complete. Initial/resume/READY/inline continuation/final-success gates stop invalid target work before authored consumption.
- Review found that a broad finished/stopping guard could strand an ordinary failing continuation's READY request. The source fix uses stopping or actual wait detachment and preserves normal error cleanup. The regression cases are written but unexecuted.
- Early finished/error exits publish task completion idempotently, releasing target retains while preserving completed outcomes. A narrow rereview found no remaining concrete outcome/ownership defect.
- Root review fixed a test fixture's intentional host-request release bookkeeping before freeze. No test or build was then run.

The pointer owner reviewed the exact b2c82d tree and found no additional immediate runtime gate/ownership defect. All such statements are source-only.

Ten new cases cover absent/stale/foreign admission, effect-free program-world admission, child cascade/unbound pointer users, late and synchronous completions, inline target invalidation, ordinary failing continuation cleanup, end-of-body success gating, inherited plus distinct targets, completed parent/child dependency and result stability, and destruction orders. Their lifetime_start/lifetime_bind fixture explicitly installs private C targets. It does NOT test authored inference.

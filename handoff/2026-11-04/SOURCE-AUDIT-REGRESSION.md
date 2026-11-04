# Allocation audit failures remain failures

Commit `083e82408a337787cfd9f08c1fff1113a1884831` fixes the assertion-path audit gap identified during nested-pointer validation. The source audit used to return 99 on an allocation-bound failure, but the program shim replaced that result with a pending assertion. SourceProbe then accepted the expected assertion and zero-live prefix without observing the failed peak bound.

The fixture now reports counts and traps immediately when its audit is invalid. Healthy results and intentional assertions retain their original behavior. NativeProbe distinguishes an expected runtime trap from an audit trap by requiring the audit report to be absent: the report proves entry already returned. A small table entry tests that distinction without running NativeProbe's runtime fixture suite. Production runtime code, allocation limits and allocator instrumentation are unchanged.

## Actual regression evidence

The focused checked-in script is registered in native CI and uses real existing source fixtures, normal/forced collection and O0/O2:

- With the old audit, the same regression fails because four zero-bound assertion runs are incorrectly accepted. The fresh baseline took 30.610 seconds.
- With the correction, all **16 healthy executions** and **two rejected zero-bound audits** pass. Both callback modes are covered. Every audit reports zero live allocations, and healthy peaks range from 1 to 967, below 2,048. The corrected phase took 33.641 seconds.
- The independent predicate mutation removes only the no-audit-report condition. Its table program compiles, then returns exactly 1 with empty output. This earlier 23.225-second proof was reused only after verifying identical predicate source, compiler/core and tool closure; the revised callback script is not part of that mutation phase.

Each phase retained its original 120-second total bound, including five seconds for cleanup, two-CPU affinity and one compile worker. Separate owner receipts confirm authoritative empty-child termination with no survivors or errors. Final independent review rehashed all source/tool and recorded artifact inputs and matched all four tested files to the committed patch.

An earlier attempted test matrix incorrectly paired AssertionCleanup with callback hooks, which need a collector that fixture does not emit. That attempt failed at LLVM validation and remains failed. The corrected matrix uses AssertionCleanup without callbacks and the existing NestedPointers Failure entry with callbacks, matching their established uses. No compiler behavior was changed to accommodate the test.

The 335-file local proof archive is SHA-256 `67fc60831f19a9a7a72aa94e8dc97b44055193ab88fe86b21f924036df5a3015`; source snapshots are reproducible from the pinned base and exact patch rather than duplicated in that archive. Final admission is SHA-256 `160cc06bb87b9ff8c1ce44a0e300932c1268ec513405c76b4c7d7c8ee8e6002c`. `source-audit-results.json` records receipt identities and scope.

This admits the focused Linux harness correction. It does not claim the broad managed/task suites, actual Windows execution, product integration or PR #1331 merge acceptance. No excluded union leak investigation, fabricated dangling pointer or premature image unload was run. Timings are bounded correctness diagnostics, not performance comparisons.

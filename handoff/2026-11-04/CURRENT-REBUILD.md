# Current takeover compiler rebuilt and tested

The exact published takeover source, `02c86287430def964445ec90185961e20319536d`, now has a separately rebuilt Linux compiler. The newly admitted 14f7 intermediate compiled it without changing the frozen c5 seed or restarting the historical bootstrap.

## Evidence

- All 4,054 tracked source blobs and modes matched tree `780ba87ea9f29308f2f20a94381cdce8cc5d26ca` before and after execution.
- Stage two completed in 97.471 seconds; stage three in 96.616; stage four in 96.829.
- Stages three and four are byte-for-byte identical: SHA-256 `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`, 11,576,992 bytes.
- All 573 exact compiler tests passed in 63.355 seconds, with no missing, extra, duplicated or failed test names.
- Every owned process terminated. Source, LLVM tools, runtime libraries and linker inputs remained unchanged.
- The coordinator independently checked the fixed-point and compiler-test receipts. This is not a claim of complete independent controller review.

The four phases total 354.271 seconds. These are bounded diagnostic/correctness runs on Linux, interleaved with another workstream, not a quiet benchmark or a reproduction of the old hosted Windows producer. Each build retained a 120-second cap and explicit `--jobs 2`. Tests retained their 180-second cap, two execution workers and two-CPU affinity; the unmodified test compiler selected nine LLVM workers inside that affinity.

`current-rebuild-results.json` records the source, artifact, test census and bounds. A local package contains the executable, exact source archive, raw fixed-point pair, logs, receipts and a read-only verifier. Its verifier independently rechecked the archive's Git blob identities, modes, hashes, raw pair and exact test names. This is a new local package, not a release or a seed-pin change.

## Jobs-forwarding regression

With that compiler, the **entire** `test/bootstrap/native-runner.sh` passed against checkpoint `cc5c8346a9c7e0bf2a45a1c1bc015dd29cdf9b00` in 17.155 seconds. This extends the earlier extracted stage-contract proof to execution, cache reuse/rejection, cleanup, in-flight source changes, controlled fixed-point cases, Windows argv planning and legacy-seed/resource behavior. Deliberate child failures are expected negative cases; the outer suite returned zero. All 4,062 tracked source blobs and modes stayed intact and all owned processes terminated.

The stage cases use controlled fixtures, not another real bootstrap. `native-runner-full-results.json` records the exact script, controller and log hashes. Full graph-driver validation subsequently passed; see below.

## Full graph-driver regression

The complete `DriverTests.chibi` suite exposed a stale comparison fixture: it rejected the mandatory `CommandTests-Run` output. The baseline failed at the predicted native-only assertion in 9.711 seconds. A fixture-only correction now emits a distinct contract marker, preserves planner rejection, and runs comparison cases from a scratch cwd with controlled legacy-seed preparation. This also prevents a test from downloading a real seed. It asserts no preparation in native-only mode and exactly one request for the expected seed in comparison mode.

Independent review approved the exact patch and controller before the corrected suite passed in 7.819 seconds. Separate test compilations took 6.239 seconds before and 5.314 seconds after. Both runs retained the 120-second cap, two-CPU affinity, unchanged tracked-source/tool fences and clean process termination. These are correctness timings, not an optimization comparison. The nested Driver compilation retains its own default worker selection. `graph-driver-results.json` records the before/after evidence. No production check was removed or weakened.

## Remaining gates

This does not establish Windows acceptance, successful historical producer CI, a full product/engine build, or readiness to merge PR #1331. Its explicit hold and independent review requirements remain. The earlier union-native leak investigation has not been rerun or declared cleared. The lost original c5 chain remains terminal-unknown and untouched.

The jobs correction and these records are committed locally. The earlier publication denial has not been bypassed or retried; no remote push is claimed.

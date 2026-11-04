# Foreign-call work in progress, 2026-11-04

This is an **inactive, incomplete recovery checkpoint**, not an integrated feature or a merge-ready change. The active experiment remains the package-aware implementation at `0545ef70f7e7610a89346925091dc4d802f6e803`. Nothing in this directory is loaded by the compiler or test runners.

`foreign-call.patch` preserves all 16 modified/new source files at the time publication was prioritized. `manifest.json` pins its base, exact resulting source tree, patch SHA256, and each resulting file. The patch includes the original-source typed producer, version-3 generated transport, Rust backend changes, C descriptor header, and test-only binding provider.

## Recovery

Use a separate worktree based on the exact base commit. Do not apply this patch to another source revision or over local changes.

```sh
git worktree add --detach ../chibi-foreign-wip 0545ef70f7e7610a89346925091dc4d802f6e803
# From the checkout containing this checkpoint:
git -C ../chibi-foreign-wip apply --check "$PWD/tools/experiments/vnext/checkpoints/foreign-wip-20261104/foreign-call.patch"
git -C ../chibi-foreign-wip apply --index "$PWD/tools/experiments/vnext/checkpoints/foreign-wip-20261104/foreign-call.patch"
git -C ../chibi-foreign-wip write-tree
```

The resulting tree must be `ddc36e1a46eff75597adf53647154c1b67c72db9`. The patch SHA256 is `780516788e5cc0534a1bc8c6307e7f0078c0090785f832fa1fe9b808edf1e2c2`. Recovery was checked using a fresh detached worktree, including the resulting file hashes; applying does not compile or execute the contents.

## What is preserved

- Typed source handling for the unchanged original foreign fixture: six foreign declarations, five definitions, inferred initialized locals, discarded calls, i64 equality, and branch-based short circuit. Imported functions have scalar signatures and no bodies. Names and diagnostic origins come from actual source.
- A sparse foreign-membership table and explicit version-3 transport. Older transport versions are rejected rather than silently reinterpreted.
- A proposed 64-byte process-local descriptor ABI. The backend resolves exact library/symbol/signature bindings before constructing output, shares imported-call lowering between Object and JIT, and prevents an unregistered JIT lookup from falling back to process symbol search.
- Test-only metadata for the existing, unchanged C probe implementation. The adapter does not replace its code with a mock.

## Validation actually completed

- The complete, unfiltered pure typed-source suite compiled and passed on the six exact typed-source files in this patch. The final observed test execution was 0.433 seconds, with empty output streams, successful input checks and clean child-process cleanup. The compile took 14.049 seconds. These are correctness-run durations, not compiler throughput measurements.
- Ten Python generator/reference tests passed after the transport update.
- Earlier preflight failures are retained in the evidence summary: reserved local names, then mutation of an immutable loop binding. The final pure suite includes their corrections; it is not a claim those earlier attempts passed.
- `git diff --check` passed on the backend source checkpoint. Publication review verifies recovery and disclosure, not full engineering or runtime acceptance.

The evidence summary provides hashes of the retained local receipts. Those hashes identify evidence; they are not downloadable CI artifacts, and the receipt bodies are not included here.

## Incomplete and untested

**Do not treat the applied patch as a working end-to-end native feature.** The backend entry point now expects eight arguments, but the native Chibi caller/preparer still supplies the old six-argument interface. The caller, registry selection, missing-symbol native rejection oracle, and same-archive JIT/AOT linkage have not been updated. The previously tested version-2 backend binary must not be used with version-3 transport.

The new Rust backend, C adapter/header and Chibi/Rust packing tests have not been compiled or executed. The full nine-pass engineering review is incomplete. No foreign JIT/AOT result, hosted CI pass, full original-corpus acceptance, cross-platform acceptance, self-hosting result, merge readiness, or representative speed improvement is claimed.

## Resume order

1. Independently review and compile the frozen packing/backend/header source. Preserve bounded execution and failed-attempt evidence.
2. Finish the native caller and exact original-registry integration. Resolve the same real C archive in both JIT and AOT; retain all existing ownership, profiling-failure and diagnostic checks.
3. Run the original foreign calls and missing-symbol rejection with exact source and tool identities. Re-run prior scalar, direct-call and package cases under the changed ABI.
4. Complete all engineering review passes and focused corrections before integrating this recovery patch into active code.

Production compiler paths, dependency and seed pins, releases, existing branch protections, and the 100,000 physical LOC/second full-rebuild target are unchanged. Existing full-build measurements remain in the earlier vNext evidence; this checkpoint supplies no new speed result.

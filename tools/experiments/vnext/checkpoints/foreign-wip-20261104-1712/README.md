# Foreign-call recovery checkpoint, 2026-11-04 17:12 UTC

This is **inactive, incomplete work in progress**, not an integrated feature or a merge-ready change. The four files here preserve the latest 23-file source/test/documentation patch without changing active compiler or experiment behavior. The earlier recovery checkpoint remains intact.

## Recovery

The exact base is `abf99e96c45ec225e3f55d08aa3ef0ed340a95ef`. Recover into a separate clean worktree, not over local edits:

```sh
git worktree add --detach ../chibi-foreign-wip-1712 abf99e96c45ec225e3f55d08aa3ef0ed340a95ef
git -C ../chibi-foreign-wip-1712 apply --check "$PWD/tools/experiments/vnext/checkpoints/foreign-wip-20261104-1712/foreign-call.patch"
git -C ../chibi-foreign-wip-1712 apply --index "$PWD/tools/experiments/vnext/checkpoints/foreign-wip-20261104-1712/foreign-call.patch"
git -C ../chibi-foreign-wip-1712 write-tree
```

The resulting tree must be `7724b15f75f56e61c01f3df9ae0d1a6ba4d5c844`. The patch is 202811 bytes, SHA256 `f55a74f4eb079c02c4313f8a822146efed2764a37723e09e089fc3ca4221317e`. `manifest.json` lists every resulting file. Recovery is verified without compiling or executing the contents.

## What changed since the earlier recovery checkpoint

- The native caller and preparation/runner path now supply the eight-argument foreign ABI, exact library/symbol/signature descriptors, the original registry cases and the same real C archive for JIT-caller and AOT linkage.
- Missing-symbol rejection has an independently specified document/span oracle. Archive paths reject unsupported escaped/control characters before use.
- Call argument reservation removes repeated whole-prefix resizing; scalar edge construction removes two temporary singleton arrays. These are source-based work reductions, not a measured throughput improvement.
- Pack returns its three buffers directly under the existing verified-Module precondition. All thirteen capacity checks remain as assertions; seven unreachable error alternatives are removed, with substantive copy, COW, poisoning and owner tests retained.
- Invalid foreign descriptor pointer/count messages now identify the binding index and field. Seven new exact-message cases extend the existing owned-failure test. This final Rust correction is not yet compiled or tested.

## Evidence and limits

The prior exact Rust source passed all seven unfiltered unit tests, including explicit foreign resolution, lifetime of borrowed metadata, blocked fallback lookup and JIT mapping cleanup. Its non-test dev-profile library also built and passed read-only artifact inspection with eight C exports and known loader dependencies. That evidence **does not apply to the final diagnostic correction in this patch**. It is not original-source foreign integration or a speed result.

The unchanged original six-function C fixture and descriptor adapter were compiled into an independently inspected deterministic archive. No generated program executed in that archive proof. Eleven generator tests and 28 native-preparation data-only tests passed after the Pack/caller correction.

The latest typed test fixture, call-span changes and direct-return Pack still need fresh native compilation and complete pure-suite execution. An announced successor compile did not start: no invocation/attempt, evidence or output was observed before the work was paused for preservation. All earlier failed attempts remain distinguished from successful evidence.

Four ordered engineering review passes are complete, including their focused corrections; the full review series is not. `evidence.json` provides source/artifact/receipt hashes and explicit limits. These hashes identify retained evidence, not hosted CI artifacts.

## Next gates

1. Compile and execute the fresh complete typed-source and packing suites.
2. Finish the remaining engineering passes and retest/rebuild the exact corrected Rust source.
3. Admit the actual new library and run the original foreign source through Chibi production, JIT and linked AOT using the same real C archive. Retain missing-symbol, profiling-failure, copy/poisoning and owner-lifetime checks.
4. Re-run the prior scalar, direct-call and package cases under the changed ABI, then publish a reviewed active integration checkpoint.

No hosted CI, full original-corpus, cross-platform, self-hosting, merge-readiness or 100,000 physical LOC/second result is claimed. Production defaults, seed/dependency pins, protections and releases are unchanged.

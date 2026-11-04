# New 14f7 intermediate compiler built and validated

## Result

The user confirmed the historical package path does not exist and requested a new build. The verified retained 7482 compiler built exact 14f7 source successfully in a separate cloud output tree. This was an isolated source build, not a restart of the unresolved c5 chain.

- Source: `14f7a46a047ab92acfe58398b3cdac25ab7d0312`, tree `342ada78280eaecde5a5a8ffa0845a74a729926d`.
- All 3,400 tracked blobs and executable/symlink modes stayed unchanged.
- Stage2/3/4: 80.260, 85.990 and 85.648 seconds, all exit 0.
- Raw stage3/stage4 bytes compare equal, without normalization. Both are 10,633,736 bytes, SHA-256 `0a43d0a2b91f6c5e36afc8ba1ea586fe938d6fbb39b687acb3e3ee442bcab7dc`.
- All **485 expected compiler tests passed** in 48.030 seconds. The exact declaration census and PASS-name set match, with no missing, extra or duplicate tests and the summary `485 tests, 0 failed`.
- Total measured build/test phases: 299.928 seconds, approximately five minutes. Every phase terminated cleanly; source, toolchain and linker-input fences passed.

The parent independently inspected the fixed-point and test receipts before continued use. This is not a claim that every controller failure path received independent code review.

## Current-source compatibility

The new compiler successfully emitted the current scalar LLVM closure in **3.782 seconds**: 32 files / 1,122,111 bytes of input, producing 26,428,976 bytes of LLVM text with SHA-256 `f2300fc3354cd7d39186b0f2e7adc40ca7cd49f3210794b22941268726beca22`.

This is the identical source closure that c5 and the retrieved 7482 compiler rejected at `ValueReach`'s `build^.used.maps.append(type);`. No generated output was executed in this compatibility probe. It removes that observed bridge-compatibility blocker, but is not a full current-source compiler proof.

## Bounds and phase diagnosis

Each build had a 120-second deadline. The compiler test phase had a separate 180-second deadline. Builds used jobs 2. The unmodified test driver uses the detected 9 LLVM compile jobs and 2 test-execution workers; transient two-core process affinity bounded CPU for both builds and tests. No compiler source or persistent, system-wide setting was altered to impose this limit.

Owned-process and output-file observations were recorded once per second. Stage2 and stage3 first showed external LLVM work and 57 LLVM pieces at 14.218 and 14.183 seconds; stage4 did so at 15.217 seconds. Stage2 first showed linking at 79.835 seconds. Most elapsed time in these successful builds was after LLVM work began. These are coarse phase observations, not an internal frontend profile, a quiet benchmark, or an explanation of the earlier 90-minute c5 attempt.

The first wrapper attempt failed before launching any native compiler because it named an unavailable Node path. Its failure receipt is preserved. A fresh attempt used the already recorded Node executable and verified its hash; no installation was needed.

The ordinary 485-test source census included no identifiable union leak investigation. The separately restricted 16-byte union leak investigation was not invoked or rerouted, and no claim is made that its restriction cleared.

## Preserved proof and remaining scope

A new local Linux package contains the executable, exact source archive/inventory, all three generated compiler binaries, raw accepted pair, complete test log, phase receipts, controller/helper identities, and the current-source compatibility evidence. A read-only verifier checks archive contents, Git blob IDs, raw binary equality and all 485 PASS names.

- Proof archive SHA-256: `224c23713d465ab0bfa68d941f62cf1092497c4e1187742d6d02aee971ded7b1`.
- Source archive SHA-256: `ee8a44d5b39d6efa597d94a4e47d9edb7741898017ab318e4bdd218170075731`.
- Summary: [14f7-rebuild-results.json](14f7-rebuild-results.json).

This is a **new independently rebuilt Linux intermediate**, not restoration of the unavailable historical package. No seed pin, release, Windows producer acceptance, failed historical CI status, PR hold or merge state changed. The historical c5 process outcome remains unknown; it was neither signaled nor modified. The next separate step is exact current-source compiler validation, followed by the remaining branch integration and platform gates.

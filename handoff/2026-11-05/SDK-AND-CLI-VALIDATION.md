# SDK installation accepted; CLI diagnostic failed

## Full SDK installation suite: accepted

The complete unchanged `graph-sdk-install` selector passed on frozen source `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`. Independent actual-run review accepted the original 13 cases and 96 assertion tokens, with 30 CompilerTools calls and two Install calls. External terminal, native selector and worker/root statuses were zero. Owner elapsed time was 160.65581034599745 seconds, within the unchanged 270-second total and 10-second cleanup reserve. Native stdout/stderr were empty. The owner log contained only the expected Git-isolation probe diagnostic, not a compiler diagnostic.

Whole-owner ECHILD and capture EOF were established, with no remaining children, owner signals, owner errors or cancellation. Independent review checked 10,691 file facts and 1,850 absences. Frozen source, products and resources remained exact. Exactly six native leaves were quarantined to 0600; only the expected mode and monotonic ctime changes were accepted. All 75 declared target outcomes were checked.

Four genuine retained caches (Run, CompilerTools, Install and Libraries) retain their original program, LLVM IR and receipt. Receipt keys, source/compiler/runtime bindings, output hashes and libc-only x86-64 ELF closure were verified. Quarantined programs are **not warm executable caches**. Authored transient fixtures were deleted by the original cleanup, so individual per-case retained ELF evidence is unavailable. Acceptance rests on the whole unchanged selector's success plus retained outputs and containment evidence.

SDK acceptance SHA256: `65e8bb3bb655f31fd1c3973a792c36dd424145ab95c2855101ecf8048a4cfb78`.

## CLI r4 diagnostic: failed, preserved

The full diagnostic used source `15e262296e60b56a91317cea094d65d0b9683345`, tree `a74761a0cb1b67e9789d29ca905675b151811017`. External terminal and native capture exited 1; worker/root raw wait status was 256. Owner elapsed time was 166.6222782420009 seconds, within the unchanged 930-second total and 30-second cleanup reserve. Original assertions and deadlines were not weakened.

The complete ChibiCliTests selector and parent-only INT/TERM and ignored-signal descendant group passed. PTY cases 0–3 completed. Case 4, redirected stdin plus suspend/resume, failed the original five-second assertion that the CLI finish after its terminal child. Case 5 was never reached; there is no final whole-foreground success marker. The earlier r3 failure remains failed. This consumed diagnostic is not a successful CLI gate or an implied rerun.

The failure snapshot records identity-stable CLI and foreground-child processes both stopped in `do_signal_stop`, with SIGINT pending on the child and none shared-pending on the CLI. This is an observation, **not a proven root cause**. Essential identities were checked, but optional child-list reads returned ENOENT, so the diagnostic does not establish a full descendant census. Diagnostic timing is cooperative, not a hard proc-read/write interruption guarantee.

Whole-owner ECHILD and capture EOF were reached with no survivors, owner signals, owner errors or cancellation. Authored fixture cleanup did signal and reap its processes; an empty owner-signal list does not mean no signals occurred. All 45 finalizer targets were checked, including four native/launcher leaves quarantined to 0600. Independent failure review found no mismatches across 9,935 file-fact checks and verified the current 4,478-file/1,668-directory inventory. The retained graph CLI cache has genuine receipt/source/output bindings and libc-only ELF closure; it is not warm after quarantine.

There is no `cli-result.json`, and owner `post_quiescence` is null. Finalizer receipt matching is not a successful-worker snapshot comparison or full successful post-fence acceptance. Current private source/product/launcher bytes and declared modes are verified, and original supplier stat identity remains intact. Pre-run private per-file metadata snapshots existed only in worker memory and were not persisted after failure, so historical private per-file metadata immutability cannot be reconstructed.

CLI failure-review SHA256: `b4ae3a59b31160f5369ebdcf6981d30106224a8220c6436e764a6ae16fb3c687`.

## Remaining gates and unchanged holds

These actual outcomes supersede earlier pending SDK/CLI descriptions, without changing earlier failed attempts or accepting full current-head integration. The [earlier CLI r3 failure](CLI-AND-C-PREFLIGHT.md) and [separate C-object evidence](C-OBJECT-VALIDATION.md) remain scoped historical records. Separate C evidence does not establish all 88 C files compiled in one integrated build.

The approved LLVM alternative supersedes the GNU/libdep provenance blocker for the planned archive route; the GNU route remains unvalidated. The [investigation checkpoint](CLI-JOB-CONTROL-INVESTIGATION.md#additional-validation-checkpoint) records the checked package/runtime evidence, accepted focused regressions for the root-directory correction, resource-contract clarification and separately scoped control results.

The 16 C-project archives, linking and host runtime remain unrun. Actual Windows validation, full current-head integration, ordinary VM/SLJIT/native differential and excluded native 16-byte union/corpus acceptance are not established. PR #1331, terminal-unknown c5 bootstrap and stronger per-Capture descendant proof remain held or unproven. No merge or release readiness is claimed.

This documentation-only checkpoint publishes no binaries, private workspace paths or raw proof archives. The adjacent [JSON](sdk-cli-validation-results.json) records exact scoped outcomes and limitations.

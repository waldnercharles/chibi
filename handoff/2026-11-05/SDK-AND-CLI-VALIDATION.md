# SDK, LLVM archives and C-host link accepted; CLI gate remains failed

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

## LLVM archive route: accepted

The approved LLVM `ar`/`ranlib` route completed 32 genuine commands with exit zero, producing 16 archives from the 87 previously accepted objects: 87 members, 898 indexed symbols and 2,061,156 archive bytes. Independent review reconstructed the required deterministic archive bytes from the original objects and their ELF-defined global/weak symbols. All 32 immutable snapshots and 16 live archives matched, including member order, symbol indexes, offsets and padding; each genuine ranlib step preserved the archive identity and digest.

Whole-owner ECHILD and native stream EOF checks reconciled with terminal completion, with no remaining native work, timeout or cleanup failure. Current original objects, source inputs and declared runtime bindings remained unchanged. This accepts the LLVM archive route only, not execution of GNU ar/ranlib, C-host linking/runtime, a full integrated build or a CLI pass. Earlier timestamp and provenance qualifications are not retroactively upgraded.

## Instrumented CLI diagnostic: scoped case-4 pass

The first instrumented diagnostic (r1, distinct from full-suite r4 above) failed during construction of the genuine native CLI, A. Its private StopTrace counter used the reserved identifier `sequence`, causing a parser rejection before B/C construction, foreground interaction or trace acquisition. Independent review reconciled the failed terminal, cleanup, retained inputs and builder quarantine. This attempt supplies no new original-case-4 outcome and does not identify the cause of the original terminal failure.

The successor renames that counter and its five uses to `trace_sequence`, preserving the trace layout and original assertions, signals and deadlines. The corrected r8 source was exercised by enabled-r2: genuine A construction and the isolated instrumented original case-4 assertions passed, with external and fixture exits zero. Independent review reconciled 4,466 immutable inputs, 4,487 retained files and whole-owner ECHILD/EOF with no survivors or owner errors/signals. Owner elapsed time was 122.57693434201065 seconds.

The 25 valid trace records provide a positive witness of stop, terminal reclaim, self-stop, resume, terminal restoration, group continuation and SIGINT/exit-130 handling in this run. They do not establish the cause of the prior uninstrumented failure or a globally loss-free trace; missing events prove nothing. B/C backing images were sampled before unlinking and cannot now be independently rehashed. Instrumentation changes scheduling. The rename is validated as a correction to diagnostic construction, not a demonstrated product repair or full CLI pass. Original full-suite case 4 remains FAILED and case 5 remains NOT_RUN. The next uninstrumented reproduction is only in preparation.

CLI scoped acceptance SHA256: `cc7aa2d39b4b89e7c2e47c8991b82bbaf7204f0c782379b421b8b243922e7f7b`.

## C-host link preflight: print-only accepted

One GCC 14.2 `-###` response-file invocation exited zero and passed independent preflight review. The printed driver/tool, CRT and library-search expansion matched the checked installed candidates; source/staging/response checks and whole-owner ECHILD/EOF reconciled, with no generated host output. No linker subprocess ran. The printed collect2 command used a temporary nested response whose contents were not observed; the original response bytes and operand order were separately verified. Printed operands are not proof of actual library resolution or plugin loading.

This accepts print-only preflight, not actual linking, generated ELF, host runtime or Graph integration. Installed libdep package provenance remains unresolved. Preflight acceptance SHA256: `61fccbda7d07a3b61a39812130b981ff1442d29bc1b2c17b617618058036eced`.

## C-host link and static ELF: accepted

The unchanged original GCC response-file link, using the accepted LLVM archives, completed once with exit zero and no diagnostics. Independent actual-evidence and static-ELF review accepted the 1,337,048-byte host, SHA256 `e2b679a01af041a2077cf9ef5433063ea2ab876f1f09a9d655e26f61a94552c0`. Native capture took 0.09234945800562855 seconds and the owner phase took 0.1344935029919725 seconds. Whole-owner ECHILD/EOF, unchanged input/response checks and final quarantine to non-executable mode 0600 reconciled, with no survivors or cleanup errors.

The output is ELF64 little-endian x86-64 PIE, with exactly libm.so.6 and libc.so.6 as direct dependencies, no RPATH/RUNPATH or TEXTREL, no writable-executable LOAD segment and a non-executable stack. All 99 strong undefined imports and both COPY imports have compatible versioned providers in the checked installed candidates. GLIBC_2.38 imports prevent claiming compatibility with older glibc. Static compatibility is not observed runtime loading or initializer execution.

The host has not executed. A narrow `--target-json` startup is only in preparation. This is accepted link/static evidence, not startup, authored Graph/C.Link execution, a full current-head build or bootstrap recovery. Separate source/object history and unresolved installed-libdep package provenance remain qualified; actual libdep loading is not established. Link/static acceptance SHA256: `c46ee6e0c2bf8774b60b3698cf7a9f0b61659eada663620047d8819bdf6de21f`.

## Remaining gates and unchanged holds

These actual outcomes supersede earlier pending SDK/CLI descriptions, without changing earlier failed attempts or accepting full current-head integration. The [earlier CLI r3 failure](CLI-AND-C-PREFLIGHT.md) and [separate C-object evidence](C-OBJECT-VALIDATION.md) remain scoped historical records. Separate C evidence does not establish all 88 C files compiled in one integrated build.

The approved LLVM alternative supersedes the GNU/libdep provenance blocker for the accepted LLVM archive route; the GNU archive route remains unvalidated. The [investigation checkpoint](CLI-JOB-CONTROL-INVESTIGATION.md#additional-validation-checkpoint) records the checked package/runtime evidence, accepted focused regressions for the root-directory correction, resource-contract clarification and separately scoped control results.

The 16 LLVM archives and subsequent C-host link/static ELF are accepted separately; host startup/runtime remain unvalidated. Actual Windows validation, full current-head integration, ordinary VM/SLJIT/native differential and excluded native 16-byte union/corpus acceptance are not established. PR #1331, terminal-unknown c5 bootstrap and stronger per-Capture descendant proof remain held or unproven. No merge or release readiness is claimed.

This documentation-only checkpoint publishes no binaries, private workspace paths or raw proof archives. The adjacent [JSON](sdk-cli-validation-results.json) records exact scoped outcomes and limitations.

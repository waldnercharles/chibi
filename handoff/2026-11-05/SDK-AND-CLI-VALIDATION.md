# SDK and scoped build artifacts accepted; CLI gate remains failed

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

The 25 valid trace records provide a positive witness of stop, terminal reclaim, self-stop, resume, terminal restoration, group continuation and SIGINT/exit-130 handling in this run. They do not establish the cause of the prior uninstrumented failure or a globally loss-free trace; missing events prove nothing. B/C backing images were sampled before unlinking and cannot now be independently rehashed. Instrumentation changes scheduling. The rename is validated as a correction to diagnostic construction, not a demonstrated product repair or full CLI pass. Original full-suite case 4 remains FAILED and case 5 remains NOT_RUN. The subsequent uninstrumented sample reproduced the failure, as recorded below.

CLI scoped acceptance SHA256: `cc7aa2d39b4b89e7c2e47c8991b82bbaf7204f0c782379b421b8b243922e7f7b`.

## C-host link preflight: print-only accepted

One GCC 14.2 `-###` response-file invocation exited zero and passed independent preflight review. The printed driver/tool, CRT and library-search expansion matched the checked installed candidates; source/staging/response checks and whole-owner ECHILD/EOF reconciled, with no generated host output. No linker subprocess ran. The printed collect2 command used a temporary nested response whose contents were not observed; the original response bytes and operand order were separately verified. Printed operands are not proof of actual library resolution or plugin loading.

This accepts print-only preflight, not actual linking, generated ELF, host runtime or Graph integration. Installed libdep package provenance remains unresolved. Preflight acceptance SHA256: `61fccbda7d07a3b61a39812130b981ff1442d29bc1b2c17b617618058036eced`.

## C-host link and static ELF: accepted

The unchanged original GCC response-file link, using the accepted LLVM archives, completed once with exit zero and no diagnostics. Independent actual-evidence and static-ELF review accepted the 1,337,048-byte host, SHA256 `e2b679a01af041a2077cf9ef5433063ea2ab876f1f09a9d655e26f61a94552c0`. Native capture took 0.09234945800562855 seconds and the owner phase took 0.1344935029919725 seconds. Whole-owner ECHILD/EOF, unchanged input/response checks and final quarantine to non-executable mode 0600 reconciled, with no survivors or cleanup errors.

The output is ELF64 little-endian x86-64 PIE, with exactly libm.so.6 and libc.so.6 as direct dependencies, no RPATH/RUNPATH or TEXTREL, no writable-executable LOAD segment and a non-executable stack. All 99 strong undefined imports and both COPY imports have compatible versioned providers in the checked installed candidates. GLIBC_2.38 imports prevent claiming compatibility with older glibc. Static compatibility is not observed runtime loading or initializer execution.

The host was not executed during link/static review; separately accepted startup evidence is recorded below. Link/static evidence does not establish authored Graph/C.Link execution, a full current-head build or bootstrap recovery. Separate source/object history and unresolved installed-libdep package provenance remain qualified; actual libdep loading is not established. Link/static acceptance SHA256: `c46ee6e0c2bf8774b60b3698cf7a9f0b61659eada663620047d8819bdf6de21f`.

## C-host startup and target serialization: accepted

One fresh private copy of the accepted host completed `--target-json` with exit zero and empty stderr. Its complete 10,371-byte stdout matched all six source-derived components under type-preserving comparison: 21 layout integers, the prepared ABI, 121 ordered instruction names, 29 native declarations, 82 ports and 58 native ID/ABI words. The expectation was fixed before execution. Owner elapsed time was 0.046376340003916994 seconds; ECHILD/EOF, no remaining children and quarantine of the executed copy to 0600 were independently reconciled. The original host stayed non-executable.

This accepts startup and exact target serialization only, not bootstrap, seed loading, compilation, VM/SLJIT parity or full recovery. The separate bootstrap `--validate` result is recorded below. Existing source/object, installed-libdep provenance and runtime-observation qualifications remain unchanged. Startup acceptance SHA256: `d721eb83d621eb60e521c4fa9a5e5544f511dea2e5dd8180076f50002e1f418e`.

## C-host bootstrap validation: accepted

One fresh private host copy completed `--validate` with exit zero, empty stderr and exactly `the artifact admits the sources; 61 changed since it was written` followed by a newline (65 bytes). The 61 is an ordinal/path/hash comparison against the older manifest, not a count of semantic edits. Native elapsed time was 2.6390313290030463 seconds and owner elapsed time was 2.814429744001245 seconds. Independent review reconciled all 66 original/copy pairs, the exact 66-file/30-directory private tree, 28 bound evidence/source/runtime identities, ECHILD/EOF and no remaining children. Original inputs remained unchanged; the executed host copy returned to 0600.

This accepts seed admission, prepared decoding/verification and default-SLJIT function emission only. It does not invoke `CompileClosure` or establish stage-one execution, fixed-point recovery, current-source compilation, Graph integration or VM/SLJIT parity. The separate direct fixed-point recovery result is recorded below. Existing source/object, installed-libdep provenance and runtime-observation qualifications remain unchanged. Bootstrap-validation acceptance SHA256: `06a08d1db9cb4c0abb0e8629666f92e32ef80f53ed12c996603331237c3afd61`.

## Direct C-host fixed-point recovery: accepted

The reviewed direct default-SLJIT recovery of 61 exact compiler sources completed with exit zero, exact 265-byte stdout and empty stderr. The complete source-defined diagnostic reports “after a second compiler”: the seed produced a first compiler, that compiler produced different second bytes, and the second produced identical third bytes. This establishes three `CompileClosure` invocations and second/third byte equality through the reviewed source path, not an independent runtime invocation trace. The retained second compiler is a 15,590,690-byte closure, SHA256 `f91a24856b95f80223ef7621d42c132eec27d5b008f303c2dad2ee5ffb2e3bd4`.

Native elapsed time was 417.58848103099444 seconds; owner elapsed time was 417.88663534799707 seconds. Independent review reconciled the retained output, all 66 original/private input pairs and 44 evidence/source/runtime bindings, ECHILD/EOF, no remaining children, signals, errors or cancellation, and host-copy quarantine to 0600. This is exact-source fixed-point recovery, not a registered `test_tools` selector pass, full CLI integration, current-head Graph build, backend parity or Windows acceptance. Existing provenance qualifications and holds remain unchanged; no additional retained-closure invocation occurred. Recovery acceptance SHA256: `de7781d23db5377a4214f1fb0c7cd1b8341f5147a83c20c022284fbee82b8deb`.

## Uninstrumented isolated case-4 sample: failed

The three-iteration sample stopped on its first failure: iteration 1 PASSED, iteration 2 FAILED at the original five-second CLI completion assertion after terminal Ctrl-C, and iteration 3 was NOT_RUN. Iteration 2 reached resumed input and foreground ownership checks; its exit-130 and final no-live-group assertions were not reached. Identity-preserving samples found A and B stopped, with SIGINT pending on B. They do not identify the stopping signal, event order or cause, and are not a complete descendant census.

Independent review reconciled the failure and cleanup: owner elapsed 24.92001563300437 seconds, ECHILD/EOF, no remaining children and all six compiler/CLI copies quarantined to 0600. Fixture cleanup did signal and reap processes; an empty owner-signal list does not mean no signals occurred. The original aggregate case 4 remains FAILED and case 5 NOT_RUN. This isolated failure shows preceding aggregate cases are not necessary for reproduction, but supplies neither a reliability estimate nor a demonstrated fix. The earlier instrumented pass remains separately scoped. See the [source-path distinction and investigation](CLI-JOB-CONTROL-INVESTIGATION.md#uninstrumented-reproduction).

## Repeated instrumented case-4 sample: three scoped passes

Three genuine instrumented original-case-4 interactions passed unchanged assertions and deadlines, including terminal Ctrl-C, exit 130 within five seconds and both no-live-group assertions. Each produced exactly 174 stdout bytes and empty stderr. All three 25-record traces were structurally valid. Iteration 1 lacks accepted C identity/executable evidence; only iterations 2 and 3 have complete sampled A/B/C identities. Sampled B/C backing files were subsequently unlinked and cannot now be independently rehashed. This is not three fully identified traces or globally loss-free observation.

Independent review reconciled owner elapsed time of 31.631693464005366 seconds, ECHILD/EOF, no remaining children and six exact copies quarantined to 0600. Neither the historical stopped-child-result ordering nor an invalid A consume followed by an actual stop was witnessed. Three instrumented passes are finite non-reproduction, not a causal explanation, reliability estimate, repair or full-suite pass. The uninstrumented iteration-2 failure and original aggregate case-4 failure/case-5 NOT_RUN remain unresolved. Repeated-sample acceptance SHA256: `e73fe6bc3ab042b224675ec346f68a9153751cb1edbea32a12fef3197f6ec34f`.

## Current-source test_tools and reflection build: artifacts accepted, aggregate failed

The genuine Graph build used current source `d97d5362e2b368b385b3b5a52abd6dbc2150324a` and the accepted Graph supplier built at `a661626d48172e3bfcda82c9dcde3018edf53eef`, with relevant source equivalence separately verified. It produced 136 C objects across 38 targets, with all 136 exact compile arrays, seven actual link/native rows and 13 intentional `-UNDEBUG` rows preserved. Genuine native compiler, asset compiler/archive, cached build helper, reflection-generated sources, C compiler host, compile tool and full `test_tools` products were independently checked. All nine executable roles were quarantined to 0600.

Native Graph exited zero, but the original outer exit remains 1 and owner status remains failed after 227.85253264100174 seconds. The inventory oracle omitted the empty `src/runtime/component_ids.tsv.lock` deliberately retained by `tools/reflect/reflect.c` to keep its lock inode stable. All 5,869 original source/runtime/vendor/evidence bindings remained unchanged. The failed adapter produced neither a successful build result nor a post-quiescence snapshot. Whole-owner ECHILD/EOF and cleanup were independently reconciled.

A separate data-only review accepted the retained products with exactly that owner-owned, regular, single-link, zero-byte, nonexecutable lock allowed. It did not weaken source hashes, use a general lock exemption, rerun native work or convert the failed aggregate into a pass. This establishes the actual scoped build products, not full integration or selector success. Retained-artifact acceptance SHA256: `d5fed6c97130a41a0e52c0a7697d4b5631c03bfa00d6482d04cd047a71dd0ff2` (7,964 bytes).

## Registered compiler bootstrap: independently accepted

The unchanged registered `compiler-bootstrap` selector completed on Linux with native and outer exits zero, 14 cases, 94 assertions and zero failures. Owner status was `ownership_passed`, with ECHILD/EOF and no survivors, after 14.624869404986384 seconds. Stdout was 1,195 bytes; the 3,900-byte stderr contained expected rejection diagnostics. Independent review verified all 4,406 bound inputs unchanged and both private images quarantined to 0600. This accepts the registered selector only, not recovery or full integration. Acceptance-review SHA256: `f8bcc081b0d50a6569f272cf4905f63e27a9372f5e235e18a56f761c3e134480` (5,008 bytes).

## Registered bootstrap recovery: independently accepted

The unchanged registered `bootstrap-recovery` selector passed both authored cases, `bootstrap_validates_without_recovering` and `bootstrap_recovers_from_a_clean_checkout`, in order: two cases, nine assertions and zero failures. Native, outer and worker statuses were zero. Whole-owner elapsed wall time was 433.2721424449992 seconds, within the separately reserved 720 productive seconds plus 30 cleanup seconds, with the original 600-second inner recovery deadline preserved. Pico prints CPU-clock test times, not child-recovery wall times; no separate child wall-time measurement is retained.

The complete 263-byte stdout matched the authored result. The 158-byte stderr was the expected rejection of a deliberately damaged seed. Independent review reconciled ECHILD/EOF, no survivors, all 4,406 bound inputs unchanged and both private executable copies quarantined to 0600. Tests and assertions remain unmodified.

The recovery case genuinely exported the compiler/core sources from Git HEAD into a clean fixture and required successful recovery, an authored recovery diagnostic and nonempty recovered bytes. Authored cleanup removed the fixture and recovered closure, so no recovered bytes remain for independent inspection. Retained stdout does not identify which allowed recovery diagnostic branch ran. Previously accepted direct fixed-point recovery remains separate evidence; this selector result does not newly establish an independent direct fixed-point comparison or full integration. Acceptance-review SHA256: `4025cfd6f05bb148a34aebaa0441ef7de00ff4fd03039834ca36c6f14f077c74` (6,546 bytes).

## Registered C-runtime selectors: independently accepted

Three unchanged selectors passed in exact authored case order with zero failures: `types`, 42 cases and 5,103 assertions in 1.668269916990539 seconds; `persistent-store`, one case and 46 assertions across bytecode and SLJIT in 1.7730385479953839 seconds; and `box3d`, two cases and 30 assertions in 1.4535320799914189 seconds. These are whole-owner elapsed times.

Each run had native and outer exits zero, empty stderr, ECHILD/EOF and no survivors. Independent review verified all 4,405 bound inputs unchanged and each run's single private executable copy quarantined to 0600. These are scoped selector results, not full integration, native-backend parity or native union/corpus acceptance. Combined acceptance SHA256: `25af814e92f6fb84ac335815bab4485d975a95255f72655dd4e32a7705198e7c` (15,260 bytes).

## Registered bootstrap load: independently accepted

The unchanged `bootstrap-load` selector passed one case and 19 assertions with zero failures, native and outer exits zero, 437 stdout bytes and empty stderr. Whole-owner elapsed time was 145.56350434900378 seconds. Independent review accepted ownership, ECHILD/EOF, no survivors, all 4,405 bound inputs unchanged, the single private executable copy quarantined to 0600 and empty home/tmp directories. The original fixtures, assertions and cooperative deadline of 1,800 work seconds plus 60 cleanup seconds were preserved; this is not a hard whole-command time bound.

The real test exported sources, edited a private copy, recovered the changed-source compiler, executed markers 37 and 41, reused the cache and evicted the oldest of two sparse 1 GiB fixtures. Authored cleanup removed the fixtures and recovered compiler. The diagnostic reported 62 changed sources and a 15,942,750-byte compiler with SHA256 `2ba61ed6066c1ecf9d32aa0a9db9c1f0c04872619bead86a3e60d4345a71d439`; that identity is diagnostic-reported, not an independently rehashed retained artifact. Exact logical peak and physical allocation were not measured. Final cleanup does not establish zero physical allocation or a peak-usage bound.

This accepts the registered bootstrap-load behavior only. Prior direct fixed-point and registered bootstrap/recovery evidence remain separately scoped. Acceptance SHA256: `4e40d352600690536d4c2798d1df963695a41083c21ff001d71cea498962f7a2` (7,907 bytes).

## Registered program SDK: independently accepted

The genuine direct `test_tools --program-sdk` selector passed both cases and all 17 assertions in exact order, with zero failures. Native, outer and worker exits were zero; whole-owner elapsed time was 6.565544757002499 seconds, with 301 stdout bytes and empty stderr. Independent review accepted ownership, ECHILD/EOF, no survivors, all 4,406 bound inputs unchanged and both private executable copies quarantined to 0600. Home/tmp directories were empty.

The unchanged assertions cover independent retained state, hot replacement, invalid compilation with diagnostics while preserving the previous program, and shared host instruction-budget exhaustion. The real asset-compiler service path was used. Three healthy framed cache files of 569, 602 and 721 bytes and an empty lock remained; no temporary residue remained. Authored service streams were redirected to `/dev/null`, so the final cache does not establish an exact service-launch or cache-hit count.

The admitted cooperative limits were 600 productive seconds plus 30 cleanup seconds. This direct selector result does not validate the separate toolchain runner's scheduling implementation or exercise a timeout. It is distinct from the accepted 13-case, 96-assertion `graph-sdk-install` suite. Acceptance SHA256: `2ae4ca5652614a8c595400516b88011a335eab733af18ca5e3b2e55a9208bf25` (1,254 bytes).

Across `compiler-bootstrap`, `bootstrap-recovery`, `types`, `persistent-store`, `box3d`, `bootstrap-load` and `program-sdk`, seven registered `test_tools` selectors are independently accepted: 64 cases and 5,318 assertions. This is scoped coverage, not full-project integration or native-backend parity.

## Remaining gates and unchanged holds

These actual outcomes supersede earlier pending SDK/CLI descriptions, without changing earlier failed attempts or accepting full current-head integration. The [earlier CLI r3 failure](CLI-AND-C-PREFLIGHT.md) and [separate C-object evidence](C-OBJECT-VALIDATION.md) remain scoped historical records. The earlier separate C evidence did not establish an integrated build. The current-source build products above are separately accepted; their original aggregate remains failed.

The approved LLVM alternative supersedes the GNU/libdep provenance blocker for the accepted LLVM archive route; the GNU archive route remains unvalidated. The [investigation checkpoint](CLI-JOB-CONTROL-INVESTIGATION.md#additional-validation-checkpoint) records the checked package/runtime evidence, accepted focused regressions for the root-directory correction, resource-contract clarification and separately scoped control results.

The 16 LLVM archives and subsequent C-host link/static ELF are accepted separately; host startup/target serialization and narrow bootstrap validation are accepted separately. Direct fixed-point recovery is also accepted for its exact sources. Current-source build products are separately accepted. Registered compiler-bootstrap, bootstrap-recovery, bootstrap-load and the three C-runtime selectors above are independently accepted. The separate `program-sdk` selector is also independently accepted, completing the seven-selector scope above. Fresh `test_language` source staging at `3549b60df81b0c149cf247be92e3fa292266d275` is independently accepted (acceptance SHA256: `b9f58e7307e9effdaf33346959972f0c491b002775c48a26192c8b979e23f543`). The subsequent commands-only run independently accepted all 131 complete compile arrays and six link/native arrays, using the accepted Graph supplier built at `a661626d48172e3bfcda82c9dcde3018edf53eef` with relevant source equivalence verified, not a fresh Graph compilation. Outer and native exits were zero; whole-owner time was 1.5271922389947576 seconds, with ECHILD/EOF and successful cleanup. Commands acceptance SHA256: `be5af6d95615620af02763ebcf57682437785322b8632d95178ed2e03725cd68` (6,127 bytes). These are emitted plans: no compiler, reflection, full build or selector execution is established. Broader runtime integration remains unvalidated. Actual Windows validation, full current-head integration, ordinary VM/SLJIT/native differential and excluded native 16-byte union/corpus acceptance are not established. PR #1331, terminal-unknown c5 bootstrap and stronger per-Capture descendant proof remain held or unproven. The next controlled CLI diagnostic remains explicitly held; instrumented samples are not a demonstrated repair. No merge or release readiness is claimed.

This documentation-only checkpoint publishes no binaries, private workspace paths or raw proof archives. The adjacent [JSON](sdk-cli-validation-results.json) records exact scoped outcomes and limitations.

# Native stage worker limits and producer diagnosis

## Correction and validation

Based on takeover checkpoint `02c86287430def964445ec90185961e20319536d`; correction commit `eff3d5ad85b80517596b751645d21d6f75f70d6c`.

`NativeStage` already emits a stage/compiler marker before `os.Capture`. The correction keeps that behavior and forwards one resolved `--jobs` value to Linux stages two, three and four, plus the Windows cross-build. It preserves byte-for-byte fixed-point requirements, error statuses, the one-extra-generation recovery limit, stage-two-only behavior, and all CI time limits. The worker value bounds stage compiler module scheduling; it is not an end-to-end cap on all helper/runner compilation.

Validated on 2026-11-04:

- The verified published c5 seed compiled both the corrected Driver and unchanged checkpoint Driver. Each compile explicitly used jobs 2 under a 119-second deadline plus owned cleanup. Neither invocation compiled the whole compiler.
- The existing native-runner stage-contract block passed with the corrected real Driver and controlled shell stages. This covered explicit jobs 1/2/4, the online-processor default, progress visible before stage completion, preserved status 19, the stage-two-only boundary, Windows argument forwarding, mandatory contract status 7, and opt-in seed failure status 23.
- The unchanged Driver failed the new jobs regression at the first controlled stage. Dry-run checks separately confirmed the corrected three planned stage commands carry `--jobs 4` and the unchanged commands do not.
- Six controlled fixed-point cases passed: missing stage three, missing stage four, failed stage four, mismatch, equal stages and recovered fixed point. Each fake stage required jobs 2. Accepted fake hashes reached the deliberately failing command-contract boundary. These are driver control-flow tests, not compiler fixed-point proofs.
- Twenty-one static workflow/lifetime tests passed, shell syntax checks passed, and `git diff --check` was clean.

The touched fixed-point fixture also needed two pre-existing setup corrections: fingerprintable LLVM placeholder files and the current `CommandTests-Run` runner name instead of old runner paths. Independent source review accepted the corrected four-file patch. No LLVM executable is invoked by the controlled shell stages.

Not run: the full native-runner script, the broader DriverTests suite, a whole-compiler bootstrap, real Windows compilation/execution, or PR merge acceptance. The separate native-comparison fixture still needs review because it retains an old runner assumption. Existing branch and PR holds remain. No performance improvement is inferred from these correctness tests.

Validation identities are in [native-stage-jobs-validation.json](native-stage-jobs-validation.json).

## Bounded producer diagnosis, not armed

The [2026-11-03 GitHub producer run 41627603178](https://github.com/waldnercharles/chibi/actions/runs/41627603178) was cancelled after about 90 minutes. Its first whole-compiler stage never reported completion. The job log has no compiler phase timings; it cannot establish an LLVM bottleneck. Local same-seed/source evidence had no stage-two files, suggesting work before external LLVM but not identifying a frontend function.

## Entry gates

1. Reconcile the separate lost bootstrap run through its original owner/process context or obtain a terminal receipt. Fresh command PID contexts do not prove it stopped. Do not start this proposed workload until that uncertainty is resolved.
2. Freeze the selected source revision, published c5 seed SHA-256, LLVM tool/runtime closure, explicit jobs 2, command, controller, output directory and ownership policy. Independent review must cover those exact inputs and the bounded diagnostic before launch.
3. Give it one reserved output directory and one attempt. Do not change or reuse the lost run, run a whole fixed-point sequence, execute runtime fixtures or auto-retry. Keep vNext's CPU window separate.

## First diagnostic: observe unchanged seed

Run only the seed-to-first-compiler stage for at most 180 seconds including owned cleanup, not the whole producer workflow. The objective is evidence, not completion. Record start/end UTC and monotonic times, exact argv, tool/source hashes and terminal return code; a timeout remains a failed/incomplete build.

The controller and sampler must inhabit the same process context as the compiler. Once each second record owned PID, parent/group identity and process start time, CPU time, RSS, child executable/argv, and counts/bytes/names of emitted .ll and .o files. Capture stdout/stderr continuously to durable files and retain them after failure/cancellation. Teardown targets only identified owned descendants; a failed observation or uncertain cleanup prevents another attempt. Retain a final process and output inventory.

The unchanged seed cannot acquire internal phase logging merely by editing the compiler source it is compiling. If an already-permitted profiler can sample the exact seed, collect a bounded symbolic CPU profile alongside these observations. If profiling is unavailable, report that limitation, not an inferred parse/check/lower hotspot.

Expected separation: .ll files are written before native_link_host.ObjectsResult launches opt/llc; object completion precedes LinkResult. Child observations and file inventories distinguish frontend/module emission, external LLVM and linking without replacing the backend. Presence or absence of an output is supporting evidence, not a complete phase trace.

## Internal phase instrumentation, only after a usable compiler is established

On an isolated instrumented compiler build, add one timestamped entry/exit event per compilation boundary: Gather; package collection/parsing/type and scalar-row construction; scalar LLVM text emission; module writes; opt/llc; linker. Record source byte/file counts, facts-row count, module count/bytes and exact tool invocations. Keep markers outside node/row loops; preserve diagnostics, exit status, input identity and all limits. The scalar compiler currently combines multiple frontend operations, so name combined phases honestly until finer boundaries are instrumented.

Logging must come from the compiler executable doing the work, and must reach an inherited diagnostic stream or explicitly flushed phase file. NativeStage's current os.Capture buffers child output. Its existing pre-capture stage marker is useful but does not make internal child messages live. Do not claim a new compiler source log is visible in the old c5 seed.

## Decision and stop

Stop at success, first rejection, budget exhaustion, cancellation or uncertain cleanup. Return the raw observations and phase evidence. Only then choose whether an algorithm/profile investigation, a validated bootstrap bridge or backend code-generation work addresses the measured cost. Do not raise the 90-minute budget or claim Cranelift is a remedy based on this cancelled run alone.

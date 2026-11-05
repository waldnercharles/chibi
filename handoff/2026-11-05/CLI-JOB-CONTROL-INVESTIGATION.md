# CLI stop/resume investigation

This is a source-investigation checkpoint, not a runtime repair or a passing CLI gate. The [SDK/CLI validation record](SDK-AND-CLI-VALIDATION.md) remains authoritative for the completed runs. The SDK suite passed; CLI r4 failed terminal case 4, and the earlier r3 failure remains failed. No failed assertion or deadline has been relaxed.

## What the failure establishes

Case 4 redirects standard input, suspends the foreground terminal group, resumes the CLI once, verifies resumed terminal input and foreground ownership, then sends terminal Ctrl-C. It failed the original five-second completion assertion. The final observations identified both the CLI and its foreground program wrapper as stopped. The wrapper had a pending SIGINT.

Those observations do not identify which stop instruction or signal produced the final state. They are successive identity-checked samples, not an atomic complete process-tree trace. The nested terminal child was not observed in that snapshot. Pending SIGINT is not evidence that it caused the earlier stop. Cases 0–3 completed; case 5 was not reached.

## The implementation actually exercised

The source basis is commit `15e262296e60b56a91317cea094d65d0b9683345`. The relevant compiler and POSIX source bytes are unchanged in the subsequent documentation checkpoints.

The native CLI's `RunChild` calls `core:sys/posix.ForegroundStart` and `ForegroundWait` through the authored Chibi implementation. It does not use the similarly named C process implementation for this path.

There are three native participants:

1. A, the CLI, owns terminal handoff to a separate foreground process group.
2. B, the generated terminal program, runs the terminal fixture through ordinary `os.Run`.
3. C, that fixture, shares B's process group and performs terminal input/output. Its source does not issue stop or continue signals.

Two distinct wait paths can mirror stops:

- Ordinary `Posix.Wait` uses `waitpid(..., WUNTRACED)`. On a returned child-stop status, it stops its caller. After that caller resumes, it continues the child PID.
- `ForegroundWait` peeks B's status, consumes a stop, reclaims the terminal, and stops A. After A resumes, it restores B's foreground ownership and continues B's group.

`os.Wait` is a separate asynchronous `WaitFor` API; it is not ordinary `Posix.Wait`. `os.Run`, direct low-level Spawn/Wait, and Capture's final wait reach the ordinary path. Capture drains its output pipes before that final wait, so this source audit does not establish prompt stop propagation while a stopped child still holds those pipes open.

## Two source questions, neither a proven cause

A possible schedule is that B receives C's stopped status, is stopped by terminal group job control, resumes, then acts on its already-returned status by stopping itself again. A can mirror that second stop. This hypothesis concerns a result already returned to user space; it does not require an unread kernel notification to survive SIGCONT. The failed snapshot does not establish that ordering.

Separately, `ForegroundWait` ignores the consuming waitid call's return value and returned PID/code, then acts on its earlier peek. That unchecked boundary is directly visible in source. A failed or empty consume would not justify acting on the stale peek. Checking consumption is a narrow hardening candidate, but it is not a demonstrated repair of r4 and has no completed real old-fails/new-passes regression. A valid returned stop can also become historical before a later self-stop, so consumption checks alone do not serialize asynchronous continuation.

No production patch has been applied from either hypothesis.

## Preserve the ordinary-wait contract

Ordinary Wait explicitly promises that a child stopped by job control stops its caller, and that resuming the caller resumes that child. Removing propagation merely because B/C share a process group would change existing behavior.

A terminal Ctrl-Z normally stops both B and C independently. A direct stop of C alone does not stop B just because they share a group. Existing propagation exposes that isolated stop through B and allows one wrapper resume to continue C. Removing it could leave B waiting for an indefinitely stopped C unless the contract and implementation change together.

The six terminal scenarios include three suspend/resume cases, but the inspected tests contain no identified child-only SIGSTOP regression. Existing end-to-end assertions require a usable single stop/resume cycle; they do not require another stop for every historical returned notification. Any repair must distinguish those obligations rather than deleting one to make the other pass.

## Next discriminating evidence

The prepared diagnostic scope is one original case-4 interaction, with bounded ordered records at the actual A/B wait, self-stop and continue sites. A must be rebuilt as the real native CLI to instrument its own wait implementation. Passing a changed core path to the existing CLI only changes code it subsequently compiles.

A useful positive witness would identify either an invalid consume followed by the corresponding self-stop intent, or a B stop result recorded before A's group continuation followed by B's self-stop intent after that continuation returns. Further genuine stop observations and process identities would be needed to connect that ordering to a later mirrored stop. A BEFORE record alone does not prove that a syscall was issued or delivered. Missing events never prove an event did not happen, and instrumentation changes scheduling.

This is not an aggregate rerun, a loop, a hardening patch or permission for automatic retries. Native construction, diagnostic execution, artifact lifetime evidence and cleanup remain separately gated. A targeted success or non-reproduction would not clear the full CLI gate.

After a demonstrated repair, validation still needs the unchanged complete CLI/terminal suite, real regressions for the repaired boundary and isolated child-stop behavior, and applicable platform validation. The separate archive/link/runtime and other holds in the validation record remain unchanged. No merge or release readiness is claimed.

## Additional validation checkpoint

Further validation work now has separately completed, independently accepted results:

- 77 finite semantic controls covering digest APIs, ELF data, path grammar, schemas and trace interpretation.
- 16 archive pure-control outcomes. Synthetic receipts and predicates do not establish production archive execution or descriptor ownership.
- A standalone read-only proc backing-file access check for a pinned Python child, with observed cleanup. This is not a native generated-image identity proof.
- 24 builtin SHA1/SHA256 and in-memory stream cases. These establish finite correctness checks, not real-file acquisition or large-file hashing throughput.
- 17 ownership/PTY controls: four positive outcomes and thirteen intended negative rejections, including retained pipe and PTY handling. These are diagnostic-support controls, not the original terminal suite.
- Eight log-lifecycle controls: two positive inner cases, five intended rejections and one scalar exception-path/umask-restoration case.
- 32 pure proc-parser controls, accepted from the complete exact combined output and exit 0. These do not establish real proc enumeration or separately captured stdout/stderr.
- Two Python-only serial owner controls, clean and retained-child, both completed with exit 0 and independently accepted. They establish the selected ownership, wait/ECHILD, internal stream completion and retained-child cleanup/rejection behavior, not native archive execution or hard resource guarantees.
- 50 focused Python regression outcomes for the root-directory correction and adjacent ownership, streaming and temporary-file behavior, independently accepted after exit 0. These do not execute native LLVM tools or establish archive acceptance.

These are separate scopes, not a combined product-suite pass. The source-only diagnostic instrumentation and builtin hash replacement preserve the original assertions, signals and deadlines. They do not establish a product fix or full diagnostic integration. CLI case 4 remains failed at its original five-second completion assertion; case 5 remains NOT_RUN.

Real proc controls r1 failed an auxiliary mount-evidence assertion and emitted no complete success result. A later bounded read found two stacked `/proc` mount entries, corroborating the overly strict single-entry assumption on current data. The failed attempt did not retain its mount table, so its historical rows cannot be reconstructed. The attempt remains FAILED.

The corrected r2 attempt was independently accepted for real proc reads, enumeration and closure, including the intended expired-deadline and zero-budget rejections; host-wide visibility and product repair remain unproven.

The approved LLVM archive alternative supersedes the GNU package-provenance blocker for the planned archive route; the GNU route remains unvalidated. Retained official package/member evidence and the selected runtime inventory, including known llvm-symbolizer/libcurl dependencies, have been checked. This does not establish exhaustive runtime behavior, an observed symbolizer invocation or network use. The root-directory verification correction passed the focused regressions above; native archive assembly remains unrun. Archive format/source review still does not establish native archive success or plugin loading.

The earlier hard-resource contract was a validation-design proposal, not an external execution prerequisite. Cooperative limits and explicit cleanup checks remain; no hard OS-resource guarantee is claimed. Genuine restrictions and remaining validation holds are unchanged.

No native compiler, native fixture or archive execution is reported by this checkpoint. The previously accepted SDK suite (13 cases, 96 assertion tokens) and 87-command C compilation result remain historical, separately scoped evidence, not new reruns. The separate text-object record and historical identity qualifications remain unchanged. Full current-head integration, the unchanged complete CLI/terminal suite and the other existing holds remain open; no merge or release readiness is established.

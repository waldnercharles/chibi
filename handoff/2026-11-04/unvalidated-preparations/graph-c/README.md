# Graph-c runtime successor: unarmed source integration

This packet contains a concrete fixed caller/controller, a fresh exact source copy and proposed finite controls. **It is not authorized for compiler, control or graph-c execution.** `plan.json` has `runtime_enabled:false`; independent control and recursive-runtime admission fields are null. No runtime intent, claim, result, control ELF or runtime output was created.

## Fixed shape and source preservation

The intended execution shape is the unchanged outer owner -> `run.py --worker` -> `inner_owner.py` -> descriptor-exec of the separately compiled graph-c ELF. There is no production command override or selectable payload. `graph_c_attribution.py` is a narrow successor of the independently reviewed core, SHA `aeb3759d27e523aeb0257e73790ffce8cca89a1e64feb02290a2442d903914b4`. `CORE-DELTA.md` documents its sole behavioral change: distinguish valid pidfd readiness from invalid/error/unsupported poll results. The originally reviewed core remains unchanged in its original packet; its earlier review does not admit this successor. `outer_owner.py` is the unchanged generic owner, SHA `eb565285001cef43ed88cf73e5797cc83b726afcf6cf192cf5b4ba10f6078698`.

The completed compile packet and its source remain read-only. `source/` is a new copy with identical bytes, modes, symlink targets and path membership: 4,087 regular files, one symlink and 1,532 directories including the root. Its distinct full-width inode/timestamp inventory is `source-baseline.json`. No authored CTests assertion or source was edited. The 20ms cancellation and 0.25-second marker check are unchanged.

The graph-c ELF remains at `/workspace/shared/chibi-graph-c-20261104-revision01/artifacts/graph-c`, 293,456 bytes, SHA `046fabf20b00f394cecf4307c7f1758ff17fad7694b31bc3da9dac889321120f`. Its separate compile/artifact-only admission SHA is `d9a7c2ce6e16da3a3e2eeea94bc15184a975e556fcfe28c562417887d6983e7d`. That admission is pinned as read-only input, not reused as runtime permission.

## Source/cwd and recursive execution evidence

`source-mapping.json` traces `CTests.Run -> os.Root -> sys.Root -> getcwd`; direct ELF section/symbol-byte inspection confirms a getcwd import without executing the ELF or readelf. Native cwd will be this packet's `source/`, not the compile packet. Static evidence is not a claim of observed runtime behavior.

`runner-cache.json` genuinely derives the new FailedStep cache from 87 ordered configuration/tool/source fingerprint lines. Its key is:

`1a9a3d0e7ef68922d833f75ac4ca82c0edf11f06f6e05a4620ed5ac71934013f`

The derivation follows byte-sorted recursive source traversal, canonical tool paths, the native signed-low-i32 POSIX stamp representation, octal mode, nine-digit nanoseconds and newline-joined SHA-256 without a trailing newline. Applying the same metadata calculation to the preserved original paths reproduces all 87 original input lines and the original `ff1e5284...` key exactly. No cache/program was created by preparation.

`runtime-dependencies.json` binds the actual ELF, current native compiler/source equivalence, original recursive behavior review, source projection, focused array-cookie evidence, GCC/include/libexec/search/runtime/Python manifests, Graph compiler/LLVM/loader manifests and supplemental chmod/sleep facts. It describes one nested FailedStep compilation/execution, at most 22 GCC driver invocations and ten generated-C executions, and the exact authored scripts/return37/return38/cookie sources. It does not invent hashes for future generated programs or claim their execution was admitted.

`guard.py` retains the full original input fences, adds both completed-original-source and fresh-runtime-source fences, requires genuine permitted source deltas, and checks the retained generated FailedStep ELF's interpreter/libc/no-RPATH metadata. All other source residue or cache growth fails. Failed runtime residue is retained, not repaired into a pass.

## Ownership and actual terminal checks

`inner_owner.py` installs nonthrowing cancellation handlers, clears inherited masks, resets SIGCHLD, requires initial ECHILD, establishes/verifies its subreaper and retains a direct-child pidfd immediately after its sole fork. A ready/go pipe gate occurs after root setsid and before any graph-c instruction. It does not stop or delay the deadline shell itself. A partial core constructor remains reachable; closing the unopened gate causes only its fixed child to exit127. No constructor exception loses the root.

The normal loop observes before draining, propagates pending cancellation, and never retries a failed registration. Success is provisional until terminal flush/close and actual inner exit. Failure latches before owned cleanup. A denied/unsupported ownership action is not retried or bypassed; unresolved cleanup fails and the unchanged outer owner continues to reject escaped adoptions.

`run.verify_inner` reconciles both original clocks, exact root identity, registered ancestry, adoption identities, WNOWAIT status/raw-status pairs, reaping and ECHILD. It rejects new lifecycle events after ECHILD, unknown/failure events, contradictory receipts, nonzero/missing actual exit and late terminal work. Input/receipt files are hashed before and after worker post-fences, then reconciled again at the outer terminal boundary.

There is one original 125-second interval, captured before admission:

- Normal inner work and successful inner receipt: before110 seconds.
- Inner failure cleanup: before115 seconds; worker must observe actual inner exit before115.
- Worker fences/completion: before120 seconds. Because the old outer receipt does not timestamp each wait, the controller conservatively requires the entire successful outer call to return before120, proving its worker already exited by then.
- The unchanged outer cleanup reserve remains the final five seconds, ending125. Final top-level receipt/output and the external observed actual terminal must respect the same original interval.

No phase starts a fresh125-second timer. Blocking I/O is not claimed to be hard-real-time; late/missing actual terminal evidence remains failure.

## Output policy correction

The older read-only runtime review described graph diagnostics on stdout. Actual native source says otherwise: `core/os/Os.chibi:242-243` calls `sys.Print`; `core/sys/posix/Posix.chibi:979-983` writes the line plus LF to descriptor2. This packet therefore proposes **empty stdout and source-ordered stderr diagnostics**.

`output-policy.json` provides exact conservative alternatives for the authored standalone-empty branch. Graph diagnostic text/ordering and stream routing are source-traced. `output-tool-strings.json` records matching literal formats and offsets read from the pinned ld/collect2/libc binaries. Those strings do not establish their control flow or complete rendered output. GNU ld/collect2 wording is explicitly **not measured here** and still requires independent source/tool review. Unknown diagnostics are a preserved failure; no stripping, broad regex, output fabrication or success-shaped fallback exists.

## Controls and remaining admission requirements

`controls/` has its own source manifest and explicitly distinguishes kernel cases, declared read-boundary models, terminal tables, counterfactual mutations and caller lifecycle cases. The concrete frozen suite has38 case families,32 owner-case invocations and31 fixture roots. Its ceiling remains at most48 launches/420 seconds, not a target. One unchanged outer spans the suite; its worker enforces5-second normal,7-second owned-cleanup and8-second external case endpoints. Final case34 is an expected nonzero outer adoption rejection, never a passed outer receipt. An external coordinator must observe the actual final top-level exit. Control sources and any synthetic artifact require their own independent admission before running. The actual20ms case cannot claim an independent count of the unchanged shell's internal forks; its known shell launch, observed registered identities, unknown-adoption rejection and kernel ECHILD must be reported accurately.

The required future control final report has phase `graph-c-owner-controls`, status `passed`, exact production `source_sha256` values, `case_manifest_sha256`, and a nonempty `files` array pinning sources, synthetic artifact, raw journals, receipts and actual terminal evidence. The required recursive-runtime admission has phase `graph-c-recursive-runtime`, status `accepted`, the exact production source hashes, artifact/cwd/argv/environment, source-baseline/cache/mapping/dependency/output-policy hashes and evidence files. These documents do not exist here. A separate exact runtime acceptance and exclusive original intent are still required afterward.

Nothing in preparation removes the need for independent review of the complete moving-to-frozen package, concrete control outcome reconciliation, output-policy review, source/cwd runtime admission or coordinated original windows. No plan may be armed merely because a source-only review found no defect.

## Work performed and review notes

Only source/data copying, source writing, text inspection, byte hashing, static ELF parsing and metadata calculations were performed. No candidate import, syntax execution, compiler, test, control, owner, controller, native/generated program, prior proof or process-control syscall was executed. No repository/production source or prior proof packet was changed.

The new caller/controller/fence code is fixed-purpose rather than a generic supervisor. Existing broad input fences dominate its line count; preserving those avoids a new weaker admission boundary. Attribution retains its existing bounded dictionaries and observation-cost limits. New lifecycle and terminal handling must be controlled against the exact final hashes, including partial initialization and close/write/fsync races.

## Frozen delivery

The top-level `SHA256SUMS` inventories every static packet file outside `source/`; `source-baseline.json` binds the full fresh source. `INPUTS.sha256` and `PREPARATION-CHECK.json` retain33 verified read-only proof inputs and full original/new source-preservation checks. Both plans remain disabled. Later admission must bind fresh exact plan hashes, finite windows and actual artifacts; modifying a plan is not itself permission to execute. The current freeze is source-only and may be reopened only for a documented reviewed revision.

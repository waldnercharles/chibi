# Detached-capture reclamation regression admission

Baseline: `ec170c73e6c6ed7165c97837ec55ac7864f66d88`; baseline exact-head
logs are recorded by evidence-only commit `c0ad2e8ade20ede5a8aae318696b5fef6306f56e`.
The coordinating worker completed an independent contract audit and approved
implementation of the transactional detached-local-box write boundary. The
production diff is confined to `write_reference` in `execution/intrinsics.c`.

A copied logical reference points to a local candidate root. Existing reference
write semantics construct that candidate in fresh working storage. A second
owning snapshot retains only the final root in commit storage. Success publishes
the new root/storage and frees previous storage; errors release only temporary
arenas. Attached values, box identity, holders, reference paths, task collection,
inspection APIs and public ABI remain unchanged. A transient working arena may
copy obsolete subtrees, but none remain in committed storage. This is a lifetime
fix, not a claimed throughput optimization.

## Proposed workload, before execution

Use existing `language-program` and Node `wasm-program` selectors through the
production supervisor. Preserve every original assertion.
The earlier 14-scenario workload remains 382 total updates and gains an equality
assertion: detached box used bytes equal a fresh snapshot of the current root.

Five new C pico_unit cases:

1. Direct intrinsic, four attempted writes: an 8,192-byte string self-assignment,
   replacement with `small`, replacement with empty, then a missing-type snapshot
   rejection. Preserve an escaped owning snapshot and nominal metadata. Four
   idle observations after shrink require unchanged small storage.
2. Direct intrinsic, nine attempted writes: two-string array, a sibling-source
   and self-source element assignment, whole-root replacement with one element,
   write through a previously captured path, invalid old second-element path,
   empty-root replacement, invalid first-element path, restored two-element root,
   and missing-type snapshot rejection. Preserve original snapshot. Four idle
   empty-root observations. A descendant-style recapture preserves the path.
3. Direct intrinsic, eight scalar-field writes in a nested registered record
   containing an 8,192-byte managed string and a small sibling string. Preserve
   original snapshot and validate all current fields. No source-level record
   syntax is added; this targets the existing reflection contract.
4. Authored descendant capture, two backend scenarios, five updates each: a task
   passes the same detached box to its child, which self-assigns then shrinks to
   `small` and empty. Exactly one inspector pause/resume per backend; preserve a
   recorded owning watch through writes, final-holder release and owner detach.
5. Authored task-array capture, two backend scenarios, three updates each: retain
   a completed task result solely through the captured managed root across a
   self-assignment, await its value, replace with empty and verify task-result
   reclamation after the remaining task exits.

Total expansion: 21 direct intrinsic write attempts and 16 synthetic updates.
Total selected new+prior capture fixtures: 398 updates, still no scenario exceeds
41. At most one owner and three tasks in an authored scenario, one pause/resume,
static source under 2 KiB and instruction budget 10,000 per update/dispatch.
Each direct case has one stack-owned test box, at most two elements, one level
of nested struct fields, strings no larger than 8,192 bytes plus NUL, at most
nine writes and two escaped snapshots. Final box backing has a 128 KiB assertion.
All temporary measurement arenas and fixture allocations are freed. No new
process, network, rendering, browser, file fixture or dependency. The new test
file is linked into the existing dispatcher, not a standalone executable.

Direct intrinsic cases run once per native/Node process; authored cases verify
bytecode and actual compiled backend identity. Native ASan/UBSan runs keep LSan
disabled under this traced host. Requested allocation bytes and xalloc call
counts around each write are reported separately from retained bytes. They are
not live/peak process memory, copy-byte counters or throughput measurements.

Review the expanded fixed-workload measurements and one negative-control run of the same bounded selector with only intrinsics.c
restored to the baseline production implementation. Restore the fix afterward
and rerun both selectors on the final exact source tree. Preserve every log,
including failures. No publication or landing is authorized by this document.

## Review and execution record

The coordinator admitted patch
`c156308530e5ce1719125b12635875489c6785eb80b6280dadb49b51aec93ff1`, then reviewed
three fixture-only corrections and re-admitted patch
`3615bdbdbe87a8a1a7bc07f7fa1064af6afa3249be526752232fd6f1bfcad382`.
Both corrected program selectors pass; the one admitted native negative control
fails seven storage regressions with the baseline intrinsic and the reviewed
production blob was restored afterward. All earlier failure logs are retained.
The coordinator also requested the unchanged existing native language and Node
Wasm groups, plus available prepared/runtime and generated-C/tooling checks,
on the frozen candidate; these are separate from the new-workload admission.

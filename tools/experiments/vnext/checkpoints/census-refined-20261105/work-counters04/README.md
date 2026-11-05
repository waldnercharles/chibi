# Disposable work counters with stdout capture

**Source-only successor, not run or accepted.** The counter02 predecessor is preserved in the census-20261105 checkpoint. Base production source is exact
revision04: MaxBlocks64, generated bridge stillv3, accepted v4 digest unassigned.
All observed values remain unknown.

## What is authored

- `instrument_prepared.py`: checks an existing typed preparation against pinned
  Model/Produce/Verify/tokenizer/test-template/Census/strings bytes; independently
  re-expands the pinned test Main; writes a new disposable copy only.
- `Counts.chibi`, `WorkProbe.chibi.in`: counted producer/verifier siblings,
  fieldwise equality using existing helpers, fixed-depth4/16 producer fixtures,
  32/128 verifier families, reversed layouts, invalid-use controls, five negative
  sources and the exact Census input. Producer expectation remains5N+2=22/82 for
  both Expression entries and Emit calls, derived independently.
- `WorkOutput.chibi.in`: one cold batch from the **actual returned** report to native
  stdout through the existing `core:os.WriteOutput`. No per-node output, mutable
  production global, runtime counter mode, new native library or framework.
- `core-snapshot/`: seven pinned, byte-for-byte copied core implementation files.
  Original source candidates and the original prepared input are never edited.

Injected Main calls `RunWorkProbes()` exactly once, sends that value to
`PrintWorkReport`, then executes its entire unchanged former regression body.
`WorkRegressionPassed()` is inserted only before that body's successful return.
No counter value is computed from a projected expected count when printing.

## Single-file entry integration

The retained native_driver/Main.chibi:532 and591–600 documents and implements
`--source` as exactly one root file. Adjacent WorkOutput.chibi was therefore an
integration defect in frozen counter02, not a supported implicit package.
This successor merges the output template's two additional imports into Main's
import section and appends its helper bodies after the probe definitions. Only
Main.chibi is an emitted root source. The complete original output template and
probe template are retained under inputs with `.chibi.in` names, not as separately
compiled roots. Their exact header and Main insertion anchors are fail-closed.
The seven-file imported core closure remains unchanged and explicit.

No generator was run to claim generated-output validation. Before compilation,
the future packet must verify the actual generated single Main contains every
output helper and both imports, with no adjacent root-helper dependency.

## Why WriteOutput, not Print

The actual Os.chibi source documents and implements `Print` as stderr output.
Native `WriteOutput` delegates to the platform stdout writer instead. VM/build
mode routes both to build error output. Therefore this adapter explicitly asserts
Native=1, Linux, X86_64, PointerBits=64 before printing. It does not pretend that
Print writes stdout or silently merge stderr into a purported stdout result.

The underlying POSIX writer retries short writes; Os.WriteOutput discards its
boolean result. Acceptance must therefore verify complete schema coverage,
terminal markers and process exit0. A truncated/missing output is failure even
if the process exits0. Stderr must be captured separately and inspected.

## Explicit additional source closure

Copied from revision04 `packages/core/`, with hardcoded
SHA256 pins in the transformer and exact preserved file identities:

- os/Os.chibi -> core/os/Os.chibi
- target/Target.chibi -> core/target/Target.chibi
- sys/posix/Posix.chibi -> core/sys/posix/Posix.chibi
- sys/windows/Decimal.chibi -> core/sys/windows/Decimal.chibi
- sys/windows/Windows.chibi -> core/sys/windows/Windows.chibi
- hash/sha256/Sha256.chibi -> core/hash/sha256/Sha256.chibi
- bytes/Bytes.chibi -> core/bytes/Bytes.chibi

The existing core/strings/Strings.chibi is independently pinned and reused.
The seven-file snapshot covers imports in all conditional branches of core:os,
not only the current POSIX arm. Core package Tests.chibi files are deliberately
not copied or registered: this is the implementation closure needed by the
adapter, not an assertion that those core suites ran. Target.chibi is copied
unchanged with its normal Native=0 placeholder; only the supported compiler's
normal native-target replacement may activate Native=1. Capture the exact
compiler invocation, generated target bytes/hash and source receipt. Do not
hand-edit the target placeholder or report VM output as native stdout.

The new manifest declares single-file Main entry mode, exact Main generation inputs,
these package imports, root imports and the stdout
contract, and preserves the former manifest as provenance. No new imported
package is silently supplied from an ambient filesystem. Snapshot extra files,
symlinks, hash mismatches and destination collisions fail before output creation.

## Execution/reporting gates still open

1. Independent exact-final-source review, including the output adapter, closure
   additions and prior plain-to-baseline keyword correction. Author static
   syntax/keyword/hash checks are not compiler or runtime evidence.
2. Authorize and run ordinary typed preparation, then the source transformation;
   review/hash all generated sibling files, Main and manifests. No execution
   happened during authoring, including of the transformer itself.
3. Prepare a **new native output-aware execution packet**. The current generic
   silent-run packet cannot accept this program: expected stdout is nonempty,
   additional core imports are explicit, and native target replacement must be
   recorded. Compile/run only after that packet's review and execution fence.
4. Capture raw stdout/stderr/exit status without filtering. Validate the complete
   CW1 schema in OUTPUT-SCHEMA.md; preserve both the parsed actual values and raw
   bytes/hash. Require report-end, regressions-passed, exit0 and reviewed stderr.
5. Independently check runtime Module/diagnostic equality and work ratios,
   retained regression outcomes, numeric verifier bounds and captured inventories.
   Author source review alone closes none of these acceptance gates.
6. At64 the128 family is explicitly skipped, not executed or accepted. Census
   inventories cover only bodies reaching Statements and mark partial emission
   complete=0. Its full inventory and later functions may be unavailable.
7. A separately reviewed cap128 candidate requires a deliberate pin rebase and
   all128/129, Census and counter gates again. Generated v4/Pack/backend/ABI,
   native checked reads and aggregate acceptance remain separate and open.

The counter semantics, arithmetic and bounds are detailed in the preserved
predecessor `../../census-20261105/work-counters02/AUTHORED-CONTEXT.md`. No native checked-byte instrumentation is added here.

## Counter04 publication lifetime correction (source only)

This successor preserves the prior source revision unchanged. Historical author,
review and audit records are excluded from this archive. Current archived identities
are in the checkpoint manifest. All execution remains held. The transformer and
newly authored tests have NOT been executed.

The coordinator must own the existing destination parent and its unchanged
ancestry exclusively throughout preparation and publication. No concurrent writer
may create, replace, rename or retarget anything there. The source preparation,
templates, transformer and snapshots must remain immutable for the whole call.
These are admission prerequisites, not locks that this script establishes.

After validating/caching input and generating source in memory, publication
allocates one uniquely owned same-parent `.incomplete-<name>-*` directory. Every
payload is exclusively created, fully written, closed and byte-verified. The
manifest is serialized before staging, then written, closed and byte-verified
last. Only then is the complete directory renamed to the requested final name.
The parent is never created by the transformer. Existing targets, including
empty directories and dangling symlinks, are refused, including at the last
check before rename. Portable rename is NOT adversarial no-replace: the check
and rename require the exclusive-parent contract. Publication is atomic namespace
visibility on a supporting filesystem, not power-loss durability; no fsync
promise or protection against a hostile/concurrent parent writer is made.

On a caught staged failure, the diagnostic names stage and final and preserves
the cause. Partial stages are retained, never deleted, reused or promoted by the
transformer. Before rename, this invocation has not published final. An exception
from rename is conservatively uncertain even if final now exists. Interruption
or process termination can also leave an uncertain outcome without a diagnostic.
The coordinator must inspect both names and independently verify manifest,
membership and all bytes against the intended invocation before deciding what
happened. Final existence or a parseable manifest alone is never success. Do not
automatically consume, promote, delete or retry these paths. After reconciliation,
use a fresh destination for a separately admitted attempt; no automatic repair
or replacement is supplied here. Ordinary consumption still requires confirmed
successful invocation and an independently checked final receipt.

`test_publication.py` authors data-only success, existing/dangling target,
missing-parent, payload/manifest open/write/short-write/close/readback failures,
pre-publication collision, and rename-before/after-effect tests. It exercises
only the cached-byte publication boundary, never mocks compiler acceptance or
measures counters. Its faults model IO boundaries, not Chibi execution. All are
UNEXECUTED. The separate finding that five producer negatives discard meters
remains OPEN and is intentionally unchanged here.

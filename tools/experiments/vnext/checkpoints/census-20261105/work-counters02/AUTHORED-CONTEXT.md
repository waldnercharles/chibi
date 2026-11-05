# Historical authored context

This is the original source-stage README preserved as historical context, not current acceptance or publication status. See ../STATUS.md for current evidence. Internal review artifacts mentioned below are intentionally not included.

# Disposable work counters with stdout capture

**Source-only successor, not run or accepted.** The predecessor packet at
`census-work-counters-20261105` is preserved. Base production source is exact
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
- `WorkOutput.chibi`: one cold batch from the **actual returned** report to native
  stdout through the existing `core:os.WriteOutput`. No per-node output, mutable
  production global, runtime counter mode, new native library or framework.
- `core-snapshot/`: seven pinned, byte-for-byte copied core implementation files.
  Original source candidates and the original prepared input are never edited.

Injected Main calls `RunWorkProbes()` exactly once, sends that value to
`PrintWorkReport`, then executes its entire unchanged former regression body.
`WorkRegressionPassed()` is inserted only before that body's successful return.
No counter value is computed from a projected expected count when printing.

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

Copied from `chibi-vnext-census-revision04-20261105/packages/core/`, with hardcoded
SHA256 pins in the transformer and copy verification in `STATIC-AUDIT.txt`:

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

The new manifest adds these package imports, root imports and the stdout
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
predecessor README. No native checked-byte instrumentation is added here.

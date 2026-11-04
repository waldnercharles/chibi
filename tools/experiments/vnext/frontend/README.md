# Current-parser experiment

This directory is a caller-first bridge to the retained Chibi frontend, not a
second parser. `prepare.py` embeds the actual slice corpus as source strings in
`ParserHarness.chibi.in` and copies the current parser's exact source closure
into a new directory. It never compiles. All semantic/link rejection fixtures
must parse successfully; parsing them does not establish their later rejection.

The harness makes 22 parser calls: 15 unique corpus documents, one UTF-8 comment
case with tree invariants, two scalar field/shape cases, and four syntax failures
with precise diagnostic locations. The 15 corpus documents contain 2,851 bytes
and 76 physical lines. The retained parser closure is eight files, 124,561 bytes
and 3,549 lines at `ba99e386a74a6f610fcdfe0c16a5aa778e025a51`:

- `source_parser/{Declarations,Expressions,Parser,Statements,Tree,Types}.chibi`
- `source_tokenizer/Tokenizer.chibi`
- `syntax_diagnostics/Diagnostics.chibi`

These packages import no core package. The required driver `--core` argument
therefore points at an empty frozen directory. This prevents accidental closure
expansion. A changed import/file set requires inspection and a new preparation.
Current parser links retain their existing `-1` absent sentinel and zero root.

Run the inexpensive, Python-only preparation/controller tests with:

```sh
python3 tools/experiments/vnext/frontend/tests.py -v
python3 tools/experiments/vnext/frontend/prepare.py /absolute/new/probe-directory
```

Only after independent launch review and a specific CPU-window approval, run
the frozen `probe-directory/controller/run.py` with the directory, `--seed` and
`--llvm-root`. It accepts only the pinned public c5 seed digest, not an arbitrary
binary. It gives one `--level 0 --jobs 1` native build at most 300 seconds and,
only after successful build and identity checks, gives the parser harness at
most 30 seconds. It never invokes the toolchain build graph or full bootstrap.
It records source/controller identities, seed/LLVM/library/link input identities,
command logs, deadline/interruption outcomes and output identity. Existing
evidence paths cannot be overwritten. Failure is evidence, not a retry request.

The controller and baseline runner share `../owned_process.py`. Preparation
copies that operation beside the frozen controller and includes it in the
implementation fence. Linux `waitid(WNOWAIT)`, pidfds, self subreaper capability
and readable procfs are required. The one-purpose caller must have a single
main thread, default SIGCHLD disposition, no existing children and no other
child reaper. It explicitly resets the required kernel SIGCHLD default before
launch, so native auto-reap flags cannot invalidate PID reservation. It
temporarily becomes a subreaper, retains the exited leader
until watchdog/signal callbacks and all group signals finish, then terminates
only actual direct/adopted children through pidfds. This includes native LLVM
workers with separate process groups. Reaping continues to kernel `ECHILD`,
not merely an empty procfs observation, and restores prior subreaper state.
Adopted descendants reject ordinary success. No old recovery process, receipt
or session is acted on. Elapsed time measures launch through observed leader
exit. Exit observation polls every 5 ms and can add that observation delay plus
scheduler delay, so fast runs are not submillisecond timing measurements.
Cleanup has a separate one-second bound and reports `cleanup_seconds`,
`cleanup_echild`, `subreaper_restored`, `cleanup_limit_seconds` and
`overall_limit_seconds`. An enclosing fixed phase must reserve that second
from its total budget; the existing 300/30 execution limits are unchanged.
Incomplete cleanup is an error. Controller crashes and blocking OS operations
still need an external supervisor for hard wall-clock guarantees. This is
controller evidence, not a benchmark. Older frozen receipts remain unchanged.

The old seed is a distinct builder experiment, **not an admitted current
compiler**. The passing parser harness validates these current source bytes
on this one Linux target with this builder. It cannot certify the entire
language, current compiler self-hosting, the source-to-native slice, or the
100,000-lines/second goal. Run-03 passed all 22 actual parser calls using the eight frozen compiler source
files from `ba99e386a74a6f610fcdfe0c16a5aa778e025a51`. Its executable SHA-256 is
`d426ee6bac6f33d14fd1ac5bd6d6d688d36b464adb31da466a33be60d9f56efb` and its
whole-run receipt SHA-256 is
`b6ce927f52b2249615034152ecf2d0c2ccf48baa9ca45c2641dee36b34fc5bf4`.
That proof used the prior frozen controller, not the subsequent shared
supervision refactor. The failed run-02 remains separate, unchanged evidence.
Run-05 subsequently passed the same build and 22 checks with the shared runner
and all source/tool postflight guards. It produced the same executable bytes;
its whole-run receipt SHA-256 is
`415f537f0fb56b4a95add6b213f6636b1e61fe6c2b9d4f3cf05d65be158326e8`.
These runs establish neither typechecking, current-compiler admission nor speed.
No typed-body producer was part of this parser checkpoint. The later
[bounded producer](typed/README.md) and [source-native backend proof](../slice_backend/README.md)
now pass their separate gates. The historical parser receipts remain limited
to their frozen parser callers.

## Scalar headers and bodies

The implemented `Produce` caller parses real Echo/Join and source variants,
verifies the produced bodies, and passes them through generated Pack to both
native consumers. The source-native proof passed 1,080 JIT value assertions and
45 linked C checks; the superseded handwritten backend probe has been removed.
The numbered design below includes broader future gates, especially package,
direct-call and foreign-call support; it is not a claim that the full original
corpus is accepted. Do not synthesize expected bodies from function names or
special-case their text.

1. Parse each explicit document once with `source_parser.Parse`. Keep its raw
   indexed nodes/fields and source bytes until diagnostics are complete. Reject
   syntax failures at this boundary. Source-tree navigation uses integer kinds
   and field IDs, not a string-shaped AST or another lexical pass.
2. Collect package/import/foreign declarations and all function headers before
   checking a body. Use the existing package/header seams as references and
   reuse `signature_reader.Signature` for admitted type syntax. Precheck the
   slice's forms: explicit i64/bool parameters, exactly one explicit scalar
   result, ordinary internal functions and declared fixture foreign functions.
   Reject generic/default/named/trait/pointer/array/optional or inferred signatures
   as unsupported slice input, before body emission. Preserve `pub` visibility.
3. Publish one immutable scalar header catalog with dense module, function,
   signature and name IDs in deterministic document/declaration order. A header
   owns its ordered parameter span, result type, declaration/body node and
   origin. Function IDs reserve zero for absence. Resolve forward and mutually
   referring calls through the complete catalog; do not ask a body to discover
   its callee signature. Reject duplicate parameters before entering its body.
4. Resolve names once into dense function/local IDs. Build the lookup index at
   header/local admission, outside instruction emission. Do not repeatedly call
   current lexical linear scans for each emitted operation. If a tiny bounded
   first test uses a scan, state its admitted count and replace it before
   increasing workload; do not call it scalable compiler evidence.
5. Walk each scalar body once in source evaluation order, returning typed values
   plus diagnostics. Expected types flow into literals/calls/returns. Parse a
   number node's literal once with pre-overflow range checks; runtime i64 add
   and multiply remain wrapping. Bool is distinct. Reject missing names,
   incorrect argument count/type, non-bool conditions and missing return paths.
   The reader must inspect every child of an admitted declaration/type, so an
   unsupported suffix cannot disappear just because its base name is `i64`.
6. Maintain a dense local-ID to current-value map. For `Join`, start `result`
   at the right parameter, evaluate the true arm with `result` set to left, and
   emit two edges to a merge block carrying the differing value. The merge has
   one i64 block parameter; addition and return consume that parameter. For
   `Maximum`, a returning arm contributes no merge edge. Reject statements
   following a terminator rather than emitting unreachable output by accident.
7. Resolve a direct call to its admitted function ID and emit source-ordered
   argument values. Foreign calls retain order even when unused. Lower `&&`
   into control flow, so the right operand's calls occur only on its taken edge.
   Foreign symbol availability is a separate pre-publication native gate.
8. Verify these produced bodies before either backend reads them. Mutate actual
   produced output for rejection tests: wrong value/block/function IDs, duplicate
   definitions, non-dominating uses, missing terminators, wrong block arguments,
   call types and returns. The internal verifier asserts compiler bugs; malformed
   source instead returns source diagnostics and does not publish executable code.

The concrete array budgets and operation vocabulary remain those in
`../slice-design.md`. No backend may consult source syntax to reconstruct a
semantic decision, and no mandatory prepared-VM, sparse-facts or LLVM-text step
is inserted. The implemented [batched boundary](bridge/README.md) is a
synchronous same-process transport, not a persistent public serialization
format. Its public source-native reproduction passed all five stages with the
separately validated final-Cargo backend library and independent terminal
readback. This remains bounded correctness evidence, not a compiler benchmark.

## Reuse cost and limits

The current `signature_reader` transitive source closure is eight files, 3,395
lines and 135,728 bytes. Combined with the parser, after removing their shared
diagnostics file, that is 15 files, 6,682 lines and 248,035 bytes. These are source
counts from explicit import inspection, not execution times. `callable_headers`
extends that closure to nine files, 4,192 lines and 179,599 bytes. Its `Register`
path still refreshes catalog views; blindly calling it for every header is not
the proposed scalable publication path. Any extracted batching change belongs
in a later reviewed change, not silently in this parser experiment.

This is a cold probe. Preparing the approximately 125 KB parser and 3 KB corpus
should take substantially less than a second, excluding filesystem scheduling.
The unavoidable harness work is one parser call per document and a linear walk
of each returned tree; token/node counts and current parser allocation/copy cost
remain unmeasured. Representative compiler cost remains unmeasured and is not predicted from
these tiny source counts. No speedup is claimed.

Delete the embedded standalone corpus transport when the real frontend driver
can read the registry directly. The controllers share one owned-process operation; the
completed run-03 proof remains pinned to its earlier frozen implementation.
Leave production parsing, old bootstrap work and backend implementation
unchanged by this checkpoint.

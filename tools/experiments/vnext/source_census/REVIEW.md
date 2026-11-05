# Source/design review

Status: author cost/design review only. Source review does not establish compiler
execution or native acceptance; both remain pending. All applicable architecture, performance, data, API, failure/lifetime, compiler,
runtime, deletion/test and style review passes were considered. This is not an
independent review.

## Brief

- Transform: one immutable CCN1 buffer becomes a 16-i64 report, or one identified
  framing error with all 13 census counters zeroed. No input is mutated.
- Counts: the historical example has 122 records and 3832387 framed bytes. The
  admitted maximum is 16385 records / 268632348 framed bytes. Two functions own
  loops/decoding; one foreign function only prints. No fresh closure was gathered.
- Consumers: the native C report host, independent oracle and future native-driver
  baseline comparison. C does not interpret payloads or record headers.
- Layer: cold proof/tool initially; a census used in the build loop is latency-
  sensitive. The compiler throughput benchmark is a separate consumer/measurement.
- Minimum work: inspect every byte once, classify LF/ASCII whitespace, and update
  counters. Payload reads are 3830907 historically, at most 268435712. Header work
  is 16+12*records, at most 196636 bytes. All growth is linear in the frame extent.
- Actual work: the sole byte intrinsic lowers to signed-index/extent checks,
  a guarded u8 load/zero extension or -1, then Chibi LF/whitespace branches,
  counter/last-byte/cursor updates and the loop branch. The source reads every
  header once through 370 four-byte calls historically. The maximum is
  4 + 3 * 16385 = 49159 calls, so 196636 header reads. There is one final FFI call.
  The nominal resolution, packing and JIT/AOT compilation occur before execution,
  not per runtime byte. No payload allocation, hash or text conversion exists.
- Estimate: roughly 10-25 simple scalar/check/control operations per payload byte
  gives 38-96 million operations historically, before branch/dependency effects.
  At the repository's rough 10^9 operations/sec scale this is tens of milliseconds
  of ideal work, plausibly subsecond rather than minutes; actual native time,
  code size, spills and bounds-check elimination are UNKNOWN and must be measured.
- Growth: O(frame bytes + record headers) runtime, constant caller state. Compile
  CFG projection is 122 typed blocks across both authored functions. Dominance has
  a bounded fixed-point algorithm, not a falsely claimed linear pass.
- Gap: UNKNOWN. No runtime/compiler timing was run. The estimate is not evidence
  of the 100000 original physical LOC/sec full-rebuild target.

## Target shape

One input borrow -> Chibi framing state machine -> Chibi byte loop -> one C report.
One canonical semantic ValueID per logical value; rare backend pointer/length
pairs go in a ValueID side vector. The unchanged input gets no loop phi. The
outer write-set carries 17 i64 IDs; the inner one carries four i64 IDs and one
bool ID. No general SSA framework, record-return encoding or managed-pointer
replacement is introduced to support this caller.

## Findings and resolutions

[ARCH] baseline.py:22-23 versus proposed frame capacity
Cost: correctness/adoption. A shared original/generated cap would reject an
otherwise supported full 16384-file or 256 MiB original input when target generation
adds its record. Evidence: inspected baseline bounds and the single target-text
substitution in Compile.chibi/Scalar.chibi. Resolution: original limits preserved;
a separate one-record/256-byte generated profile adds to total frame capacity.
The current literal template was inspected and all stated numeric substitutions
produce 235 bytes. Other generation workloads require a named profile decision.

[ARCH] typed/Verify.chibi:19-25 and the final dominance walk
Cost: correctness. The current physical-order and forward-edge assumptions reject
both while backedges. Evidence: source caller has 2 loops, a 121-block projection,
and current MaxBlocks is 64. Resolution proposed, not implemented: reachability,
predecessors and fixed-point masks with all original ownership/type/order/edge
checks retained; gate 128 on actual emission and 128/129 tests. Recomputed table
ceilings and the work/memory bound are recorded in README.md. Raising the cap
alone does not resolve this finding.

[ARCH] typed/Produce.chibi:188-222
Cost: semantic authority. Current ReadType permits only spelled i64/bool with
nominal 0 and no uses. A borrowed-type spelling shortcut would bypass retained
nominal resolution. Resolution proposed: ordinary catalog admission through
reader.Signature with one canonical exact nominal descriptor; reject aliases,
modifiers and spoofed identities outside the chosen contract.

[PERF] slice_backend/lib.rs:210-212 scalar value map
Cost: compiler memory/cache work proportional to EVERY value if widened into a
pair for two rare borrowed parameters. Evidence: this source has precisely two
logical borrowed definitions; all counters/results are scalar. Resolution
proposed: retain scalar mapping and add two pair side rows, proposed 24 payload
bytes with actual-handle layout assertions still required. No per-runtime-byte
side lookup or C crossing.

[TEST] new sixteen-argument report and expanded borrowed entry
Cost: false ABI acceptance. The historical foreign proof exercised eight scalar
arguments, not this 16-position report or pointer/length expansion. Resolution:
require distinct values in every position, six-register/ten-stack-argument
coverage, alignment and result checks in both JIT and linked AOT, plus one-report
and missing-symbol controls. Neither proof was run here.

## Correctness notes

- Actual foreign allocation validity is a host contract. Index checks prove only
  index versus declared extent. Guard-page valid-allocation tests exercise the
  intrinsic; forged/freed pointers are not promised recoverable input errors.
- Original totals include replaced originals; active totals are derived by
  subtracting replaced and adding generated, never overwriting the denominator.
- Frame/payload extents and budget subtraction are checked before reads/addition.
  u32 decoding is at most 4294967295; valid offsets/counters stay below 268632348.
  No accepted runtime arithmetic approaches i64 overflow despite wrapping ops.
- Empty files, final unterminated lines, CRLF, bare CR and ASCII-only nonblank
  semantics are stated. No newline is appended to the framed payload.
- Error paths stop scanning and clear all report counters. The first error's
  record/header/EOF offset is kept. No malformed frame calls an assertion.
- Backend physical block order must not reintroduce forward-only dominance.
  Reverse postorder plus predeclared block parameters preserves valid backedges
  and dominator-first instruction availability without reinferring source types.

## What I would delete

- No production code is superseded by this proposal, so delete none now.
- Reject any unpublished per-byte foreign census, generated-payload-as-compile-LOC
  accounting, spelling-only BorrowedBytes admission or widened-every-value design
  if it is rediscovered. None is preserved as an alternate proposal here.
- After real-tool adoption, remove superseded synthetic-only acceptance scaffolds
  only when every useful invariant is covered by the exact-source real path.

## What I would measure

- Exact retained-parser nodes, emitted typed blocks/values/origins/arguments,
  source write-sets, CFG nesting and the conservative 121-block estimate.
- Generated template/closure bytes from the actual pinned current invocation,
  including original/replaced/generated nonblank counts; no fresh census exists.
- Actual pair-map/record layout and allocator/COW/packing traffic; masks plus
  predecessor/worklist memory; fixed-point pops/intersections on worst inputs.
- Eager-JIT and AOT code time separately, guarded read instruction count, scalar
  loop throughput, report argument placement/alignment and zero invalid loads.
- Full nonincremental development compile latency with linked/finalized output,
  original source denominator and complete phase/auxiliary-input accounting.
  Current compile_both cost includes both outputs and must not be subtracted.

## What I would leave alone

- Managed pointers/views/COW semantics, production parser/tokenizer and the
  retained signature reader. The proposal uses them where their contracts fit;
  it does not substitute a new language parser or relabel old references.
- Existing scalar Value/Instruction rows and ordinary scalar arity. Their common
  path should not pay pair storage or arbitrary foreign-borrow support.
- Source state-machine length. Splitting counters into encoded return values
  would create representational work solely to fit an arbitrary proof cap.
- Existing historical receipts, current-v3 open native selections, source fences
  and default-toolchain gates. This source/design trial closes none of them.

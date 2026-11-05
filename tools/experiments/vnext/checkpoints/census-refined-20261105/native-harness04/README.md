# Census native consumer: source-only candidate

No compiler, backend/native executable, project test, bridge generator, or either
new preparer has been run for this checkpoint. This is an inactive preservation checkpoint, not an activation or acceptance receipt.
Earlier harness checkpoints and the input revision remain frozen.
The copied
`Main.chibi` and `reference.py` are byte-identical to the input revision.

This is a dedicated correctness consumer, not a new production mode, registry
variant, lowering implementation, compiler-closure census, or acceptance receipt.
The current revision cannot run it: Model requests v4, its reviewed new digest is
not set, generated Pack is v3, and the current function-block limit is 64. The
separately proposed cap128/permutation work must pass its own gates first.

## Source deliverables

- `Main.chibi`: exact retained actual Census source, unchanged.
- `Capabilities.chibi`: seven supplemental authored functions in the same package
  (six exported and a private forwarding helper). Neither replaces Census.
- `CensusMain.chibi.in`: original bridge NativeMain ownership test program, with
  fixture/foreign slots and the lifetime/corpus call sites changed. Its pipeline is
  retained source
  bytes → ProduceSources (which calls Verify) → an explicit Verify assertion →
  generated Pack → the existing shared compile_both JIT/object entry. It retains
  poisoning/dropping compilation buffers, concurrent owners, repeated object
  copies, exact COW-copy checks, failure metadata and profiling-rejection control.
  Every native address is checked for export/name/ordered *logical* signature and
  result before the synchronous C call. No Chibi borrow is manufactured by the
  old scalar test program; C creates the actual input allocation.
- `native_adapter.c/.h`: one shared test adapter for JIT and AOT. Its sole foreign
  binding is the real 16-i64 report, with an independent fixture expectation of
  retained i64 ID 2. Two narrow test entry points separate bounded lifetime checks
  from the external differential corpus; no mode flag or callback framework.
  BorrowedBytes is never added to scalar foreign metadata.
- `aot_main.c`: links the emitted object to the same adapter, calling the same
  entry signatures and consuming the same suite bytes/expected tuples as JIT.
- `prepare_sources.py`: source-only literal copying with exact-byte roundtrip,
  immutable source identity checks, a literal 29-file existing SOURCE_MAP, and
  mandatory externally reviewed Model/Pack hash pins. It refuses v3 Pack and
  never imports/runs generate.py, compiler code or another preparer. It copies
  the unchanged owned_process.py as the future runner boundary, not a substitute.
- `prepare_cases.py`: a future data-only preparation step. The copied independent
  `reference.py` supplies *all sixteen* expected values for every corpus frame, including
  malformed errors, zero counters and extent. C does not reconstruct any field.
  The separate fixed C lifetime fixture has a manually authored literal tuple;
  it is not a generated corpus expectation. Small authored corpus inputs cover
  all main/record header fields, truncations, byte
  semantics, package ordering, replacement pairing and precedence. Optional
  `--frame` inputs admit opaque external exact-closure/full-domain/fuzz cases.
- `host-contract.h`: unchanged contract copy. Production host.c and its input,
  output, allocation/close/output-failure controls remain separate gates.

## Preparation publication and failure contract

Both preparers require an already existing canonical output parent, exclusively
controlled by the outer coordinator for the entire preparation, including the
absence check and rename. Ancestors must stay unchanged; no concurrent process
may create or replace the destination or staging paths. Inputs, imported oracle
and source files must remain immutable throughout the operation. This is a caller
precondition, not a lock or an adversarial-filesystem guarantee.

An existing destination of any kind, including an empty directory or dangling
symlink, is refused. Canonical paths cannot traverse symlinks. Source preparation
validates all source bytes, pins, artifacts and substitutions before staging.
Case preparation checks every external frame's regular-file path, readability
and extent, and the aggregate case count, before staging. A late missing,
nonregular or oversized --frame therefore leaves no output or staging tree.
External frame bytes remain mmap-streamed with 256-KiB copy chunks, not collected
into RAM; only the fixed small corpus and O(N) metadata are retained. A second
open checks inode/device/size/timestamps against preflight. This does not protect
mmap against a concurrent writer or truncation; the immutability rule is mandatory.

Each invocation allocates its own unpredictable `.OUTPUT.incomplete-*` sibling
using exclusive directory creation. It writes all files there, closes all streams,
and writes its manifest last. In particular, all nine invalid-transport files
close before cases.json is written. Only then does it recheck destination absence
and rename the complete tree to the final name. Embedded native object paths use
the final directory, never the temporary sibling. No recursive creation of the
output parent is performed. There is no cross-filesystem copy fallback.

A target appearing before the final absence check causes failure and is untouched.
Portable directory rename can replace an empty directory created *after* that
check; hence exclusive control of the parent is required, and this implementation
does not promise race-safe no-replace publication against another writer. A caller
unable to enforce that rule must not use these preparers. Atomic namespace rename
is not crash durability: no fsync or power-loss recovery guarantee is supplied.

An ordinary write, close or rename failure retains only the owned staging tree
for diagnosis, rather than deleting potentially useful evidence. The diagnostic
names both staging and final paths and propagates failure; unexpected exceptions
and interruptions also attempt that diagnostic. A killed process may not print it.
A staging tree is always incomplete/unaccepted, even if it contains a manifest.
Never consume, rename or resume it as an accepted result. A successful preparation
requires a confirmed zero exit plus verification of the final tree's identities;
even that is only data preparation, not native acceptance.

After a failure, reconcile the final and staging paths before retrying. If final
is absent, a corrected invocation may use the same fresh final name and allocates
a new staging sibling, leaving old diagnostics untouched. If final exists (for
example, interruption after rename but before exit receipt), do not overwrite it
or retry blindly: verify provenance/contents and retain it for investigation,
or select a different fresh final name. Cleanup of retained diagnostics is an
explicit outer-owner action after identity/ownership verification.

`preparation_tests.py` authors data-only regressions for late invalid inputs,
no-overwrite and symlink restrictions, transport/payload/manifest-close failures,
target appearance, rename failure, corrected fresh retry and successful
manifest/copy identity. Synthetic source/archive bytes never enter a compiler.
These tests have **not run**. Future authorized invocation is
`python -B preparation_tests.py`; this imports the two preparers and independent
oracle and must be admitted as a distinct data-only stage. The source manifest
snapshots this test file alongside both preparers. Private review reports are
not preparation inputs or payload. The source manifest records every copied
source hash/extent and the backend/adapter paths, hashes and extents; those
identities provide preparation provenance without bundling private reports.

The harness04 success regressions independently decode suite fields and verify
manifest correspondence, literal frame/tuple expectations, external input order
and exact malformed controls. The source regression uses distinct synthetic
bytes and a separately frozen 29-file closure, checks the full output membership,
exact source copies, embedded fixture documents and all four substitutions.
All tests remain authored and **unexecuted**.

The obsolete empty CheckLinkRejections call/block is removed. CheckFailure still
checks the profiling-rejection owner, and signature negatives and all real native
ownership checks remain intact.

## Adapter storage and suite format

The coordinator sets `CENSUS_NATIVE_CASES` to one immutable `cases.bin` for both
JIT and AOT, and pins its hash before/after every stage. That path is a local test
input, not generated by native code. The suite is not a Census frame:

1. Eight literal bytes `CCTEST1\n`, followed by little-endian u64 test-case count.
2. Per case: little-endian u64 frame extent, sixteen little-endian signed i64
   expected fields in ABI order, then exactly that many opaque frame bytes.
3. EOF immediately after the last case. C accepts 1..100000 cases and real
   extents through 512 MiB, allowing the profile cap and oversized-rejection
   cases without forged lengths. This is a test-resource bound, not a reduction
   of Census's admitted frame domain.

C only parses this suite envelope. It never inspects CCN1 headers, classifies
payloads, counts lines/records/packages or supplies precomputed counters *to*
Census. Each case gets two real mmap allocations: one left-aligned, one
right-aligned between PROT_NONE neighbors. The entire accessible middle region
is made read-only before calls. Both receive identical exact bytes. NULL is used
only with extent zero. Each mapping is unmapped only after its final call.
Allocation, mprotect, read and close failures are errors, not skipped successes.
These local allocation protections do not change any system security setting.

The capture compares every field, exactly one report, and entry return against
oracle status. Callback count saturates at two so repeated calls cannot wrap to
one. Capture is a single synchronous process hook; concurrent/reentrant use is
forbidden. No entry or borrow is retained after the adapter returns.

Capability calls use the real bytes 0/127/128/255 at both guard edges; indices
INT64_MIN, -1, first, interior, last, exactly length and INT64_MAX; NULL/zero;
direct-call and block-merge pair forwarding in both branches; wrapping
subtraction at signed endpoints; zero/many/nested loops; and sixteen distinct
positive/negative callback arguments spanning registers and stack.

The ownership program performs three CheckLifetime calls per iteration across
eight iterations, **24 bounded lifetime invocations** (peer, anchor, surviving
anchor). Each checks capabilities and a literal 47-byte valid Census frame at
both guarded edges. The frame contains one replaced original `a\n ` and one
generated `b\n\nc`, with a literal sixteen-field expectation independent of the
suite preparer. No suite file is opened in this path. AOT makes one such bounded
invocation before its corpus call.

After the ownership loop, CheckCorpus revalidates the Census export and logical
signature against the same produced module and calls the external suite exactly
once while the anchor is still live. Failure closes the anchor; success copies
the object and closes the anchor. Addresses never escape these synchronous
calls. AOT runs the identical external corpus once. Both guard placements remain
intentional: **two Census calls per corpus case per execution mode**, four total.

For N cases totaling B frame bytes, JIT corpus work falls from 24 opens, 24B file
input, 24B payload copies and 48N Census calls to one open, B input, B copies and
2N calls. JIT+AOT corpus calls fall from 50N to 4N, a 12.5x work-count reduction,
not a measured speedup. The 268632348-byte valid profile falls from 12894352704
JIT scanned frame bytes to 537264696; JIT reads/copies each fall from 6447176352
to 268632348 bytes. The 24 lifetime invocations add only 2256 frame bytes scanned
and copied, plus unchanged bounded primitive checks. Nine owner compilations
remain unchanged. Peak corpus guard storage remains about twice the rounded
largest frame size (about 512.4 MiB for that profile), plus four guard pages.
The aggregate suite maximum is still impractical (100000 times 512 MiB);
a future runner must enforce total bytes, elapsed-time and storage budgets before
external/full-domain cases, not rely on per-case limits as a practical budget.

Adapter/AOT exit results: 0 exact match; 1 mismatch; 3 suite/resource/environment
error. The retained Chibi ownership caller maps a nonzero adapter result to exit
7 for lifetime failure, or 11 for corpus failure, after closing owners; its
controller must not expect the raw adapter exit code.
Generated-code faults remain process failures collected by the existing runner.
There is no signal handler that turns guard faults into success.

## Integration plan and non-negotiable gates

1. Review the v4 descriptor/digest/generator changes, nominal resolution and
   nonescape, pair lowering, checked reads, subtraction, loop verifier/cap128,
   128/129 limits, packing bounds and block permutation work. Run those only under
   the coordinator's separately authorized bounded stages. This harness does not
   establish their readiness. Regenerate canonical v4 bindings in an explicit
   future generator stage, retain exact source and output hashes, and verify
   generated-file agreement. Never run generation hidden in source preparation.
2. Independently review and execute reference_tests.py (ordinary and explicitly
   enabled large tests). This candidate reuses the oracle but does not certify it.
3. In a fresh coordinator-owned directory, compile the adapter into a test-only
   archive from native_adapter.c, with the selected slice_backend/foreign.h.
   Do not link production host.c into that archive. Snapshot the archive/backend
   identities. Run prepare_cases.py only as an explicit Python data-preparation
   stage, outside any native/compilation measurement. Record its source hashes,
   oracle hash, case names, complete expected tuples and cases.bin hash.
4. Invoke prepare_sources.py with the selected root/output, already built backend
   and adapter, and externally reviewed model/pack SHA-256 pins. It does not prove
   that those artifacts passed review or generation agreement. Coordinator must
   verify all source-manifest entries and artifact identities before execution.
5. Integrate a Census-specific runner at the test-consumer layer. The existing
   scalar registry and run_native manifest expectations are intentionally not
   stretched to accept this candidate. Use unchanged owned_process.py for each
   bounded compiler/JIT/perf-failure/C-link/AOT process, process-group cleanup,
   timeout diagnostics, exit records and immutable artifact verification. Supply
   the same CENSUS_NATIVE_CASES to both execution modes, recording one corpus
   invocation per mode and two placements per case. Bound total suite bytes,
   storage and elapsed time, and separate compile/lifetime/corpus timing.
   The future runner adapter
   is not supplied and remains an explicit integration gate.
6. Compile prepared Chibi test program once. Run bounded JIT ownership checks
   and the single JIT corpus pass;
   copy source-native.o only after JIT checks pass and owners are destroyed. Run
   the inherited separate PERF_BUILDID_DIR rejection process. Link aot_main.c plus
   that exact copied object and the identical adapter archive; run linked AOT on
   the same suite. No separate AOT lowering or independently handbuilt IR.
7. Verify rejection of malformed suite envelope controls and add callback fault
   controls (zero/two reports, wrong field/return) before accepting the comparator.
   Verify the fixed lifetime frame/tuple independently against reference.py in a
   separately authorized stage. Check no generated call occurs for logical
   signature/export mismatch. Add runner receipts or counters proving 24 bounded
   lifetime calls and one corpus call in normal JIT, and one of each in AOT. Native
   stack alignment, ABI canaries, checked reads and guard faults need real results.
8. Later: actual immutable compiler closure identity/provenance; high-count and
   full-domain differential (including statuses 2/13/14, 4096/16384 limits and
   full 268632348-byte profile); host I/O/allocation controls; fuzz minimizations;
   resource measurement. The `--frame` input is a transport hook, not evidence
   those suites exist or ran. Do not call the small suite full-domain coverage.

No runtime readiness, passed test or speed improvement is claimed by these files.

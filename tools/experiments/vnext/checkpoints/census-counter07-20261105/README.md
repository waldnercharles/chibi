# Counter07 inactive source delta

**Source and archive reviews complete; preservation only. Runtime held.** This checkpoint
is source preservation only. No counter generation, project tests, compiler
acceptance, native probes, observed counters or timing acceptance is established.
The original typed SIGSEGV remains unresolved and did not involve these counters.
MaxBlocks64, the v3 generated bridge and the unassigned accepted-v4 digest are
unchanged. No active compiler, backend, default, ABI or capacity is changed.

## Exact reconstruction without execution

This is a two-code-file unapplied patch, not another full source archive.

1. Read `identity.json`. Obtain a fresh copy of its `base_directory` at its exact
   `base_commit` in `waldnercharles/chibi`. Do not edit that published archive.
2. Require exact membership: the 13 files listed in `identity.json`, plus the
   base README whose hash is pinned separately. Reject extra files and symlinks.
   Verify before sizes and SHA256 values. The base README describes counter04.
3. In the fresh copy only, apply `counter04-to-counter07.patch` with
   `patch --batch --fuzz=0 -p1`. Only `WorkProbe.chibi.in` and
   `instrument_prepared.py` may change. Do not import or execute project code.
4. Verify membership again and every listed after size and SHA256. This restores
   counter07 executable source, templates and unchanged dependencies byte-for-byte.
   Use this document as the current technical README. Its normalization replaces
   historical status prose; it is not a byte-identical copy of the author README.

The patch was replayed with zero fuzz in a separate temporary copy, and all 13
non-README source/contract files matched counter07. This checks reconstruction,
not source transformation or execution. `SHA256SUMS` covers this README, the patch
and `identity.json`. The source payload manifest digest identifies the full
author input, not this normalized four-file packet. Historical audits, private
reports, receipts and binaries are not distributed here.

The preserved base documentation is
`../census-refined-20261105/work-counters04/README.md`; counter semantics and bounds
also appear in `../census-20261105/work-counters02/AUTHORED-CONTEXT.md`. These paths
are relative to this checkpoint, not the reconstructed working copy.

## Source-review sequence and corrections

Nine ordered engineering passes were completed across the source revisions,
with independent targeted review of the corrections. This is not a claim that
all original passes independently reviewed an identical final file hash.

1. Performance: no blocking source finding for this disposable probe; actual
   overhead remains unmeasured.
2. Data and representation: invocation-owned counters, partial function rows and
   exact masks retain their stated meanings.
3. Structure and control flow: original compiler operations remain authoritative;
   counted siblings, single-root integration and output are direct and bounded.
4. Failure and lifetime: direct-final preparation writes and discarded negative
   meters were identified. Counter04 staging and counter05/06 rejection assertions
   received targeted source review, closing these scoped source findings.
5. APIs: no additional API finding. Partial rows, absent observations and strict
   output framing remain explicit consumer obligations.
6. Compiler and language: eight implicit nested scalar updates were identified as
   unsupported by retained stage-one lowering. Counter06's expression-local
   address projections received targeted source review, including negative cases.
7. Runtime, VM and data formats: no further blocking source finding, with concrete
   managed-allocation and unverified lifetime/COW limits retained below.
8. Deletion and tests: missing positive verifier-work lower bounds and successful
   inventory/mask checks were identified. Counter07 supplies them; independent
   targeted source review found no new defect and closed both assertion gaps.
9. Style: final counter07 review found no substantive finding or requested edit.

All identified scoped source findings have reviewed corrections. Source-preservation
archive review is complete. Generated-source review, compiler provenance, both-stage
acceptance and all runtime gates remain separate. None of these reviews is an executed test or mutation test.

## What the reconstructed source does

The transformer checks an existing typed preparation against pinned Model,
Produce, Verify, tokenizer, test Main template, Census and strings bytes. It
independently re-expands the pinned original Main and creates disposable counted
producer/verifier siblings. Original preparations and production sources remain
unchanged. Exact source pins, anchor counts and lexical rewriting fail closed.
No generated output has been produced or validated for counter07.

`Counts.chibi` defines invocation-owned counters and partial function inventories.
`WorkProbe.chibi.in` compares original/counted Modules fieldwise and all six
Diagnostic fields, uses fixed-depth 4/16-loop producer inputs, 32/128-block verifier
families in forward/reversed layouts, invalid dominance uses, five rejection
sources and the exact Census input. Producer expectations remain 5N+2=22/82 for
Expression entries and Emit calls. These are predictions, never observations.

The five rejection cases check reached phases and retained partial inventories.
Parser rejection precedes State and leaves a fresh meter empty. Unknown assignment
stops in the write prepass without a body row. Three body failures retain one
incomplete entry-only row, including the bool parameter where present. Header,
alias, empty-header and missing-final-return publication exits are not newly
exercised; no exhaustive rejection coverage is claimed.

Counter06's eight scalar increments use `(&s.meter).counter += 1` at the unchanged
event positions. The meter stays inline by value. No derived pointer is stored
in State, bound to a local, returned or passed to another helper. The recursive
function-array append and seven post-State publication sites are unchanged.

Counter07 adds assertions only. Each successful producer has one complete row,
function ID 1 and one signature parameter; its table deltas equal the returned
Module table lengths. There are N assignment/loop/mask rows, and each mask is
u64(2), derived from the second binding's slot. Checking mask count before iteration
rejects an empty snapshot. Across 4/16-loop fixtures this inspects 20 mask values.
In each valid and invalid-use verifier layout, reachability equals B, dominance
pops and predecessor reads are at least B-1, and word intersections equal twice
predecessor reads. Existing upper bounds remain. This rejects all-zero work
without requiring identical convergence schedules or a linear-time claim.

At cap64 the 128 family remains explicitly skipped, not accepted. Census inventory
covers only bodies reaching Statements; failed prepass and unvisited later bodies
have no row. `complete` describes the body snapshot, not overall Module acceptance.
Partial emission stays complete=false; a full Census inventory may be unavailable.

## Single root and output contracts

Only Main.chibi is an emitted root. Probe and output templates remain `.chibi.in`
inputs. The output imports and helper bodies are embedded into that Main, not
silently provided as adjacent root helpers. Main calls `RunWorkProbes()` once,
passes its actual returned report to `PrintWorkReport`, then runs its entire
unchanged original regression body. `WorkRegressionPassed()` precedes only that
body's successful return. Expectations never replace actual printed counters.

One cold output batch uses native `core:os.WriteOutput`; Print writes stderr.
The adapter asserts Native=1, Linux, X86_64 and PointerBits=64. VM/build-mode output
is not native stdout. The POSIX writer retries short writes, but Os.WriteOutput
discards its boolean result. Acceptance therefore requires complete CW1 schema,
report-end, regressions-passed, exit0 and separately captured/reviewed stderr.
Truncated output fails even with exit0. No per-node printing, mutable production
global, runtime counter mode, new native library or framework is added.

The unchanged seven-file core snapshot includes bytes, SHA256, os, target, POSIX
and both Windows implementation files, covering all conditional branches of
core:os. Strings is independently pinned and reused. Core tests are not copied or
registered, and their execution is not claimed. Target.Native remains its normal
zero placeholder; only normal supported native-target replacement may activate it.
Do not hand-edit it. Future validation must record the compiler invocation,
generated target bytes and source attribution. Ambient filesystem imports, extra
snapshot files, symlinks, hash mismatches and destination collisions are rejected.

## Preparation publication and recovery contracts

The coordinator must exclusively own the existing destination parent and its
unchanged ancestry for the entire call. Inputs, templates, transformer and
snapshots must remain immutable. These are admission prerequisites, not locks
established by the transformer; the parent is never created for the caller.

After validation and in-memory generation, publication allocates one uniquely
owned same-parent `.incomplete-<name>-*` directory. Every payload is exclusively
created, fully written, closed and byte-verified. The manifest is serialized
before staging and written, closed and verified last. Only the complete stage is
renamed to the final name. Existing targets, including empty directories and
dangling symlinks, are refused, also at the final check before rename.

Portable rename is not adversarial no-replace. Check-and-rename requires the
exclusive-parent contract. This supplies atomic namespace visibility on a
supporting filesystem, not fsync or power-loss durability. No concurrent-writer
protection is promised.

A caught staging failure retains the stage, preserves the cause and names stage
and final. Before rename, this invocation has not published final. Rename errors
are conservatively uncertain even if final exists; interruption can also leave
an uncertain result without a diagnostic. Inspect both names and independently
verify intended manifest, membership and all bytes before deciding the outcome.
Existence or a parseable manifest alone is not success. Never automatically
consume, promote, delete, reuse or retry a stage. After reconciliation, any
separately admitted attempt uses a fresh destination. Ordinary consumption still
requires confirmed success and an independently checked receipt.

`test_publication.py` authors 21 data-only scenarios across seven methods,
including actual cached-byte publication, existing/dangling targets, missing
parent, staging allocation, payload/manifest IO faults and short writes,
readback mismatches, pre-rename collision and rename-before/after-effect failures.
They exercise the cached-byte boundary, not a mocked compiler or measured counts.
All remain unexecuted and are not full preparation-integration coverage.

## Runtime limits and remaining gates

Runtime-source tracing identifies at least four managed allocation requests per
selected producer scalar increment on the traced path. Instrumentation is not
allocation-free and cannot establish production timing acceptance. Actual native
lowering, allocation totals, temporary roots, GC, layout, copy-on-write behavior
and elapsed overhead remain unmeasured. Expression-local source syntax does not
prove runtime lifetime safety.

Runtime remains held. Later, separately authorized work must establish exact
compiler executable/closure/seed provenance and both-stage compatibility;
generate a fresh sibling; independently inspect/hash Main, all source siblings,
imports and manifests; verify eight updates, seven publications and original
regression retention; admit a native output-aware execution packet; execute the
publication cases, positive/negative probes and regressions; and capture complete
raw stdout/stderr/exit status with both markers. Independently check observed
Modules, diagnostics, counters, inventories, masks and verifier bounds. All of
these observations are absent here. Cap expansion requires a deliberate pin
rebase and renewed 128/129 and Census gates. v4, Pack, backend, ABI, native checked
reads and aggregate acceptance remain open. No instruction here lifts the hold.

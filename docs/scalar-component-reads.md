# Checked scalar component getters

This is a syntax-neutral Stage 1A compiler foundation for the Studio migration.
It lowers an explicit component-field getter to a checked scalar-copy operation.
It does not inline a C function body or translate Studio behavior.

## Scope and contracts

`BF_COMPONENT_READ` consumes an entity handle and produces a copied bool or
numeric value. The canonical instruction records the component binding and ID,
layout hash, row stride, field offset and scalar width. Authored lowering first
matches the catalog field against the reflected root's name, hash, size and exact
field name/storage/offset/width. Optional, nominal, container, managed-result,
aggregate and unproven shapes retain the reference implementation.

Each execution reacquires the current world, validates the entity slot,
generation and occupancy, finds the current component column, checks component
membership, and requires the exact layout and stride. Bool field reads perform
these layout checks too; existing bool component-presence operations retain their
different contract. Bound column caches and binding worlds do not supply this
read. Structurally valid execution-view storage remains an engine ABI invariant.
No row pointer escapes or survives a storage-changing operation.

The VM and C exporter copy scalar bytes. SLJIT and generated Wasm emit checked
loads directly on success. Wasm uses the canonical VM helper only on the failure
branch. Unavailable storage preserves `BEHAVIOR_FUNCTION_HOST_DATA`, the message
`Referenced storage is unavailable`, result nonpublication, and source/node/frame/
stack/pin/step metadata. Success clears only the old message. Range annotations
are owning-write contracts: this read does not clamp stored values.

The compiler retains unused fallible reads and does not common-subexpression
eliminate them across intervening writes. Existing getter component-write
metadata and conservative function scheduling proof are retained. Module read
dependencies remain visible. This change does not broaden imported-helper or
parallel-system eligibility.

Ordinary `self.Component.field` and query-member reads still produce their
existing whole-component snapshots. In inspection, a root observer can mutate the
world after observing a copied root; retaining that snapshot is observable.
Explicit scalar getters have no intermediate root observation and do not retain
unrelated managed fields. Escaping references and managed values remain generic.

Prepared schema identity includes the actual stable-name/ordinal map, now with
both `QUERY_VARIABLE` and `COMPONENT_READ`. The shared declaration keeps the query
opcode at its current ordinal and adds the scalar read after `COMPONENT`.
The manual payload/semantic version remains 13: no existing instruction payload
or execution meaning changes. Older opcode layouts are rejected by the schema
identity. Rebuild prepared artifacts from source. Authored projects need no
contract migration or project-version bump.

The original feature branch started at `02f8453211dd4cc57965ba52c645f08a92e53c5e`.
The refreshed candidate preserves that ancestry and merges eligible main
`7885af0cd5a7e1c8600a6bd9e3db0fd8d88c1cf4`, including query-variable lowering,
prepared opcode-schema identity, capture retention and formatter fixes.

## Historical baseline and measurement method

The pre-optimization fixture is preserved in commit
[`0ab1488f85c16839afd34b3b6870a774de1eb123`](https://github.com/waldnercharles/chibi/commit/0ab1488f85c16839afd34b3b6870a774de1eb123), tree
`54320bd1309ef406261e997ec68283cf5a76e2cd`. It uses the actual production reference
callbacks and authored lowering, before changing production code. Native and Node
Wasm baseline selectors passed respectively 18 cases / 7,191 assertions and
18 cases / 7,411 assertions, in 0.062 s and 0.195 s under the production
supervisor. Builds/configuration were separate.

Six small source functions compare member/getter paths for signed I32 (including
an out-of-range stored value), high-bit U64 and F32 negative zero. They run on the
VM and actual compiled entries with inspection disabled/enabled. Cold allocation
counts include preparation and first execution. Five trials of eight invocations
then reuse the canonical bank and VM storage while resetting the value arena.
Timing includes correctness assertions and describes only this tiny diagnostic
fixture. It is not a representative Studio workload, steady ECS query-row loop,
or comparison against well-written C.

Baseline ordinary members execute two reference helpers and reserve 14 bank
values; getters execute three and reserve 19. Component primitives account for
12 and 17 respectively. Two additional writable-return cells and one unexecuted
snapshot descriptor belong to return plumbing. Removing component bridges does
not imply that every function bank or runtime allocation disappears.

## Historical focused measurements

These measurements predate the main/schema/query integration. They describe
the original scalar candidate, not the refreshed combined candidate.
The machine-readable [640 raw records](scalar-read-measurements.json) retain
baseline/candidate structural, cold-allocation and per-trial measurements. The
candidate production diff from the baseline has SHA-256
`039c99a625b978f26cc66a4cbf556890289e74ee20e6a7c334d17d559e59d965`.
This identifies the original measured production sources without a
self-referential commit ID. The raw records are retained unchanged.

For the uninspected I32 getter, executed helpers fall from 3 to 0 and reserved
bank values from 19 to 2. Ordinary members remain at 2 helpers and 14 values.
The getter's existing write/effect annotation remains unchanged. Median timings
below are assertion-inclusive microseconds per read, not a general speed claim.

| Execution | Baseline → candidate µs/read | Cold allocations | Cold requested bytes | Scalar-read arena bytes |
| --- | ---: | ---: | ---: | ---: |
| Native VM | 0.290500 → 0.121375 | 12 → 7 | 4,544 → 1,912 | 344 → 0 |
| Native SLJIT | 0.076375 → 0.042625 | 10 → 5 | 3,856 → 1,544 | 344 → 0 |
| Node generated Wasm | 0.533375 → 0.255375 | 11 → 7 | 2,816 → 1,240 | 256 → 0 |

Both baseline and candidate reuse their allocations in the steady trials, so
both record zero heap allocation calls there. The reduction above is in the
cold preparation/first-call fixture. Neither the remaining return bank nor the
VM's own storage has been eliminated.

## Validation scope

The independently reviewed refreshed validation uses the existing native
language and Node Wasm groups,
plus explicit native/Wasm query-program selectors for the shared SLJIT
entity/column guard refactor. Generated C is compiled and executed by
`c-export-alias`; the prepared producer/runtime-only consumer is exercised by
`language-prepared`; the bounded native tooling group checks schema identity and
shared runtime contracts. Run the combined workloads on the reviewed candidate and report their actual
results.

The scalar fixture covers typed copies, invalid IR, missing/stale storage,
recovery, bool layout checks, occupancy word boundaries, hash collisions, live
registers, unused failures, budgets/branches, read-write-read and debug snapshots.
The combined scalar/query case builds one 12-instruction definition and makes
20 calls across VM/compiled and inspection modes. It chains query handles into a
scalar read, keeps integer/floating/handle values live across both operations,
then rereads the tuple. Missing tuples and stale entities keep distinct failure
messages, leave all results unpublished, and recover in the same context.
The generated-C fixture retains two four-instruction query functions/16 calls
and three five-instruction scalar functions/132 calls within its existing single
compiler process, 10-second child limit and 128 KiB source cap. No additional C
fixture or compiler child is added by the refresh.

The original measured candidate passed these independently selected checks.
These are historical results, not refreshed validation. Builds and
configuration are excluded from the production supervisor's wall times.

| Check | Cases | Assertions | Wall time |
| --- | ---: | ---: | ---: |
| Native ASan/UBSan language group | 347 | 27,709 | 14.039 s |
| Node Wasm group | 206 | 26,303 | 3.053 s |
| Native ASan/UBSan query-program | 18 | 736 | 0.216 s |
| Node Wasm query-program | 18 | 736 | 0.248 s |
| Native Release scalar fixture | 25 | 9,685 | 0.057 s |
| Node Wasm scalar fixture | 25 | 9,843 | 0.184 s |
| Generated C (ASan/UBSan driver) | 1 | 2,249 | 0.602 s |
| Prepared producer/runtime-only consumer | 19 | 833 | 0.210 s |

The refreshed committed head must pass the independently reviewed checks before
landing; its exact-head review and results will be reported in the pull request.
Browser execution, full-engine/Studio suites, GPU behavior, other architectures
and leak detection are not established by Node and focused SDK checks. The
original cloud-browser attempt had a verified sandbox-startup blocker;
browser execution has not been attempted for the refresh. Native ASan/UBSan
checks disable leak detection because LeakSanitizer could not start under the
original tracing setup; no fresh leak-detection coverage is claimed.

## Refreshed integration author validation

The independently reviewed matrix passed on clean local merge
`6b6369f89d2301c127b2f01068fa2e58ccee5e46`, source tree
`7bd613ffc2772b307cc1dd22a6e2d59557aaf75a`. Its parents are the original scalar
candidate `b26d743f64b2a1c2b103c9a65bc8907925ec006c` and eligible main
`7885af0cd5a7e1c8600a6bd9e3db0fd8d88c1cf4`. The report added afterward changes
only this document; independent review and checks on the exact published head
are still required before landing.

| Check | Cases | Assertions | Wall time |
| --- | ---: | ---: | ---: |
| Native ASan/UBSan language group | 382 | 35,964 | 29.913 s |
| Node Wasm group | 226 | 32,081 | 11.479 s |
| Native ASan/UBSan query-program | 28 | 3,373 | 0.555 s |
| Node Wasm query-program | 28 | 3,390 | 0.841 s |
| Generated C (ASan/UBSan driver) | 1 | 2,378 | 0.640 s |
| Prepared producer/runtime-only consumer | 19 | 833 | 0.265 s |
| Native ASan/UBSan tooling group | 116 | 29,566 | 24.118 s |

Each invocation used the production `tools/test_policy.py` supervisor; configuration and builds were separate. Native language and
tools used Clang 19.1.7 Debug ASan/UBSan with leak detection disabled. Generated
Wasm used Emscripten 6.0.10 Release and Node 24.19.0. All three currency builds
reported no work before execution, and the tracked checkout remained clean.
These correctness checks include inherited capture/formatter/schema regressions;
they establish no fresh comparative performance or browser result.

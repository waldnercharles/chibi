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

Prepared format version 14 rejects the previous opcode numbering. There is no
authored project contract change and no project-version bump. Rebuild prepared
artifacts from source. This independent feature branch starts at
`02f8453211dd4cc57965ba52c645f08a92e53c5e`; it does not incorporate the separately
held query-variable feature. If both land, reconcile prepared versions/opcode
numbering and rerun the reviewed checks on their exact combined head.

## Baseline and measurement method

The pre-optimization fixture is preserved in commit
[`0ab1488f85c16839afd34b3b6870a774de1eb123`](https://github.com/waldnercharles/chibi/commit/0ab1488f85c16839afd34b3b6870a774de1eb123), tree
`54320bd1309ef406261e997ec68283cf5a76e2cd`. It uses the actual production reference
callbacks and authored lowering, before changing production code. Native and Node
Wasm baseline selectors passed respectively 18 cases / 7,191 assertions and
18 cases / 7,411 assertions, in 0.062 s and 0.195 s under the production 60-second
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

## Focused measurements

The machine-readable [640 raw records](scalar-read-measurements.json) retain
baseline/candidate structural, cold-allocation and per-trial measurements. The
candidate production diff from the baseline has SHA-256
`039c99a625b978f26cc66a4cbf556890289e74ee20e6a7c334d17d559e59d965`.
This identifies the measured production sources without a self-referential
commit ID; the pull request records the final tested head.

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

Independent review selects the existing native language and Node Wasm groups,
plus explicit native/Wasm query-program selectors for the shared SLJIT
entity/column guard refactor. Generated C is compiled and executed by
`c-export-alias`; the prepared producer/runtime-only consumer is exercised by
`language-prepared`. Every test command retains its shared 60-second deadline.

The scalar fixture covers typed copies, invalid IR, missing/stale storage,
recovery, bool layout checks, occupancy word boundaries, hash collisions, live
registers, unused failures, budgets/branches, read-write-read and debug snapshots.
The generated-C fixture adds three five-instruction functions and 132 actual
calls within its existing single compiler process and 128 KiB source cap.

The measured candidate passed these independently selected checks. Builds and
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

The final committed head must repeat the selected checks before landing; its
review and results are reported in the pull request.
Browser execution, full-engine/Studio suites, GPU behavior, other architectures
and leak detection are not established by Node and focused SDK checks. The cloud
browser has a verified sandbox-startup blocker; ASan/UBSan can run with leak
checking disabled because LeakSanitizer cannot start under the tracing setup.

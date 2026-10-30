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

## Checks

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

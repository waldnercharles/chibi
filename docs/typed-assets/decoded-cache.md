# Decoded asset module cache

## Scope

This change is optional to the compiled asset migration. It retains decoded
prepared modules at the existing native compiler-host seam. It changes no asset
syntax, source policy, compiler resource, prepared ABI or capability profile.
The migration can be reviewed and accepted separately.

The earlier cache retained serialized closures, then decoded and validated them
on every read. A process sample during shared prototype expansion showed that
repeated loading in the hot path. The interrupted expansion run is incomplete
evidence; it establishes neither a failure nor a before/after speedup.

## Ownership and execution

- Exact source and path identify a memory entry within an immutable compiler,
  collection and library snapshot. Snapshot changes discard cached entries.
- Every read invokes Build with a fresh arena and execution scope. Evaluated
  documents are not cached. Current instruction budgets and cancellation apply
  to cache hits, including nested calls.
- An invocation leases its immutable decoded host. Eviction, thread release or
  a nested read replacing the snapshot cannot free that host while it is active.
  Generation checks prevent outer work publishing into a replaced snapshot.
- The existing 128-entry / 128 MiB asset cache accounts for serialized bytes,
  source/path keys, decoded canonical package storage, native tables and lease
  wrappers. Detached active asset leases remain charged until their final return.
  Oversized modules execute transiently without entering the cache.
- The storage getter measures canonical bytecode storage, not process RSS or
  generated machine code. The shared compiler facade remains a separate cached
  host. No quota or capability was increased.

The implementation is in `src/authoring/asset_compiler_host.c`. Read-only storage
getters in `src/toolchain/compiler_host.c` and
`src/runtime/native_capabilities.c` keep their private layouts private.

## Local evidence — 2026-10-22

Release native drivers, sanitizers off, ABI 20. Build/configuration time is
separate from test-command wall time. Source review covered invocation state,
aggregate copies, leases, reentry, invalidation, accounting and failure paths.

| Existing selector | Cases / assertions | Wall time | Log under `.chibi/` |
| --- | --- | --- | --- |
| asset-cache | 2 / 60 | 10.692s | `asset-decoded-asset-cache-local-copy.log` |
| execution-scope | 2 / 120 | 0.702s | `asset-decoded-execution-scope.log` |
| asset-source | 6 / 84 | 5.143s | `asset-decoded-asset-source.log` |
| typed-prototype | 7 / 130 | 10.655s | `asset-decoded-typed-prototype.log` |

The cache cases verify one preparation across repeated fresh evaluations,
nested union/array mutation without changing the original, budget failure,
cancellation after execution starts, successful recovery, thread release while a
lease is active, and nested snapshot replacement. Active storage remains charged
within the cap and returns to zero after release. Existing compiler/library/path
invalidation and corrupt persistent-record recovery assertions remain.

The first attempts stopped in process-reader preflight or rejected unsupported
test-fixture record/pattern mutation syntax. Their logs are retained; they are
not passing cache evidence. The final fixture uses supported union payloads and
an ordinary local array copy with the same original-value and result assertions.

Native SDK packaging passed at identity
`a075fb84c7889508597d95c3f98c6415099b199b87a30d2c8716370ceac7b03f`.
This is a local checkpoint. Remaining project routes, refreshed Web/Studio
consumers, platform checks and final main integration remain outstanding.

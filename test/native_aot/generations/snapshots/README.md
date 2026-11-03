# Native retained snapshot ABI

The raw fixtures call `llvm_native.EmitSnapshotLibrary` directly. The `Source*`
fixtures compile ordinary package source through the same transaction API.

`Probe.chibi` emits the production callbacks and runs `Runtime.ll` through the
existing poisoned allocator at O0 and O2. It covers transactional publication and
abort, malformed schema/version, duplicate stage rejection, type splitting and
merging, aligned inline stage storage, repeated release, embedded NUL text,
shared record/string/primitive-array values, a 100-level inline record, and a
10,000-level array graph. Sixty-four identical roots add no clone allocations.
The final allocation count must be zero.

`Build.chibi` uses the production object/link path to emit three images. The real
host rejects an incompatible field name, accepts reordered native type ordinals,
preserves shared owned values, and keeps Main from running again. The old image
writes an unload marker from its destructor; the new Frame requires that marker
before reading its migrated state. This proves that the snapshot owns its bytes
independently of the old image.

Snapshot schemas compare ordered field and variant labels and a bijection of
type nodes. Layout evolution inside an opaque root, including splitting or
merging types, rejects the incoming generation. Existing flattened retained
records keep their named-leaf migration rules. A transaction clones each unique
source data/length/target-type descriptor once; distinct slices are separate
keys. Pointer/view/task/map/resource state remains rejected.

Run through `generations/run.sh`, or pass a matching native host explicitly:

```sh
sh test/native_aot/generations/snapshots.sh COMPILER LLVM_BIN LEVEL HOST OWNED_WORK
```

The runtime/live harness currently runs on Linux x86-64 and macOS arm64. Windows
callback exports and pointer widths are covered by emission review; this fixture
does not claim Windows execution.

`source.sh` compiles retained string arrays, array-backed record arrays and union
payloads. The real host rejects a changed record schema, then accepts native type
reindexing and outer plain-record field reordering/new initialization. Main runs
once and a local copy survives subsequent mutation. `SourceAudit.chibi` also runs
at O0/O2 in its production shared context and execution-family heap with the
common poisoned allocator. It calls Frame three times, bounds each 4,096-element
retained string/record-array growth to 16 backing allocations. A 1,000-row
retained nested array has the same bound for 4,096 inner appends, while a retained
optional array performs 4,096 guarded element writes without allocation. It
checks that a saved nested value survives mutation, releases twice,
and requires zero live allocations. No GC-pointer retained state is admitted.

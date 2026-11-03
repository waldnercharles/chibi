# Publication-boundary Text ownership

Run only `sh test/native_aot/managed_pointers/text-import.sh COMPILER LLVM WORK`
through the approved bounded controller. This focused runner never invokes the
managed-pointer or task suite. It does not register or widen language admission.

## Exact inventory

- `Labels.Main`: existing rootless `Row { labels: string[] }`, shared inner
  backing, element publication, whole-array replacement, array-argument
  constructor, scoped row set/append/extend, self-assignment, and snapshot isolation.
  Labels also covers scoped whole labels-field replacement, field append/extend,
  indexed Text replacement, and field reacquisition after a scoped RHS replaces
  the outer array backing, with a distinct four-label RHS value.
- `Leaf.Main`: existing rootless `Row { leaf: Leaf { label: string } }`, the same
  publication boundaries through scoped row set/append/extend, repeated counted
  Text owners, and snapshot isolation.
- Both fixtures mix borrowed `writer\u0000literal`, counted `A\u0000Z`, and empty
  Text. Their first section is the writer-only source. The extraction marker
  excludes `Constructor` and `Main`; the emitted writer must contain no cell
  construction calls, and must contain Text copy/share and family support.
- `Copies.Main`: 4096 ordinary counted copies of two rows sharing 1024 counted
  Text elements. The canonical allocator audit must report at most 64 total
  allocations, independently of the number of copies or nested elements.
- `Graphs.DAG`: 15 distinct binary-backed levels, 32768 unfolded leaves, and
  16 backing ranges including the outer array.
- `Graphs.Deep`: 15 finite single-child backed levels, the maximum admitted
  source depth: the existing metadata limit is 16 records including Leaf. This
  is not a claim to prove 128-level, arbitrary-depth or recursive-source-type
  acceptance. Over-limit source remains rejected; the production gate is unchanged.
  Both graph entries retain a source snapshot and perform self-assignment after
  cell publication. Their audit stops before allocation 513, so a regression
  cannot expand the DAG without hitting a hard allocation limit.

`SourceProbe` and `SourceAudit.ll` remain the canonical poison/reclamation
infrastructure. Labels and Leaf each run at O0/O2 with normal and forced
collection, with and without allocator/release collection callbacks (16 runs).
Copies and the two graph entries run at O0/O2 with normal/forced collection
(12 more runs). Their allocation limits apply to the entire fixture, not just
the importer. Source emission, LLVM compilation and runtime results must be
reported separately; an unexecuted fixture is not a pass.

There are **68 runtime invocations**: 36 positive image cases, four one-site
ViewSet bypass controls, 16 semantic normal/forced/callback cases, four counted
copy cases, and eight graph cases. No invocations of unrelated fixtures are
included.

## Two-image lifetime proof

The full source fixture supplies the old image's actual array-argument
constructor. Its writer-only prefix supplies actual source `Rows`, `Write`, and
`Replace`. The adapter resolves functions from emitted source-name metadata and
requires matching semantic view schemas; it does not guess function ordinals.
The old image's adapter initializes zero rows, calls the source constructor,
and registers the resulting descriptor through the same managed-root API as
the existing image tests.

Positive image case names are explicit in `text-import.sh`:

- Both shapes: `view`, `replace`, `constructor`, `scoped_set`, `scoped_append`,
  and `scoped_extend` (12 shape/boundary pairs).
- Labels only: `scoped_field`, `scoped_field_append`, `scoped_field_extend`,
  `scoped_text_set`, `scoped_rhs`, and `aliases` (six more pairs).
- Each pair runs at O0/O2, for 36 positive image invocations.

Scoped functions must emit the borrowed-descriptor `i32(ctx, ptr)` ABI and
must not return or retain their pointer. The adapter resolves the old cell
through `chibi_managed.address` immediately before the actual scoped call.
Nested append/extend/indexed Text cases start with constructor-imported Rows,
so they mutate shared populated backing, not only an empty descriptor.
`scoped_rhs` starts with zero rows, replaces the entire array during RHS
evaluation, and then writes the first row's labels field. Its final two rows, four first-row labels and 48-byte total distinguish the
field store from omitting it: the replacement Rows alone has three first-row
labels and 34 bytes. Source Main
also checks that an earlier snapshot survives this replacement and self-assignment.
Expected row counts, first-label counts, and byte totals are checked separately.
Every published nonempty Text descriptor must have a counted owner, and every
byte is checked, while both images remain mapped. The constructor also verifies
that already-counted Text keeps its original owner.

Labels `view` records the actual nested backing address and reference count at
the source Write's consuming import seam. This nonallocating observer does not
retain or change the value. The host requires that the incoming backing was
unique and that its address did not change during import/publication.

Labels `aliases` constructs 64 distinct labels backings and then appends all 64
aliases in reverse order. Its old-image constructor imports 128 rows containing
384 Text descriptors. The importer's queue exceeds its initial capacity, and
the distinct typed memo keys exceed the initial memo table. The host checks all
Text descriptors and requires each late reverse alias to share the corresponding
imported backing. Source Main also verifies snapshot isolation after mutation.
The setup uses only unboxed-local appends, preserving borrowed Text until the
constructor boundary.

Only after ownership/content/sharing checks and zero writer cell leases may the
host close the writer, read again, release the old root, collect, verify zero
old leases and zero audited allocations, and close the old image. The allocation
counter belongs to the host, so releasing cross-image storage never reads an
unmapped allocating-image global.

The negative module removes exactly one publication import call inside the
actual source `Write`, preserving the incoming value acquisition/move and all
cleanup. Its four O0/O2 shape runs must report status 33 and the exact
`boundary Text owner missing before unload` diagnostic. On any owner/content
failure, the host drops the root and collects while both images remain mapped,
checks both cell counts and the shared allocation count are zero, and returns
without either `dlclose`. Process teardown
then removes the images. No borrowed bytes are read after unload, no descriptor
is fabricated, and no failed ownership check can reach the normal unload path.

The collector-bearing `Row { pointer: ^int, labels: string[] }`, retained values,
map graphs, opcode/ABI changes, and broad suite registration are outside this
fixture's scope. No previous unidentified union-array fixture is used.

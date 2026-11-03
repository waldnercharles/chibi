# Native value search and removal

`run.sh COMPILER LLVM [ORDINARY_COMPILER]` runs the original record-array
sample and source cases at O0/O2, argument-order and authored-shadow controls,
poisoned ownership checks, and an allocator callback that collects a live
pointer array during composite comparison. Scalar and string search do not
allocate equality scratch. Composite searches reuse a queue and pair table
across candidate elements, then release both.

Array `.remove_item(needle)` uses that same search and consuming removal.
It evaluates the receiver place and needle once, then resolves the current
receiver after argument effects. Only the first match is removed; misses keep
the backing unchanged. `Methods.chibi` checks late receiver replacement, aliases,
fresh and self-aliased needles, nested/inline string fields and structural map
values. `MethodReentry.chibi` collects unrelated pointer-array owners during the
comparison's scratch allocation. Task-observer methods compare handle identity
and use the same copy-on-write backing as indexed mutation; their completed
promise, allocation and reload controls live in `task_owners/run.sh`.

The Windows workflow executes `Values`, `Ordering`, `Shadow` and `Maps` at O0/O2
with the fresh stage4 compiler. Its type-rejection control preserves an
existing output. The raw borrowed-descriptor and poisoned allocator probes
currently run on POSIX; Windows execution is established by the hosted run,
not by a cross-link.

`runtime.sh COMPILER LLVM` also exercises the explicit native metadata API.
It checks first-match search, active union payloads, embedded NUL bytes, IEEE
NaN behavior, descriptors sharing different-length prefixes, malformed shape
and instruction metadata, scratch reuse, and recursive values of depth 2500
and 10000. The pair key is both borrowed addresses, shape and extent; no
backing-identity shortcut bypasses element comparisons. A desktop bucket is
32 bytes. Epochs reset membership without clearing each earlier capacity.

The ordinary runtime's `pin_value_equal` defines the expected active-payload
and IEEE rules. The retained reference compiler confirms NaN behavior even
when both values share the same array backing. Its older union-array removal
implementation has a separate copy defect: a `Choice.Boxes(Box[])` element
matches before removing a preceding `Choice.Empty`, but fails to match after
that removal. `Values.chibi` checks the intended active-payload result after
removal independently, rather than reproducing that defect. The optional
ordinary invocation therefore checks the unchanged argument-order case;
the union post-removal result is a native semantic regression.

Composite equality accepts only the existing validated snapshot value kinds.
Pointers, tasks and views remain outside that structural comparison domain.
Map shapes have one key and one payload edge. Source `contains` and functional
`remove_item` use the same graph for map-valued elements through their existing
collection carriers. Retained snapshot roots still reject map-bearing values; comparison
nodes outside those roots do not emit snapshot copy helpers. Generic native entries reject erased composite search metadata;
explicit value entries validate the shared shape graph before emitting code.
The new collector pins cover live pointer-array owners around composite
equality allocation. Other allocating operations retain their own contracts.

The raw map controls compare associations independently of insertion order,
including a 64-entry indexed table, tombstones, empty backed maps, binary string
keys, nested maps and array/record payloads. NaN values remain unequal even
when both operands share their backing; signed zeros compare equal. Comparison
borrows canonical runtime map descriptors, whose cached key hashes and index
follow the existing map lookup contract. Scratch allocation stays at the two
reused session buffers across these controls.

`Maps.chibi` checks association order, replacement/duplicate keys, nested values,
IEEE values, first-match removal, surviving ownership and named-argument effects.
`MapsOracle.chibi` executes ordinary equality and an explicit entry-copy removal
loop. The recovered ordinary runtime has a separate map-array copy defect:
`remove_item` selects the correct row but zeroes surviving map payloads. Native
post-removal assertions preserve the documented values instead of that defect.
The shared-family fixture mixes retained `Box[]` roots and comparison-only map
nodes, keeping snapshot ownership closed. `ContainsReentry` covers unrelated
live collector roots during the new composite-membership callback path.

The source graph interns immediate map types once through a zero-sentinel
128KiB index, allocated only for graphs that reach maps. Retained admission
keeps its existing map-free query; structural comparison uses the same bounded
value traversal with counted map edges enabled. Task identity operations keep
their separate exact storage case and never enter this structural graph.

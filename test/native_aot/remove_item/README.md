# Native value search and removal

`run.sh COMPILER LLVM [ORDINARY_COMPILER]` runs the original record-array
sample and source cases at O0/O2, argument-order and authored-shadow controls,
poisoned ownership checks, and an allocator callback that collects a live
pointer array during composite comparison. Scalar and string search do not
allocate equality scratch. Composite searches reuse a queue and pair table
across candidate elements, then release both.

The Windows workflow executes `Values`, `Ordering` and `Shadow` at O0/O2
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
Pointers, tasks, maps and views remain outside that structural comparison
domain. Generic native entries reject erased composite search metadata;
explicit value entries validate the shared shape graph before emitting code.
The new collector pins cover live pointer-array owners around composite
equality allocation. Other allocating operations retain their own contracts.

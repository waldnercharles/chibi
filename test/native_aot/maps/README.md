# Native map behavior

Run `sh test/native_aot/maps/run.sh NATIVE_COMPILER LLVM_BIN` from the checkout,
using a native compiler rebuilt from the current source.
The native compiler builds an authored probe, which calls the real LLVM emitter,
verifies and links its map operations, then runs them at O0 and O2. No C compiler
or VM is involved.

Expected keys and values are plain lists. The program checks empty and missing
keys, replacement order, deletion and reinsertion, compacting deletion churn,
independent map copies and insertion-order iteration across dead entries. Live
position checks reject indices outside the span, and reading a dead position's
key or value must call the runtime's index failure. Owned
strings check binary keys, duplicate-key replacement and string references held
by copied maps. The allocator counts all outstanding allocations after the maps,
temporary arrays and strings are dropped. Freed bytes are poisoned to expose
borrowed string reads after their temporary map has been dropped.

A second program lowers canonical `scalar_facts` rows through `scalar_llvm` for
map literals, copies, mutation, missing keys and owned strings, including nested
short-circuit paths. Six malformed
fact tables check key types, arity, result types, literal pairs, noncanonical input map types and mutation
outside an assignment. A read-only empty-map program keeps map helper runtime
dependencies covered even when the entry needs no context.

This checks integer and string map instructions and facts lowering. The separate
`test/native_aot/map_programs` suite compiles and runs authored map expressions
and is included by this entry point.

Authored iteration covers key/value and value-only bindings, deletion and
reinsertion order, tombstones, snapshots during direct and reference mutation,
nested loops, break/continue and returning strings from temporary maps. The
ownership source is also compiled to LLVM and rerun through the poisoned
allocator at O0/O2, requiring zero outstanding allocations after every exit.
Numeric/bool record values cover nested records, copied reads, aliases,
replacement, growth, deletion and reference replacement. Missing record keys
must report `MapKeyAbsent`; they never synthesize a zero record.
Authored union values use the same ownership closure for their active string,
record and array payloads. Their missing keys also report `MapKeyAbsent`.
`Unions` covers copied/temporary reads, tag replacement, duplicate keys,
iteration and growth; `UnionMissing` checks the failure at O0 and O2.
Optional map values preserve missing-key nil separately from a stored nil
entry. `Optionals` checks presence, numeric/string/record/union payloads,
copies, temporary results, iteration, growth and guarded string ownership.
The same aggregate get helper takes an explicit zero-default mode; ordinary
record and union lookups retain their required-key failure.
Evaluation-effect cases also require a read-only argument to survive a later
argument replacing the same map. Set/delete capture their target's address
and reload it after key/value evaluation. These run under the poisoned allocator.
Read, `has`, `in` and `not in` evaluate their receiver before a key callback.
Replacing the original map during that callback preserves the captured value,
including through aliased scoped parameters and binary string keys.
Managed record values share the aggregate lifecycle for nested scalar/string
fields. Copy, replacement, deletion, growth/shrink, temporary results and loop
exits must retain each live payload and release it once. Array-backed record
values use that same closure, including nested records and string arrays.
Borrowed string and record array elements also survive later literal callbacks
that replace the source element before the map is constructed.
String variables used as map keys also survive later callbacks that replace
the variable, including differently named scoped aliases and conditional calls.
Array payloads cover numeric, bool, string, record and union elements through
the existing array lifecycle. The native emitter checks independent mapped
array edits, missing-key empty arrays and values escaping a dropped map.
Authored array payloads support reads, literals, copies, insertion/deletion,
iteration, calls/returns and projected append/extend/remove/resize/element
assignment. Callback arguments finish before a projected update resolves its
current map entry. Readonly iterator buffers passed to C use independent loans.
Projected map foreign buffers and compound mapped-array element assignment
remain closed. ArrayValues exercises borrowed and fresh owned elements,
duplicate keys, copies, scoped aliases, growth, deletion and record shrinking.
String arrays grow with empty elements; authored record growth requires a
separate failure-parity check before the final retirement gate.

A growth/shrink regression builds 4096 entries, deletes all but 64 and checks that logical entry/index capacity shrinks with the live map. Capacity planning near 2^30 is checked without allocating those buffers.

Pass an ordinary build host as the fourth argument to `run.sh` to compare the
array-map source fixture with the bytecode consumer while it remains in use.
It checks that temporary-map projections keep their arrays before releasing
the map and that array index callbacks read the receiver's earlier value.
Foreign-buffer cases use real native C calls; the retained ordinary host lacks
that capability and its older test runner skips foreign imports.

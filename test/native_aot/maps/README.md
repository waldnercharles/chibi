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

A growth/shrink regression builds 4096 entries, deletes all but 64 and checks that logical entry/index capacity shrinks with the live map. Capacity planning near 2^30 is checked without allocating those buffers.

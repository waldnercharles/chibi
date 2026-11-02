Run `sh test/native_aot/managed_values/run.sh NATIVE_COMPILER LLVM_BIN` on macOS
arm64 or Linux x64. It compiles real source at O0 and O2, poisons freed bytes,
and checks that every context allocation is released.

The program covers string-bearing record snapshots, nested records, dynamic NUL
bytes, copy and replacement, self-assignment, borrowed parameters, written
references, returned temporaries, scope exits, arrays of records and union
payloads. Record arrays exercise element replacement, append, copy, slice,
resize, removal and temporary-element reads. The baseline compiler leaks on this
program and returns 99 through the allocator audit.

The native generation and CLI watch suites separately exercise retained records
with strings across live edits, field reordering, new-field initializers and
incompatible schema rejection. Arrays and unions remain unsupported as retained
bindings. Records that hold array backing still need recursive ownership before
they can participate in this counted aggregate path.

The emitter probe separately exercises native `ValueCopy`/`ValueDrop` directly:
an owned text record survives dropping its original, its array copy survives
dropping the source array, and all owners are released. Invalid scalar operands
are rejected. Native array ranges and map payloads share the generated record
helpers; frontend record expansion remains specialized until array backing
joins the same lifecycle.

The native array probe shares counted backing, then checks set/remove, append
with and without growth, self-extension during growth and release order. A raw
caller-owned array has a null owner; the probe imports it before mutation and
checks that releasing the borrowed descriptor neither frees nor changes it.
The descriptor is now 24 bytes, with its data pointer still at field 2. Source
array copies remain eager until foreign calls carry writable-place leases; no
array descriptor currently crosses the generation host's scalar/text ABI.
A unique writable loan also appends inside existing capacity without creating
another allocation; this catches repeated copying of compiler record fields.

Foreign buffers carry an owned argument snapshot and its writable descriptor
cell until the call. The source regression uses actual libc calls to check an
earlier buffer surviving replacement by a later argument, ordinary parameter
isolation, scoped writes and temporary arrays. The native probe writes through
one of two aliases of the same cell while reading the other, after a later
ordinary array copy; both C pointers must agree and the copy must stay unchanged.
Source foreign record fields remain rejected until recursive array-backed
record ownership is available. Indexed places also need a writable path resolved
after later argument evaluation. Locals and temporary arrays are supported.
The rejected Box fixture keeps the caller-unchanged assertion as an acceptance
case for the next ownership step.

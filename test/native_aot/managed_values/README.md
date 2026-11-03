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
bindings. Ordinary records and unions that hold array backing share the same
value lifecycle as scalar/string aggregates. `Backed.chibi` checks independent
snapshots, field mutation, managed record arrays, mixed string/array union
payloads, force-unwrapped records surviving later optional replacement, and
teardown of a 10,000-deep recursive union value. Recursive record
declarations keep the ordinary compiler's existing rejection.

The emitter probe separately exercises native `ValueCopy`/`ValueDrop` directly:
an owned text record survives dropping its original, its array copy survives
dropping the source array, and all owners are released. Invalid scalar operands
are rejected. Native array ranges and map payloads share the generated record
helpers. Scalar lowering uses the returned owned value whenever copying can
replace a borrowed descriptor. The raw record-argument probe imports a borrowed
array field, isolates mutation and verifies complete release.

The native array probe shares counted backing, then checks set/remove, append
with and without growth, self-extension during growth and release order. A raw
caller-owned array has a null owner; the probe imports it before mutation and
checks that releasing the borrowed descriptor neither frees nor changes it.
The descriptor is now 24 bytes, with its data pointer still at field 2. Source
array copies share owned backing and detach at writable boundaries; caller-owned
backing is imported. No array descriptor currently crosses the generation host's
scalar/text ABI. Flat element
backings have an eight-byte reference count. Backings of array-bearing record
elements reserve 32 bytes; only their last release uses that dead header as a
pending node in a stack-owned drain queue. No callback or context survives the
synchronous drop, and value depth does not consume native call depth.
A unique writable loan also appends inside existing capacity without creating
another allocation; this catches repeated copying of compiler record fields.
Explicit native `ArrayCopy` still clones for raw foreign ABI consumers.
Native `ValueCopy` shares, so its callers must take an `ArrayLoan` before exposing
a mutable buffer. Raw `ForeignCall` supplies no uniqueness or descriptor writeback.

Foreign buffers carry an owned argument snapshot and its writable descriptor
cell until the call. The source regression uses actual libc calls to check an
earlier buffer surviving replacement by a later argument, ordinary parameter
isolation, scoped writes and temporary arrays. The native probe writes through
one of two aliases of the same cell while reading the other, after a later
ordinary array copy; both C pointers must agree and the copy must stay unchanged.
Inline authored record fields use a typed reference chain into their canonical
record storage. The foreign loan preserves the earlier buffer while later
arguments can replace that record. `ForeignPlaces.chibi` checks deep fields,
by-value isolation, scoped writes, repeated arguments and later replacement.
Direct retained record buffers and indexed container places still require
separate writable-path and retained-lease handling.

`Growth.chibi` counts context allocations for 4,096 appends through a direct
record field and through a scoped record parameter. Each path allows at most
16 allocations, so spare capacity must be reused rather than cloned for every
append. Another 4,096 readonly source array snapshots must allocate no backing.
Field-place mutations also replace their owner from an argument and
check the current record's descriptor and sibling fields.

`DeepArrayFields.chibi` checks canonical array mutation through inline record
prefixes, including fetched map values, by-value and scoped records, saved
compound values, and callbacks replacing the root or an element's parent.
Bracket writes check their initial logical place before RHS effects and check
its current location again after callbacks; their unavailable-place failure
keeps category zero. `DeepArrayGrowth.chibi` allows at most 16 backing
allocations for 4,096 appends, both with unique storage and after one CoW detach.
`DeepArrayWidth.chibi` instruments array copy/drop helper entries while 64
other live array fields remain unchanged. It permits at most 64 entries across
4,096 leaf appends, independently of backing allocation counts.
Direct retained deep array places remain closed until retained leases carry the
full field path.

`MapCopies.chibi` requires 4,096 readonly source map snapshots to allocate no
backing, then mutates one array payload through append, extend, assignment,
removal and resize while its original map remains unchanged. Each mutation
starts from a newly shared copy. Source map copies
share counted entries. Explicit native `MapCopy` retains its physical-clone
contract; mapped mutation detaches and publishes the canonical map before
borrowing its current array entry.

# Native nested arrays

`run.sh NATIVE_COMPILER LLVM_BIN [ORDINARY_REFERENCE]` runs the unchanged
`generic_array_calls_after_descriptor_growth` acceptance test, unchanged
`imports/native_values`, `collections/scoped_lent_locals` and `collections/ranges`
tests and real source
programs at O0/O2. Copies, element replacement, returned and temporary inner
arrays, four array levels, and string payloads run with normal allocation
and the existing poisoned-free/zero-live-allocation audit. Nested iteration
covers keyed bindings, fresh receivers, break/continue/return, copied bindings,
three array levels, and string elements while replacing the source array.
Forward and reverse traversals preserve original indices; reverse cases also
cover empty/one-element arrays and aliased scoped receivers.

Each ordinary interned collection lowers to one synthetic record whose field
is its immediate array descriptor. Outer arrays use the existing record type
ordinal, counted backing and recursive lifecycle. Scalar/LLVM rows and the
24-byte array ABI stay unchanged. The producer suite's `Collections.chibi`
checks that unused optionals do not become collection records and that a
noncanonical `make_array` catalog cannot change its projected meaning.

Each nested iteration binds the physical wrapper privately and acquires its
immediate array descriptor into the source binding for that turn. Existing
loop scope cleanup releases it on normal and early exits.

Nested writable projections, retained nested arrays, and
named constructor arguments remain explicitly rejected pending their own
lowering. Managed pointer arrays remain outside these synthetic records until
aggregate tracing is implemented. The ordinary compiler also currently rejects
nested `.append` with an array-item type diagnostic; this suite does not count
that behavior as implemented.

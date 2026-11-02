# Native nested arrays

`run.sh NATIVE_COMPILER LLVM_BIN [ORDINARY_REFERENCE]` runs the unchanged
`generic_array_calls_after_descriptor_growth` acceptance test, unchanged
`imports/native_values` tests and real source
programs at O0/O2. Copies, element replacement, returned and temporary inner
arrays, four array levels, and string payloads run with normal allocation
and the existing poisoned-free/zero-live-allocation audit.

Each ordinary interned collection lowers to one synthetic record whose field
is its immediate array descriptor. Outer arrays use the existing record type
ordinal, counted backing and recursive lifecycle. Scalar/LLVM rows and the
24-byte array ABI stay unchanged. The producer suite's `Collections.chibi`
checks that unused optionals do not become collection records and that a
noncanonical `make_array` catalog cannot change its projected meaning.

Nested iteration, nested writable projections, retained nested arrays, and
named constructor arguments remain explicitly rejected pending their own
lowering. Managed pointer arrays remain outside these synthetic records until
aggregate tracing is implemented. The ordinary compiler also currently rejects
nested `.append` with an array-item type diagnostic; this suite does not count
that behavior as implemented.

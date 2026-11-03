# Native nested arrays

`run.sh NATIVE_COMPILER LLVM_BIN [ORDINARY_REFERENCE]` runs the unchanged
`generic_array_calls_after_descriptor_growth` acceptance test, unchanged
`imports/native_values`, `collections/scoped_lent_locals`, `collections/ranges`
and `collections/value_sequence_copies` tests and real source
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

Two-level writes support numeric, bool and string inner arrays rooted at a
variable or scoped parameter. Brackets capture both indices and check the
initial place before the RHS; methods evaluate their arguments before looking
up the current receiver. The native address operation makes the outer backing
unique, then lends its inner descriptor only for the immediate mutation and
writeback. It acquires no snapshot owner that would force each append to copy.
`Growth.chibi` checks allocation growth from 64 to 1024 appends, using the audit
counter. `checked/Main.chibi` requires the exact ten ordinary/native outcomes,
including wrong-category rejection and early/late unavailable places.

Retained nested arrays, deeper writable projections, nominal/aggregate/pointer
inner elements, and named constructor arguments remain outside this slice.
Managed pointer arrays remain outside these synthetic records until aggregate
tracing is implemented.

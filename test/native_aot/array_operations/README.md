Run `sh run.sh NATIVE_COMPILER LLVM_BIN` to compile the actual LLVM emitter
and execute its ArrayInsert and ArrayReverse instructions at O0 and O2.

Insertion clamps an index to 0..length and moves initialized elements backward.
Reversal swaps whole elements. Both consume an owning descriptor and reuse the
existing copy-on-write uniqueness and element lifecycle. The probe preserves
an independent shared copy while inserting before/after the original elements,
then reverses the result. A second execution uses the existing allocator audit
to poison released storage and require zero live allocations. Native unit tests
reject mismatched operands and reserved instruction metadata.

Resize preserves the ordinary source policy. Authored struct arrays can shrink
or retain their length, but growth fails before allocation. Union arrays grow
with the zero variant, including zero payloads. The synthetic records for
nested arrays also admit zero rows. The source fixture
checks owned shrink/copy behavior at O0/O2 with the poisoned allocator; separate
method, functional, field and map-entry growth cases require failure 134.
A real shared Host call through a helper also requires 134 after the context
failure propagates through its caller. The owned-task generation suite checks
functional and mutable failures after suspension and swap, with zero live
allocations before the old image unloads.
Raw native ArrayResize keeps zero-growth mode 0; mode ArrayResizeInitialized
requires record elements and rejects growth.

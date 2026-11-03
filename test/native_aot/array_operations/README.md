Run `sh run.sh NATIVE_COMPILER LLVM_BIN` to compile the actual LLVM emitter
and execute its ArrayInsert and ArrayReverse instructions at O0 and O2.

Insertion clamps an index to 0..length and moves initialized elements backward.
Reversal swaps whole elements. Both consume an owning descriptor and reuse the
existing copy-on-write uniqueness and element lifecycle. The probe preserves
an independent shared copy while inserting before/after the original elements,
then reverses the result. A second execution uses the existing allocator audit
to poison released storage and require zero live allocations. Native unit tests
reject mismatched operands and reserved instruction metadata.

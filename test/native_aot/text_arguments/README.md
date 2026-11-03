The native text operations preserve source evaluation order before selecting
named parameters. Run `sh run.sh NATIVE_COMPILER LLVM_BIN [ORDINARY_COMPILER]`.

The test executes all twelve unchanged native-argument samples, including
seven accepted calls and five exact diagnostics, at O0 and O2. Additional real
programs exercise all six slice permutations, binary/unary names, loose labels,
embedded NUL bytes, clamped reads, authored shadowing and dynamic string
ownership across a later scoped replacement. The audit allocator poisons frees
and requires zero live allocations after Main. The optional ordinary compiler
runs the same additional source programs.

`run.sh NATIVE_COMPILER LLVM22 [ORDINARY_COMPILER]` executes scalar square
root, precise sine/cosine, exponential, rounding, power and remainder at O0/O2.
It covers f32/f64 overloads and defaults, named argument evaluation order,
integer-width ranking, signed zero, infinities, NaNs, subnormals and authored
shadowing. An optional retained ordinary compiler runs the same square-root,
kernel and remainder programs against the owning C runtime catalog.

The owning definitions in `src/runtime/math_definition.h` explicitly map
`sin_precise` and `cos_precise` to platform libm at the operand's precision.
`src/runtime/math.c` calls the f32 functions for number and the f64 functions for
f64. The separate deterministic `sin`/`cos` builtins are unchanged. The same
runtime definitions own the exp/round/pow/sqrt overloads and numeric defaults.
The f32 square root remains an LLVM intrinsic.

Floating remainder uses LLVM 22 `frem` without fast-math flags. LLVM defines it
as the result of `fmod`, and may lower it to that system function:
https://releases.llvm.org/22.1.0/docs/LangRef.html#frem-instruction

The driver names the math symbols in libm on Linux, libSystem on macOS, and
UCRT on Windows. The maintained UCRT export list includes the required f32/f64
functions on Win64:
https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-crt/lib-common/ucrtbase-common.def.in
Linux static programs reject this system-math dependency before writing backend
artifacts. A freestanding static math kernel remains a separate compatibility
gate. Windows linking is checked separately from actual Windows execution.

The facts boundary currently expresses reversed foreign arguments only for a
direct two-argument call. Operand evaluation stays in source order; lowering
reorders the two completed value slots. General named-call permutations remain
a separate lowering gate. Direct libm calls inside waiting functions also remain
blocked by the native emitter's existing foreign-call restriction; nonwaiting
math helpers can be called from a waiting function.

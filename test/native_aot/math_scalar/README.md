`run.sh NATIVE_COMPILER LLVM22` executes scalar square
root, precise sine/cosine, exponential, rounding, power and remainder at O0/O2.
It covers f32/f64 overloads and defaults, named argument evaluation order,
integer-width ranking, signed zero, infinities, NaNs, subnormals and authored
shadowing. The source fixtures execute at both optimization levels.

`tools/compiler/package_compiler/ScalarMath.chibi` owns precision/default
selection. Precise operations use the operand-width platform math function;
the separate deterministic `sin`/`cos` builtins retain their frozen f32 algorithm.
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
a separate lowering gate. Synchronous foreign calls with scalar-by-value
arguments and results can run before and after suspension. This includes math
calls and direct/indirect scalar C calls; it does not admit borrowed strings or
array buffers into coroutine frames. The existing typed effects and frame
ownership checks still apply. An immediate-ready authored test exercises the
same path without beginning a task command.

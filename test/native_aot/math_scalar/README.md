`run.sh NATIVE_COMPILER LLVM22 [ORDINARY_COMPILER]` executes the scalar square
root and remainder cases at O0/O2. Remainder covers f32/f64 overloads, unit and
zero defaults, named argument evaluation order, integer-width ranking, signed
zero, infinities, NaNs, subnormals and authored shadowing. An optional ordinary
compiler runs the same remainder program as a differential reference.

Floating remainder uses LLVM 22 `frem` without fast-math flags. LLVM defines it
as the result of `fmod`, and may lower it to that system function:
https://releases.llvm.org/22.1.0/docs/LangRef.html#frem-instruction

The driver names the required math symbols in libm on Linux, libSystem on macOS,
and UCRT on Windows. Linux static programs reject this system-math dependency
before writing backend artifacts. A freestanding static math kernel remains a
separate compatibility gate. Windows linking is checked separately from actual
Windows execution.

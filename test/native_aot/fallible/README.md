Run `sh test/native_aot/fallible/run.sh NATIVE_COMPILER LLVM22` to check every
original fallible sample with an expected diagnostic, plus seven unchanged
positive handling controls. The CLI checks the owning compiler's exact message
and source position. The script keeps the full fallible selector separate while
its remaining positive source/runtime parity gates are addressed.

The same runner checks seven original optional-bool samples at O0/O2, covering
present false versus absence through nested guards, member reads, negation,
short-circuit operands and nil comparisons. A separate real-source fixture
checks parenthesized members in logical operands, arguments and negation.
An optional third argument names the retained ordinary compiler for the same
runtime controls.

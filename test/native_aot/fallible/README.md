Run `sh test/native_aot/fallible/run.sh NATIVE_COMPILER LLVM22` to check every
original fallible sample with an expected diagnostic, plus seven unchanged
positive handling controls. The CLI checks the owning compiler's exact message
and source position. The script keeps the full fallible selector separate while
its remaining positive source/runtime parity gates are addressed.

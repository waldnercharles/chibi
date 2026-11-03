Run `sh test/native_aot/fallible/run.sh NATIVE_COMPILER LLVM22` to check every
original fallible sample with an expected diagnostic, plus seven unchanged
positive handling controls. The CLI checks the owning compiler's exact message
and source position. The script keeps the full fallible selector separate while
its remaining positive source/runtime parity gates are addressed.

The same runner checks seven original optional-bool samples at O0/O2, covering
present false versus absence through nested guards, member reads, negation,
short-circuit operands and nil comparisons. A separate real-source fixture
checks parenthesized members in logical operands, arguments and negation.

Three original record-alias cases run at both optimization levels. A result-bank
copy fixture checks independent i8/u64/bool/f64 values, mutation and shadowed
locals. The accepted cases run natively at both optimization levels.

Primitive optional reads also cover four original imports samples and the
original typed-local descriptor-growth case. Value contexts read a guarded
payload; optional arguments/assignments and nil tests keep the descriptor.
The fixture includes f64 arithmetic, i16 optional forwarding, inferred false
bool locals and present numeric zero. Each category keeps its own import root.

Lazy coalescing and plain optional record fields run the six original imports
samples and a source fixture with calls that count evaluations, false/zero
payloads, nested fallbacks and absent records. Eighteen malformed facts cases
reject before LLVM publication. The managed-values runner checks the same
source with poisoned frees and balanced allocations at O0/O2.

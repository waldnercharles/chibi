# Native built-in math

`run.sh NATIVE_COMPILER LLVM_DIR` executes the original compiler math samples
and deterministic trigonometry at O0 and O2. `RunWindows.ps1` runs the same
sources with the fresh Windows compiler. Compile-error samples retain their
exact diagnostic goldens; no sample is copied, rewritten or skipped.

`Nested.chibi` checks record-field writebacks after RHS replacement, scoped
receivers, once-only indices, snapshots and managed siblings. Hidden parent
snapshots end with the assignment's lexical scope.
`Scopes.chibi` runs under the existing POSIX allocator audit. Subsequent array
mutations must allocate nothing, proving the parent leases have ended.
The view and recursive-record fixtures preserve their rejection diagnostics
and any prior output.

`Deterministic.chibi` pins the existing field sine/cosine approximation's
exact f32 results, including large angles, negative zero and nonfinite input.
The finite goldens come from `src/support/cos_sin.h` with contraction disabled.
Range reduction uses the platform's C ABI `remainderf`; no first-party C helper
is linked into the generated program.

`failures` distinguishes expression bounds failures from unavailable logical
destinations before and after RHS effects. Its fifteen expected failures reject
all seven typed categories for the ordinary unclassified reference failure.
Pass an ordinary compiler as the third `run.sh` argument to compare that same
fixture's outcomes directly.

`select.sh` and `Select.ps1` separately execute the typed native selection
primitive at O0 and O2, checking both outcomes, bool, NaN and negative zero.

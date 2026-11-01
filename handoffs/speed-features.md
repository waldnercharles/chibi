# Speed features (owner priority, 2026-10-29)

The owner said: "If there are language features that would help to improve speed, we should consider
prioritizing them as well." Ranked by strings-ints from profiling the Chibi parser on the bytecode VM.
For each pass over `Steps.chibi` (330 KB): tokenize about 0.9 s, tree build about 1.9 s, row emission
about 1.1 s, about 85 KB/s in total. The per-feature gains are estimates from the shape of the code;
nothing was measured feature by feature.

| # | Feature | Estimated gain | Owner |
|---|---|---|---|
| 1 | Native backend for the compiler host (W7 arrays, W9 fields, W13 imports, W14 catalog; then W21/W22) | 5-10x on the whole compile | sdk-cmake (W7, W14), builtin-math (W9), core-math (W13, W12) |
| 2 | Inlining small functions (Take/Add/Peek/Accept/IsName-style accessors, about 10 calls per token) | 20-30% of tree build | generics |
| 3 | In-place element mutation through a pointer, without copy-on-write or struct copies | measured: no copy cost visible, VM dispatch dominates; not built | — |
| 4 | Use records with array fields (#827, merged) for struct-of-arrays parser state | 15-25% of build | strings-ints (parser) |
| 5 | Byte views/slices without copies | measured about 1% of tokenize; not built | — |
| 6 | Dense `switch` on int/enum compiled to a jump table | 5-10% of build | generics |
| 7 | Capacity hints / `reserve(n)` for arrays | measured about 0%; not built | — |
| 8 | Enums with free name tables and constant int-indexed tables | 5-8% of build and emit | records |
| 9 | Record literal constructed straight into array storage | measured: same cost as appending a call result; not built | — |

Measured so far: #3, #5, #7 and #9 give nothing on the VM; dispatch dominates. The levers are #1 (native)
and #2 (inlining), plus a persistent compiled-compiler cache (records), since each
build-host start spends about 173 s recompiling the compiler. Each owner adds the feature
(language plus compiler, with the seed rule from `docs/self-hosting-build.md`), merges it as soon as
it builds, and tells strings-ints so the parser can use it.

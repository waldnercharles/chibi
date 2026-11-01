# Language features for the C exit (coordinator, 2026-10-30)

The owner asked whether we're adding language features, such as Jai, Odin or Zig-style generics, to
help replace C. Before this, feature work followed the speed list and `@error`, not the C exit.

## What Chibi has

- `$T` generic functions, specialized per concrete binding (monomorphized, as in Zig), across
  packages; `$N: int` compile-time parameters; callable parameters specialized to direct calls.
- Compile-time evaluation of scalar and type values, calls, locals, `if` and `while`
  (`docs/code-language.md`, "Generics and compile-time values").

## What's missing, and what C it replaces

| # | Feature | Prior art | Replaces | Owner |
|---|---|---|---|---|
| 1 | Generic types: parametric records and unions, `Pool :: struct($T: typeid, $N: int)` | Odin parametric structs | the C containers, handles and pools in `src/support`, `src/runtime`; the compiler's parallel arrays | generics, right after the handle-type natives that block #884 |
| 2 | Full compile-time execution: arrays, `for`, records, native calls, no step budget beyond a deadline | Zig `comptime`, Jai `#run` | the generated C tables and CMake/Make code generation (`sdk/*.cmake` generators, `*_packaged.c`) | generics, after 1 |
| 3 | `foreign import` and `foreign` blocks declaring C symbols in Chibi | Odin `foreign import`, Zig `extern` | the C-side native registration (`native_bindings.c`, `language_catalog.c`); it is W14's foreign ABI (D5) | sdk-cmake, as the design of W14 |

Order the generics queue as handle types, then 1, then 2, then inlining and the jump-table switch.
Use Odin's syntax and semantics for 1, because Chibi already uses Jai/Odin `$T`. Zig-style
type-returning functions can come after 2. Each feature lands like the others: language plus
compiler, the seed rule from `docs/self-hosting-build.md`, then merge.

The native census's 569 remaining functions don't wait on these. They wait on W7, W9, W13, W12 and W14.

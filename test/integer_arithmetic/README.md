# Integer arithmetic regressions

`Arithmetic.chibi` preserves two regressions from `test/language/packed.c`.
`WideConstantFoldingIsExact` keeps the original typed `Inner` and `Fold` functions,
u64 operands `9007199254740993` and `9007199254740991` and the independent fixed
golden `54043195528445952`.

The remaining 72 tests preserve `typed_integer_edges_match_vm_and_compiled_backends`.
Each calls an ordinary typed helper with the original runtime operands and
compares its result with a fixed literal. Signed literals preserve the original
C inputs' bit patterns. No conversion casts or computed expected values are used.

| Type | Rows | Operations |
| --- | ---: | --- |
| i8, i16, int, i64 | 10 each | wrapping add/subtract/multiply, ordering, divide, remainder, both zero-divisor cases, both minimum/-1 cases |
| u8, u16, u32, u64 | 8 each | wrapping add/subtract/multiply, ordering, divide, remainder, both zero-divisor cases |

`language-integer-arithmetic` runs the authored assertions through the native
compiler on both target backends, then through both stage-one compiler
backends on both target backends. Each test keeps the original 10000-instruction
bound. Native activation, ABI and ownership checks remain in C.
`execution_linked_function_returns_checked_value` and
`execution_integer_bitwise_preserves_width_and_valid_shifts` in
`test/language/execution.c` retain activation/invocation and raw argument-bank
coverage.

`wasm-integer-arithmetic` preserves the original cases' Wasm group coverage,
using BYTECODE and WASM as the target and stage-one compiler backends.
The Chrome Wasm acceptance driver runs the same selector.

`LiteralOnlyInitializers*` check that a local initialized by literal-only arithmetic
(`x := 1 + 2`) takes int, number or a wider integer from its literals' spelling, as
a lone literal does.

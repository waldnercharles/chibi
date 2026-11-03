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

`chibi-test-integer-arithmetic` runs the authored assertions through the native
compiler. The [native backend suite](../native_aot/backend/README.md) separately
keeps the total i32 arithmetic and bitwise emitter contract with 668 calls per
O0/O2 module. The retired `whole-integer` C selector used hand-built BF
instructions; its exact integer math did not admit exact integer source
overloads. Source math converts integer operands to `number`.

`LiteralOnlyInitializers*` check that a local initialized by literal-only arithmetic
(`x := 1 + 2`) takes int, number or a wider integer from its literals' spelling, as
a lone literal does.

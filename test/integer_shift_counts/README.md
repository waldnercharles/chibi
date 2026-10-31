# Shift-count regressions

`chibi build test language-integer-shift-counts` runs these authored packages
through `chibi test`, including four expected, located compile errors. It retains
the nine `shift_samples` from `archive/test/language/integer_semantics.c` without
restoring the stage-zero compiler or comparing one compiler with another.

| Package | Independent oracle |
| --- | --- |
| `exact_counts` | `(i8(1) << 200) >> 199` is 2, not a prematurely normalized count |
| `contextual_counts` | `(1 << 200) >> 199`, in i8 context, is 2 |
| `dynamic_left` | a count expression crossing i64's maximum reduces to 1; input 1 gives 2 |
| `negative_below_int64` | exact negative count reports `Shift count must not be negative` at 2:34 |
| `dynamic_negative_below_int64` | dynamic-left count reports `Constant value is outside the range of i64` at 2:35 |
| `left_limit`, `right_limit` | count 4097 reports `Constant shift count is too large` at 2:33 |
| `limit_inclusive` | count 4096 is accepted; the sum is 0 |
| `runtime_counts` | input 1 shifted by literal 200, u64 parameter 200, or literal 5000 is 0 |

The five `@test` functions contain seven fixed result assertions. Expected-error
files retain their original program line and column. These are the host's normal
authored-test route, not a claim of all-backend or native-code coverage.

`language-integer-shift-dispatch` runs the real selector and requires every case
identity and `9 tests, 0 failed`. It then changes only the expected value 2 to 3
in a scratch copy, invokes the same table/driver route and requires one failed
test plus a failing selector status. It also checks that the retired native
`--integer-shift-counts` spelling is rejected instead of falling through to
syntax tests. Both selectors remain in the ordinary language group.

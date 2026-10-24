# Core math

This candidate is held from SDK shipping. The raw float arithmetic is tested,
but stage zero still silently narrows typed double call arguments. The owner has
now approved requiring explicit double-to-float conversion while keeping implicit
float-to-double widening. Its coordinated compiler prerequisite has not landed.
No compiler/runtime fix or package-only workaround is part of this checkpoint.

The candidate `core:math` package has one pure, authored function:

```chibi
package example
import math "core:math"

pub Position :: (value: float) -> float {
    return math.InverseLerp(2.0, 6.0, value);
}
```

`InverseLerp(start: float, end: float, value: float) -> float` evaluates exactly
`(value - start) / (end - start)`. It allocates no storage and needs no native
capabilities. All three operations use `float` precision. It neither widens the
intermediates nor clamps the result. On the range from 2 to 6, values 2, 3 and 6
produce 0, 0.25 and 1; values 0 and 8 produce -0.5 and 1.5. Reversed endpoints
work through the same expression.

The owner approved this type-preserving, unclamped direction on 2026-10-24.
This package preserves the language's floating arithmetic and compiler defaults.
It makes no change to the separate [integer contract](package-numeric-contract.md).

## Floating edge cases

The bytecode VM and configured SLJIT backend use the same raw arithmetic in the
tested cases. Equal endpoints have no special fallback. A zero numerator divided
by a zero denominator produces NaN; a nonzero numerator divided by zero produces
an infinity with the division's sign. NaN inputs propagate. Infinite operands
can produce either NaN, infinity or signed zero according to the expression.
No blanket finite assertions run.

Signed zero follows each subtraction and division. For example,
`InverseLerp(0.0, -1.0, 0.0)` produces negative zero. A finite range whose
subtraction overflows can lose the expected geometric fraction. With endpoints
`-FLT_MAX` and `FLT_MAX`, zero produces positive zero, and `FLT_MAX` produces
NaN. There is no numerically stabilized alternative hidden inside this API.

Single-precision rounding is observable. With start -1, end 16777216 and value
16777215, the denominator rounds to 16777216 and the result is exactly 1. A
widened denominator would give a different result. One third returns the nearest
single-precision value, hexadecimal `0x1.555556p-2`.

## Scope and verification

The intended slice accepts `float` without silently narrowing typed `double`
arguments. The current concrete signature cannot enforce that requirement.
For example, passing a double value of 16777217 to an inverse interpolation
between 16777216 and 16777218 returns 0 after argument narrowing; preserving the
double value would return 0.5. The C test records this blocker explicitly rather
than treating it as approved package policy.

An imported generic declaration with `(start: $T, end: T, value: T) -> T` is
rejected as `Unknown callable 'math.InverseLerp'` in the exact requested base.
Stage one rejects the same double call with
`argument differs from its parameter type` at line 3, column 49 of the focused
consumer, while stage zero accepts it. Both stage-one compiler modes reproduce
this mismatch. A public double form is deferred rather than adding a compiler
feature or a second name. The approved global conversion rule is being implemented
as a separate parent-coordinated prerequisite before this candidate can ship.
Checked and clamped
forms need a concrete consumer and a separate small contract. `MoveTowards` is
excluded while its negative-delta and nonfinite policy remains unsettled.

The C/pico cases in `test/language/package_core.c` execute runtime inputs on
bytecode and the configured compiled backend. They supply exact nonfinite values
from C, check NaN classification and zero/infinity signs, and compare finite
results exactly with representable expected values. No broad epsilon masks
precision differences. They also compile a source-level imported call and inspect
its pure export. Run the selector from the repository root:

```sh
CHIBI_LANGUAGE_BUILD_DIR=<build>/test/language python3 tools/test_policy.py tool language-package-core
```

`CHIBI_CORE_MATH_SOURCE` can select the installed `Math.chibi` for this check.
The existing CMake core-source glob installs this candidate without shared SDK
file edits. A local SDK install, exact source comparison and an external installed
compiler import/call passed. This local verification does not clear the shipping
hold. Stage-one compilation does not establish native LLVM floating support;
the native LLVM lane currently supports int/bool only. Wasm, generated C,
Windows and native LLVM float execution are not claimed by this slice.


## Bounded checkpoint evidence

The isolated development branch starts at requested main
`343627984c88bd39ef6365d79f45e768266a3513`. No compiler, runtime, ABI, bootstrap,
floating compiler flags, or worker-owned SDK/dispatcher files were changed.

The final focused current-source Release checks passed on macOS:

| Check | Assertions | Test wall time |
| --- | ---: | ---: |
| `language-package-core`, bytecode and SLJIT | 15,798 | 0.881 s |
| Stage-one imports, bytecode compiler | 5,806 | 5.334 s |
| Stage-one imports, compiled compiler | 5,578 | 4.274 s |
| External installed compiler import/call | One source usage check | 0.095 s |

Initial native configuration took 4.8 s and its focused build took 44.1 s,
separate from test execution. Supplementary SDK/player build and local install
also passed; their combined wall time was not recorded. Every build used at
most two jobs. C formatting uses clang-format 22; source formatting and whitespace
checks passed.

The broad native, material, project, language-all and tooling groups were not
run. Independent review and any compiler policy repair remain with the parent.
Logs and ordinary build/install outputs are retained under `.chibi/math/` for
continuation. The disposable external consumer is removed after verification.


## Resumed current-main candidate

The original checkpoint remains preserved as `d039de3e`. Its four-file change
applied without conflicts to main
`7ffa3ef48b204658ba6d9f95f6b2716039fd8798` as source checkpoint
`553cfedb09fca24c357f860b0a7aa242253850d9` on the separate branch
`codex/core-math-main-20261024`.

The current-main candidate passed `language-package-core` with 15,798 assertions
in 0.856 s, stage-one imports with 5,806 assertions in 5.059 s, and compiled
stage-one imports with 5,578 assertions in 3.917 s. Incremental configuration and
build took 2.3 s and 2.1 s respectively, separately from test execution. These
checks retain the documented current-behavior blocker characterization; passing
them does not establish the newly approved rejection rule.

The global conversion acceptance fixtures and compatibility inventory live on
the independent `codex/float-conversion-oracles-20261024` branch. They deliberately
fail admission checks on unmodified current main. No shared compiler, SDK,
prepared-format, bootstrap, ABI or dispatcher edits were made in this math lane.
The package remains float-only and held. No PR is open.

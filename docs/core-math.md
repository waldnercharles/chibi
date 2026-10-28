# Core math

This is a development candidate held from SDK publication. The owner approved
implementation on 2026-10-27, including an analytic spring response and the
replacement of high-level behavior-native math with package imports. That
approval does not clear the earlier shipping hold. The dated validation receipt
records current-source execution, installed-SDK checks, independent review and
unexecuted coverage. SDK publication still requires an explicit release decision.

## Recovery

The original single-function candidate remains preserved at
[`d039de3ee`](https://github.com/waldnercharles/chibi/commit/d039de3eeb2e66e34fc3fc5f97339f9ba6ead06e).
Its rebased checkpoint and hold remain at
[`90e0936e9`](https://github.com/waldnercharles/chibi/blob/90e0936e90a06c6ed66f6a347c98923b04cd6c0b/docs/core-math.md).
Those candidates tested raw `float` inverse interpolation. Their passing checks
characterized a then-unwanted implicit narrowing and did not prove the intended
conversion boundary.

This work started from main `18dc01662344afbdf9265073d0ce890e3a4f6d11` and was
adapted to main `7f35662ac53a3d6635eaac7b95af3f0cafbdc586` after reload PR #775
and authored-debug-test PR #779 merged.
Today's `number` is `f32`; `f64` narrows only through an explicit conversion.
The candidate preserves the original inverse-interpolation expression and
rounding, with those current type names. Public precision-polymorphic exports
remain a dependency of the separate exported-generics work. No alternate legacy
names or generic compiler changes are introduced here.

## Use

```chibi
package camera

import math "core:math"
import ease "core:math/ease"

pub Fraction :: (position: number) -> number {
    return math.InverseLerp(2, 6, position);
}

pub Follow :: (spring: math.Spring, target: number, elapsed: number) -> math.Spring {
    return math.AdvanceSpring(spring, target, 2, 0.8, elapsed);
}

pub Fade :: (progress: number) -> number {
    return ease.SineInOut(progress);
}
```

`Fraction(3)` is `0.25`. A `Spring` is an ordinary value record with `position`
and `velocity` fields. The caller keeps its returned value between updates.
Changing the target leaves the current position and velocity available for the
next update. No pointer, allocator, clock, task or game service is required.

## Scalar functions

Every public scalar argument and result below is `number` unless stated otherwise.

| Function | Contract |
| --- | --- |
| `IsFinite(value) -> bool` | False for NaN and either infinity; true for signed zero and subnormals. |
| `Lerp(start, end, fraction)` | Unclamped `start + (end-start)*fraction`, with a separate rounding after each f32 operation. |
| `InverseLerp(start, end, value)` | Unclamped `(value-start)/(end-start)`, entirely at f32 precision. |
| `Remap(value, old_min, old_max, new_min, new_max)` | Composes inverse interpolation and interpolation. |
| `Step(value, edge)` | Zero below the edge, otherwise one, including an unordered NaN comparison. |
| `SmoothStep(value)` | Clamps finite progress to `[0,1]` and evaluates the cubic smoothstep curve. |
| `QuantizeAngle(angle, step)` | Rounds to a multiple, with half steps away from zero. A negative step is allowed; zero is invalid. |
| `AngleLerp(start, end, fraction)` | Follows the shortest angular difference in radians; fractions may extrapolate. The result remains near `start` rather than being normalized. |
| `Damp(current, target, decay_rate, elapsed)` | First-order exponential following with elapsed seconds. It stores no velocity. Nonpositive rate or elapsed time leaves `current` unchanged. |

The three raw range functions keep IEEE arithmetic. Equal inverse endpoints do
not have a fallback: zero divided by zero gives NaN, and a nonzero numerator over
zero gives a signed infinity. Subtraction can overflow even when all operands are
finite. NaNs and signed zeros follow the actual expression; no wider or clamped
answer is substituted.

The finite-domain helpers assert their input contract. Quantization and angular
interpolation also reject a nonfinite result. `Damp` uses f64 intermediate work,
an accurate small-step exponential difference and complementary reconstruction
near its target. This avoids both lost tiny steps and loss of a finite residual
when `1-exp(-x)` rounds to one. Public results narrow explicitly to `number`.

## Progress easing

`core:math/ease` exports `Linear` and the `In`, `Out`, and `InOut` forms of
`Sine`, `Quadratic`, `Cubic`, `Quartic`, `Quintic`, `Exponential`, `Circular`,
`Back`, `Elastic`, and `Bounce`: 31 stateless functions in total.

Each takes progress and returns progress. The intended domain is `[0,1]`, without
implicit clamping. Back and Elastic may overshoot. Endpoints are exact; the
oscillating InOut curves also guard their exact midpoint. Progress easing has no
retained velocity and is not a replacement for an interrupted physical spring.

## Analytic spring

`AdvanceSpring(spring, target, frequency, damping, elapsed) -> Spring` advances
both fields under a target held constant for the interval. Frequency is cycles
per second, damping is the dimensionless damping ratio, and elapsed is seconds.
The equation is `x'' + 2*zeta*omega*x' + omega^2*(x-target) = 0`.

The function preserves the old operation's finite-input requirement, minimum
frequency of `0.01`, and nonnegative damping/time clamps. Zero or negative elapsed
time returns the original record exactly. Equilibrium also returns it unchanged.
There is no settling snap and no near-critical substitution: underdamping,
critical damping and overdamping have their own analytic transitions. A
nonfinite public result fails before the caller receives a new state.

The solver uses f64 coefficient work. A rationalized overdamped slow root avoids
cancellation at large damping. Small-phase/exponential series preserve small
coefficients. Separate position-change coefficients and reconstruction from the
nearer endpoint preserve tiny motion toward a distant target and a small target
after a long decay. The public state rounds to f32 after each call.

Fixed-target subdivision is therefore a numerical comparison with stated
tolerances, not a bitwise identity. Retarget comparisons must use changes at the
same physical times. Different samples of a moving target are different inputs.
Platform libm behavior and extreme phase sensitivity also preclude a general
cross-platform bit-identity promise.

## Native boundary and retirement

High-level Step, Mix, Smooth Step, Quantize Angle and Advance Damped Spring
bodies, native declarations/bindings and behavior graph nodes are removed.
Callers import the package functions. The source/native spring response changes
from implicit integration to the analytic response described above.

Shader and field math remain separate. Existing deterministic engine `sin` and
`cos` keep their float approximation. The new narrow `sin_precise`, `cos_precise`
and `fmod` host functions forward to platform libm at the selected f32/f64 width.
`fmod` defaults its divisor to 1 at both widths.
The analytic spring and progress curves use the precise functions. Arithmetic,
curve policy and spring state transitions stay in Chibi.

SDK installation includes the math directory in every profile, including
profiles without the Unix build host. Compiler and value-host permissions stay
unchanged. Both compiler paths check all project packages and their transitive
collection imports, while omitting unused collection bodies. Snapshot syntax
and graph validation still covers every supplied file.
With no project files, the first input file selects the collection root. The
self-hosted compiler also retains its explicitly requested root.

The native package owner enumerates the prepared subset in original graph
order. Its indices are separate from the unchanged snapshot graph indices;
the documentation diagnostic path resolves packages by identity. Incremental
reuse and retained dependency generations remain keyed by identity.

Asset generation first compiles the current compiler using only its recorded
core dependencies and their sibling files. The checked older seed can perform
that step. The current compiler then handles the expanded collection, which
remains included in the packaged source and manifest. No checked seed or
prepared format is rewritten.

No project version bump is proposed. No authored engine project uses the retired
behavior functions/nodes, and the shader variants are preserved. Studio main
`9afa743996a72efb491ca169e0928eb3e56429fe` has one live consumer in
`projects/studio/content/behaviors/OutlinerRows.chibi`. The companion branch
`codex/core-math-consumer-20261027` replaces its integer-derived Step with a
typed 0/1 comparison and keeps the original `Count - 1 + offset` rounding order.
That edit works with the existing engine and leaves Studio's manifest version
alone, as the consumer-first migration recipe requires. It must land before
the engine retirement. A disposable copy passed the existing v28-to-v38
migrations and formatting checks.

The companion subsequently landed as
[Studio PR #104](https://github.com/waldnercharles/chibi-studio/pull/104), merge
`be7709fd737cb82bae10fdd553772daba0bc749e`, after independent review and passing
editor/version/build-startup CI. Its source and extraction digest are unchanged
from the reviewed candidate.

For a hand-authored project using an old source spelling, import `core:math`,
use the corresponding function and explicitly convert f64 values where needed.

## Validation

Ordinary behavior is checked in authored `@test` functions beside the package.
The C package-math selector supplies exact nonfinite values and signs, checks
rejection and conversion boundaries, reads the actual package source files, and
verifies catalog retirement. The separate benchmark compares imported scalar
calls with inline arithmetic and spring record-update forms, reporting
preparation, invocation, allocation and retained storage without a timing gate.

```sh
CHIBI_LANGUAGE_BUILD_DIR=<build> python3 tools/test_policy.py tool language-package-math
CHIBI_LANGUAGE_BUILD_DIR=<build> python3 tools/test_policy.py tool language-package-math-benchmark
```

`CHIBI_CORE_MATH_ROOT` selects the math directory of an installed SDK. Compiler
import checks additionally prepare the real package sources with both compiler
stages and execute the resulting math artifacts with the ordinary math catalog.
The compiler host's deliberately restricted execution catalog is preserved.

The [2026-10-27 receipt](evidence/core-math-20261027/RESULTS.md) records the
current builds, checks and measured costs. None of the 2026-10-24 passing counts
are claimed for this revision. Wasm embeds only the six shipped math sources in
the test filesystem; it uses the installed copies for an installed-SDK build.
Subsequent current-main checks and PR status are in the
[publication receipt](evidence/core-math-20261028-publication/RESULTS.md). The
math SDK publication hold remains in force.

## Primary references

- [Odin core:math, pinned source](https://github.com/odin-lang/Odin/blob/7e313bd6f1fa9ae887c7b522611481cbf2da4cac/core/math/math.odin#L389-L442)
  provides interpolation, remapping and angle conventions. Its degenerate-range
  policies are not substituted for Chibi's approved raw inverse interpolation.
- [Odin core:math/ease](https://github.com/odin-lang/Odin/blob/7e313bd6f1fa9ae887c7b522611481cbf2da4cac/core/math/ease/ease.odin)
  and its AHEasing origin provide progress-curve equations. Adapted source keeps
  the required notices and marks the changes.
- [Bevy StableInterpolate](https://github.com/bevyengine/bevy/blob/main/crates/bevy_math/src/common_traits.rs#L437-L471)
  demonstrates first-order exponential damping without velocity state.
- [Ryan Juckett's derivation](https://www.ryanjuckett.com/damped-springs/)
  derives the three analytic damping regimes. The Chibi transition and numerical
  stabilizations are written from the equations.
- [Rust springs solver, pinned source](https://github.com/sibaiper/springs/blob/cf3d07299d12079f40a08c53a7f476931627b054/src/solver.rs)
  is an implementation comparison. Its near-critical substitution and settling
  snap are not used here.
- [Rust's f32 API](https://doc.rust-lang.org/std/primitive.f32.html) supplies
  numerical operations; the animation APIs above are ecosystem libraries.

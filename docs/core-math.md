# Core math

`core:math` provides scalar interpolation, range mapping, angle helpers,
exponential damping and an analytic spring. `core:math/ease` provides stateless
progress curves.

This is a development candidate held from SDK publication. Validation of the
current forward port is incomplete. Earlier candidates' passing checks do not
establish release readiness for this revision. SDK publication requires an
explicit release decision.

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

`Fraction(3)` returns `0.25`. Keep the returned `Spring` between updates so its
position and velocity carry into the next interval.

## Types and precision

The public numeric arguments, results and spring fields are `number`, which is
`f32`. `IsFinite` returns `bool`. These exports do not select an `f64`
specialization when passed double-precision values. Convert each `f64` input
explicitly at the boundary:

```chibi
pub FractionFromDouble :: (position: f64) -> number {
    return math.InverseLerp(2, 6, number(position));
}
```

That conversion rounds the input to `f32`. Widening a result later with `f64(...)`
does not recover precision lost by the input conversion or the package call.
`AngleLerp`, `Damp` and `AdvanceSpring` use `f64` intermediate calculations, then
explicitly narrow their public results to `number`.

## Scalar functions

| Function | Contract |
| --- | --- |
| `IsFinite(value) -> bool` | False for NaN and either infinity. Signed zero and subnormals are finite. |
| `Lerp(start, end, fraction)` | Unclamped `start + (end-start)*fraction`. The subtraction, multiplication and addition each round to `f32`. |
| `InverseLerp(start, end, value)` | Unclamped `(value-start)/(end-start)`, with every operation at `f32` precision. |
| `Remap(value, old_min, old_max, new_min, new_max)` | Composes `InverseLerp` and `Lerp` without clamping. |
| `Step(value, edge)` | Returns zero when `value < edge`, otherwise one. The edge belongs to the upper side. An unordered NaN comparison also returns one. |
| `SmoothStep(value)` | Requires finite input, clamps it to `[0,1]`, then evaluates `value*value*(3-2*value)`. |
| `QuantizeAngle(angle, step)` | Requires finite inputs and a nonzero step. Rounds to a multiple with half steps away from zero. A negative step is allowed; a nonfinite result fails. |
| `AngleLerp(start, end, fraction)` | Requires finite inputs. Interpolates the shortest angular difference in radians, choosing the negative direction at a half turn. Fractions may extrapolate; the result is not normalized. A nonfinite result fails. |
| `Damp(current, target, decay_rate, elapsed)` | Requires finite inputs. Follows a fixed target with exponential decay, using a rate per second and elapsed seconds. Nonpositive rate or elapsed time returns `current` unchanged. |

`Lerp`, `InverseLerp` and `Remap` preserve their raw floating-point expressions.
Equal inverse endpoints have no fallback: zero divided by zero produces NaN;
a nonzero numerator over zero produces a signed infinity. Subtraction can
overflow even when the inputs are finite. NaNs and signed zeros follow the
expression; no wider or clamped answer is substituted.

The input requirements on `SmoothStep`, `QuantizeAngle`, `AngleLerp` and `Damp`
are assertions. They apply even when an endpoint or zero-time shortcut would
otherwise return immediately. `AngleLerp` with zero fraction preserves `start`,
including its zero sign, after checking its inputs.

### Exponential damping

For positive rate and elapsed time, `Damp` implements the relation

```text
next = target + (current - target) * exp(-decay_rate * elapsed)
```

It retains no velocity. A rate of `ln(2)` halves the remaining distance after
one second under a fixed target. Retargeting starts from the current value.

The implementation uses `f64` work, a small-step exponential difference and
reconstruction from the nearer endpoint. This preserves tiny steps and finite
residual distances that a direct `1-exp(-x)` blend can lose. The returned value
still rounds to `f32` after each call, so time subdivision is compared with
numerical tolerances rather than bitwise equality.

## Progress easing

Every easing function takes `progress: number` and returns `number`.
`Linear` returns its input. The ten families below each export `In`, `Out` and
`InOut`, for 31 functions in total.

| Family | Exports |
| --- | --- |
| Sine | `SineIn`, `SineOut`, `SineInOut` |
| Quadratic | `QuadraticIn`, `QuadraticOut`, `QuadraticInOut` |
| Cubic | `CubicIn`, `CubicOut`, `CubicInOut` |
| Quartic | `QuarticIn`, `QuarticOut`, `QuarticInOut` |
| Quintic | `QuinticIn`, `QuinticOut`, `QuinticInOut` |
| Exponential | `ExponentialIn`, `ExponentialOut`, `ExponentialInOut` |
| Circular | `CircularIn`, `CircularOut`, `CircularInOut` |
| Back | `BackIn`, `BackOut`, `BackInOut` |
| Elastic | `ElasticIn`, `ElasticOut`, `ElasticInOut` |
| Bounce | `BounceIn`, `BounceOut`, `BounceInOut` |

The intended progress domain is `[0,1]`. Inputs are not clamped. Outside that
domain each equation continues as written and may produce NaN, as with a
circular curve's square root. Back and Elastic can overshoot `[0,1]` even for
valid progress. Bounce has separate rebounds.

All curves preserve the exact endpoints zero and one. `BackInOut` and
`ElasticInOut` also explicitly preserve the exact midpoint. Other interior
values follow `f32` arithmetic and the selected mathematical kernels.

These functions map progress to progress. They have no clock or retained
position or velocity. An Elastic curve can oscillate, but restarting it after a
target change does not preserve spring momentum.

## Analytic spring

```chibi
pub Spring :: struct {
    position: number;
    velocity: number;
}

// Exported by core:math:
// AdvanceSpring(spring, target, frequency, damping, elapsed) -> Spring
```

`AdvanceSpring` advances both fields with the target held constant throughout
the interval. Frequency is cycles per second, damping is the dimensionless
damping ratio, and elapsed time is seconds. Position and velocity use the
caller's units, with velocity in units per second. The equation is

```text
x'' + 2*zeta*omega*x' + omega^2*(x-target) = 0
omega = 2*pi*frequency
```

Keep both returned fields when retargeting. The new target changes the next
transition without resetting the current position or velocity. A `Spring` is an
ordinary value record; the caller owns the state. No pointer, allocator, clock,
task or game service is required.

All six numeric inputs, including the two state fields, must be finite.
Frequency below `0.01` is clamped to `0.01`; negative damping is clamped to zero.
Zero or negative elapsed time returns the original record exactly. Equilibrium
also returns it unchanged. Input assertions run before these shortcuts.

Underdamping (`damping < 1`), critical damping (`damping == 1`) and overdamping
(`damping > 1`) use their respective analytic transitions after the damping
clamp. There is no near-critical substitution and no settling snap. A result
that cannot be represented as finite `number` position and velocity fails
before a new state is returned.

The solver computes coefficients in `f64`. Small-phase and small-decay series
retain small coefficients, and a rationalized overdamped slow root avoids
cancellation at large damping. Position-change coefficients preserve tiny
motion toward a distant target; reconstruction near the target preserves a
small target after a long decay. Each call rounds the public state to `f32`.

Fixed-target time subdivision is therefore a numerical comparison with stated
tolerances. Retarget comparisons must change the target at the same physical
times. Different samples of a moving target are different inputs. Platform
libm behavior and extreme phase sensitivity preclude a general promise of
bitwise equality across platforms.

## Migrating behavior math

Import `core:math` and replace the retired behavior-native spellings with
qualified package calls:

| Retired source spelling / behavior node | Package replacement |
| --- | --- |
| `step` / Step | `math.Step(value, edge)` |
| `mix` / Mix | `math.Lerp(start, end, fraction)` |
| `smooth_step` / Smooth Step | `math.SmoothStep(value)` |
| `quantize_angle` / Quantize Angle | `math.QuantizeAngle(angle, step)` |
| `advance_damped_spring` / Advance Damped Spring | Keep a `math.Spring` and replace it with `math.AdvanceSpring(spring, target, frequency, damping, elapsed)`. |

`math.Step` is uppercase and takes value before edge. Convert any `f64` inputs
explicitly to `number` where these scalar APIs are intended.

The spring response changes from implicit integration to the analytic response
above. Keep the existing position and velocity when moving state to `Spring`;
compare the new motion at the same update times and retarget times.

The retired behavior graph nodes have no replacement native nodes. Express the
behavior through imported package calls. Shader and field math remain separate
and retain their own nodes and contracts.

## Native mathematical kernels

Existing engine `sin` and `cos` retain their deterministic float approximation.
The thin native `sin_precise`, `cos_precise` and `fmod` functions forward to
platform libm at the selected `f32` or `f64` width. `fmod` defaults its divisor
to one at both widths. The analytic spring and trigonometric easing curves use
the precise functions; curve policy and spring transitions remain in Chibi.

Source notices in `packages/core/math/Math.chibi` and
`packages/core/math/ease/Ease.chibi` record the Odin and AHEasing origins and
alterations. The spring transition is derived from the analytic damping
regimes described by Ryan Juckett.

## Validation

Authored `@test` functions beside the package cover ordinary numeric behavior,
easing and spring transitions. The C package-math selector supplies exact
nonfinite values and signed zeros, checks rejection and conversion boundaries,
reads the package sources and checks retirement of the old catalog entries.
The separate benchmark compares imported scalar calls with inline arithmetic
and spring record-update forms; its measurements are not a timing gate. The
[current native checkpoint](evidence/core-math-20261028-integration/RUNTIME-308.md)
records zero wrapped allocations in the Lerp loops and nonzero allocations in
both spring loops. Those counters cover the benchmark bodies and their runtime
work.

```sh
CHIBI_LANGUAGE_BUILD_DIR=<build> chibi build test language-package-math
CHIBI_LANGUAGE_BUILD_DIR=<build> chibi build test language-package-math-benchmark
```

`CHIBI_CORE_MATH_ROOT` selects a math source directory, including the directory
from an installed SDK. That selection alone does not establish installed-SDK
compatibility. Release evidence must describe the exact current candidate and
its completed compiler, backend, package and installed-SDK checks. Validation
of this forward port remains incomplete, and the SDK publication hold remains
in force.

# Built-in math implementation stage

Approved scope, 27 October 2026: built-in f32 `vec2`, `vec3`, `vec4`, `quat`,
`mat2`, `mat3` and `mat4`, writable swizzles, recoverable math failures, and
staged CPU/native/shader delivery. This document records the first module
stage; it does not mark S2 complete.

## Implemented modules

- `tools/compiler/math_types` classifies canonical source math names and checks
  constructors, broadcasting, operators, common functions and swizzles. Its
  category numbers are package-local semantic categories, never catalog,
  bytecode or prepared-format ordinals.
- `tools/compiler/math_constants` evaluates flattened f32 compiler constants:
  swizzle reads/writes, column-major matrix products, transpose, determinant,
  inverse, Hamilton products, conjugation, rotation and scaled normalization.
  Arrays belong to the compiler's constant evaluator. They are not the runtime
  representation of a built-in math value.
- `tools/compiler/math_layout` describes compact values and explicit std140
  uniform/std430 storage packing. It supplies value size/alignment, array
  stride, array base alignment, column stride and component offsets.

The existing `--package-tests` dispatcher loads these sources and runs their
authored assertions on BYTECODE and SLJIT. Native-boundary assertions establish
the current Cute vector/quaternion sizes, alignments and quaternion field order
on the target compiling the test driver.

## Contract for the compiler adapter

The source compiler still owns alias canonicalization, source ranges, operand
evaluation, literals and numeric conversions. Apply its existing conversions
before calling math policy. `Scalar` means resolved f32. An f64 value cannot
reach that category without explicit `f32(value)` or `number(value)` narrowing.
This module does not replace the language's integer conversion rules.

Vector constructors accept exact component concatenation and scalar splats.
`mat3(1)` constructs an identity matrix; `mat3(0)` constructs zero. Matrix
components and columns are consumed column-first. `quat(x,y,z,w)` requires four
scalar components and does not establish a normalized invariant.

Vectors use memberwise arithmetic with scalar broadcasting in either order.
Matrix multiplication is algebraic and uses column vectors: `(A * B) * v`
applies B first. Matrix addition/subtraction requires equal dimensions;
scalars scale matrices. `matrix_comp_mult` requests memberwise multiplication.
Equality compares exact components and returns bool. Ordering compares scalar
components explicitly.

Swizzle reads use one of `xyzw` or `rgba` and may repeat components. Writes
require distinct destinations. The adapter evaluates the receiver location
once and the RHS once, snapshots the complete RHS, then performs stores.
`WriteSwizzle` tests the value transformation, including `v.xy = v.yx`; it
does not prove source lvalue evaluation or side-effect ordering.

Quaternion storage is xyzw, with w the real component. Hamilton composition
`q2 * q1` applies q1 first. The recoverable rotation helper normalizes a finite
nonzero quaternion before rotating a vec3; q and -q produce the same rotation.
Raw quaternion construction does not silently normalize.

`Binary` and `Function` return the successful payload category only. The
adapter must also inspect `BinaryRecoverable` and `Recoverable` before choosing
the full result type. In particular, quaternion-vector rotation needs a
recoverable path because a raw quaternion may be zero or nonfinite.
`normalize`, `normalize_or`, `inverse` and `quat_normalize` have recoverable
domain failures. An explicit normalization fallback must be finite;
`normalize_or_zero` supplies finite zero itself.

These internal tagged unions do not settle the general public library error
convention. That convention remains with the recoverable-results workstream.
Do not wire a successful payload kind directly into a plain result while
discarding its failure descriptor.

## Numerical and layout limits

Normalization scales by the largest absolute component before squaring, which
avoids overflow/underflow for large or small finite directions. Inversion uses
partial pivoting and detects exact-zero pivots at f32 precision. It does not
apply an arbitrary epsilon and does not promise useful accuracy for every
ill-conditioned matrix. Nonfinite inverse/normalization results are rejected.

An ordinary arithmetic constant-fold rejection means the compiler cannot fold
that result. It must not turn IEEE runtime overflow into a new source error.
Swizzles copy nonfinite components without arithmetic. Per-operation numerical
tests distinguish exact cases from tolerance-based comparisons. CPU/GPU bit
identity, NaN payload identity, signed-zero identity and denormal preservation
are not promised across backends.

Compact vec3 has 12 bytes; compact mat3 has 36. Shader vec3 arrays use stride16;
std140/std430 mat3 columns use stride16 and occupy 48 bytes. A std140 scalar
array has base alignment16 even though a scalar value has alignment4. Use
`ArrayAlignment` when placing arrays. These are explicit buffer contracts,
not permission to memcpy compact CPU aggregates into shader buffers. Native
target reflection remains authoritative for host ABI layouts.

## Integration sequence and ownership

1. Math owner: land reviewed module behavior and authored regression adapter.
2. Compiler integration owner, coordinated with Claude: attach these rules to
   stage-zero and stage-one construction, operators, calls and lvalues; retain
   source diagnostics and evaluate-once behavior. Recover useful algorithms
   from preserved local `be5ab05b9`; do not import its obsolete project-version
   migration or overwrite the work preserved on its branch.
3. Compiler integration owner: register actual built-in identities and layout
   facts, update bootstrap/SDK and build registration as needed, and prove the
   combined candidate. Prepared-version changes require an actual wire change.
4. Native owner: lower the same math to scalar/aggregate operations before
   optimizing SIMD. Run executable O0/O2 oracles per supported target. This
   stage does not edit the shared LLVM producer.
5. Shader owners: export math facts through S1 and lower them through S3;
   matrix shader IR also needs S7. Compare controlled CPU/GPU samples with
   operation-specific tolerances. Existing supplied-scalar-facts tests cannot
   establish authored vector/matrix shader support.

The ownership proposal was posted on PR694 at
<https://github.com/waldnercharles/chibi/pull/694#issuecomment-6460987750>.
It does not clear any collections, GC, publication or other explicit hold.

Claude confirmed the order in
<https://github.com/waldnercharles/chibi/pull/694#issuecomment-6461212824>:
independent modules may proceed; shared math compiler hooks and bootstrap
regeneration wait for PR694 to merge. Then base integration on that main,
land workstreams one at a time, regenerate once per integration, and check for
duplicate Place/Decl/storage/intrinsic constants. No files stay reserved by
Claude after that merge.

## Remaining acceptance

- Full source-level constructors and diagnostics in both compilers.
- Source lvalue side effects and RHS evaluation once for writable swizzles.
- Public recoverable result shape and corresponding call-site regressions.
- Remaining common-function execution, quaternion axis-angle/slerp/conversions.
- Prepared and bootstrap compatibility on the integrated compiler candidate.
- Native execution and shader CPU/GPU checks, with actual target/layout facts.

Module tests are evidence for the module stage only. Report source compiler,
VM, native, shader, merged and deployed states separately.

# Built-in math implementation stage

Approved scope, 27 October 2026: built-in f32 `vec2`, `vec3`, `vec4`, `quat`,
`mat2`, `mat3` and `mat4`, writable swizzles, recoverable math failures, and
staged CPU/native/shader delivery. This document records the module stage and
the provisional source compiler integration; it does not mark S2 complete.

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
`WriteSwizzle` tests the value transformation, including `v.xy = v.yx`.
The source corpus additionally checks location evaluation, argument snapshots,
pointer-returning receivers, nested projections and RHS side effects.

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

The results workstream selected ordinary values with a final typed optional
error, explicitly marked `@fallible`. Recoverable math will use ordinary source
helpers carrying that marker after the tested results prerequisite is available.
The current adapters reject these operations explicitly. Their internal tagged
unions do not replace the public convention or discard failure descriptors.

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

Source `mod(x,y)` uses `x - y * floor(x/y)`, including negative divisors.
Source `round` rounds halfway values away from zero. Shader lowering must retain
these rules rather than select a target operation with different tie behavior.
`length` and `distance` use ordinary f32 products and may overflow for large
finite inputs; scaled `normalize_or_zero` avoids that intermediate overflow.
Lowercase `step(edge,value)` is the source builtin. The separate scalar package
API `math.Step(value,edge)` retains its existing argument order.

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

On 28 October the owner requested an earlier local development base while
PR694's CI remained red. The provisional branch
`codex/builtin-math-on-694-20261028` pins
`19ad570dd12acb13de066c31a9c791b018c60aa7` and replays only the math changes
from `94d09d8cb650417197f6c5202e35497d76943826`. This changes local development
order. Published draft PR783 and its tested history remain on their original
branch; held code is not published through this provisional branch. Inherited
failures must be recorded separately, and the math-only changes still need
transplanting onto accepted main and validation of that final combination.
The clean replay is not new-base test evidence.

## Provisional source compiler stage, 28 October

Both adapters now implement built-in declarations, constructors, scalar
broadcasting, vector and matrix arithmetic, algebraic matrix products, Hamilton
products, equality, common nonrecoverable functions and writable swizzles.
Built-in values have compact, compiler-owned f32 fields. Compatible reflected
types are checked before reuse, including types returned by native calls without
an explicit source annotation. Matrix storage fields remain private.

The stage-one adapter uses private `MathDecl=15`; char14 and optional/collection
declarations keep their existing numbers. It captures each borrowed aggregate
before later operands can mutate it. Swizzle writes keep one logical receiver,
snapshot compound operands before the RHS, reread untouched components after the
RHS, and replace the complete value once. Stage zero also recognizes swizzle
mutations when deciding whether a local may borrow another value. Nested reads
retain their logical intermediate values for debugger watches; writes use the
composed physical component map.

No prepared-format, runtime, GC, LLVM or checked bootstrap file changed. The
production compiler inventory adds `Math.chibi`, `math_types/Types.chibi` and
`math_types/Functions.chibi`, bringing the pinned inventory from 27 to 30. Tests
and constant/layout evaluators stay outside that bootstrap source inventory.

Validation on the provisional branch:

- The module candidate built through `make build`, with debug configuration,
  warnings and ccache enabled and sanitizers disabled. Configuration took 8.7 s,
  recovery host 11.5 s, dependencies 7.5 s and the graph build 439.7 s.
- The source compiler libraries and language driver build through CMake with
  `CHIBI_LANGUAGE_TEST_ASSET_COMPILER_HOST=OFF`. This explicit focused setting
  excludes the packaged asset service from that driver; the normal default
  remains ON and source snapshot checks remain intact.
- All 44 authored module tests pass on BYTECODE and SLJIT. All 42 source cases
  pass in both compiler VMs: 34 accepted programs execute on both VMs and eight
  programs require source diagnostics. Stage-zero runtime oracles and exact
  debugger point/watch comparisons also pass. The math source case took
  71.02 s; its full types selector, including existing cases, took 100.90 s.
- Semantic 78/78, program 205/205, native calls 7/7, component writes 15/15,
  borrow 31/31, navigation 36/36 and source edits 8/8 pass. Existing compiled types,
  collections on both compiler VMs and argument sharing also pass.
- Six malformed native-return cases require an incompatible-layout diagnostic
  before field access or scalar-result math can read an invalid descriptor.

Inherited failures are recorded separately against plain pinned `19ad570`:

- Asset packaging failed 2/4 at the same inventory assertions in baseline and
  module candidate: the fixture expected 25 sources while 27 were listed. Claude
  repaired the upstream count in `a16788d1c`; this feature's exact count is 30.
  The expanded packaging corpus passes 4/4 with preservation/work checks intact.
- `--record-compatibility` fails its legacy native text callback case at
  lines 97/213 on both binaries; its other case passes.
- Native-only `--record-values` exits 139 on both binaries. Math does not edit
  either record fixture or the collection runtime.

Logs, binary hashes and the baseline comparison are retained under
`.chibi/math/` in this worktree. These are local development receipts. The
combined branch remains unpublished and must be reconciled with accepted main,
including the retirement and strings/test integrations, before a publishable
candidate is validated.

## Remaining acceptance

- Public recoverable result shape and corresponding call-site regressions.
- Source math constant folding and compile-time constant parity.
- User records containing math values, with the compositional-record prerequisite.
- Quaternion axis-angle/slerp/conversions.
- Prepared and bootstrap compatibility on the integrated compiler candidate.
- Native execution and shader CPU/GPU checks, with actual target/layout facts.

Module tests are evidence for the module stage only. Report source compiler,
VM, native, shader, merged and deployed states separately.

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

The existing `--package-tests` dispatcher loads these sources and their
regressions from `test/compiler_math`, and runs the authored assertions on
BYTECODE and SLJIT. Native-boundary assertions establish
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
failures were recorded separately. At that checkpoint the math-only changes
still needed transplanting onto accepted main and validation of that combination.
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

## Accepted-main source port, 28 October

`codex/builtin-math-main-20261028` starts at accepted merge
`7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62`. The preceding `19ad570` source
candidate and its receipts remain frozen. This port preserves main's native
character result validation, array ownership changes and authored test discovery.
The validated `7c6ca2f5` source patch is also frozen. The working branch now
includes accepted build-protocol repair `3e2b28dc` and canonical fixture merge
`afcd777b`. The boundaries preserve every production math source file. The
selected host/library build passes at the first boundary; strict canonical
fixture validation is recorded below.

The Graph registrations now include the production `math_types` directory in
asset inventory and supply `CHIBI_SOURCE_ROOT` to the module-test adapter.
Authored math tests live under `test/compiler_math`; their adapter retains the
same logical package paths. The production compiler therefore has exactly 30
source inputs, with no unrecorded test sibling in an imported compiler package.

Native-returned math values participate in common-function dispatch. Compatible
reflected rows retain their native requirements and headers after layout
validation. Managed pointers admit only the validated compact math layouts in
addition to the existing authored records. Swizzle lowering preserves logical
read/write effects, mutation facts and the implicit-dereference debugger watch.
The focused `--package-compiler-chibi-math-native` regression checks native
getters, swizzles, lengths, sine, pointer mutation and exactly three getter
calls. C and both source-compiler VMs must produce equivalent declarations,
effects, source points and watches, and execute on both target VMs.

This regression uses quarter-turn sine inputs. Built-in sine and cosine retain
the VM's existing deterministic float approximation; this port does not change
their precision contract or the execution runtime.

The earlier `7c6ca2f5` validation used an ignored source snapshot and the existing
standalone language CMake project, with SDK native/desktop and packaged asset
tests explicitly off. Its four-file fixture helper contains equivalent
initializer/concatenation corrections and a warning exception limited to the
test driver. Production sources and compiler targets retain their warnings.
The helper and every snapshot difference are recorded in
`.chibi/math-main/validation-source.json` and
`.chibi/math-main/snapshot-local-testinfra.patch`. This is experimental local
source validation. It does not replace normal default build or SDK acceptance.

At accepted fixture base `afcd777b`, the same standalone driver now builds with
Clang 22 and `-Wall -Wextra -Werror -UNDEBUG`, without helper patches or a
warning exception. The first strict attempt exposed 34 math-owned sample call
rows missing their `diagnostic` initializer. Designated initializers fix those
rows without changing any string, call order or oracle. The original failing
build is preserved. Configuration took 4.045 s, the initial build 28.964 s and
the successful rebuild 4.092 s.

All eight selected canonical groups pass: 325 tests and 62,970 assertions in
65.944 s of policy wall time, excluding configuration and build. They cover
semantic analysis, programs, native calls, record values/compatibility, asset
packaging, authored module tests and the source compiler types group. The
types group includes native math results, all 42 source math cases and typed
packages, with 19,958 assertions. All 30 ordered production source paths and
texts still match the tested experimental bootstrap. Receipts, strict compile
commands, the exact repair and original failure hashes remain under
`.chibi/math-main/canonical-build`; its snapshot/build are retained for the
queued asset replay.

The native math regression passes with 431 assertions, including actual target
backend entry counts and no bytecode entry on the compiled target. The final
native checks also pass: semantic 80, program 209, native character results 6,
asset packaging 4 at 30 sources, native calls 7, component writes 15, borrow 31
and navigation 36. All 44 authored math tests pass on BYTECODE and SLJIT. The standalone driver
omits the task-pool test and project-asset editing selector by construction.

All 16 earlier `7c6ca2f5` focused groups pass. The types group includes the 42-case math
corpus, native getter regression and typed package cases (19,943 assertions
before the added backend counters). Compiled typed packages, collections on
both VMs and the compiler argument-sharing guard also pass. The selected
commands took 215.848 s in total, excluding configuration and build time.

Self-bootstrap passes with 30,371 assertions in 691.037 s: stage two reproduces
the same 22,469,206-byte compiler, SHA-256
`f1a6bdffc9d8bec2de2b55f61bad48a2d8593c2a8ab1fc21cca42f46a6497c9c`.
The supported artifact writer and recovery check also pass against the full
30-source snapshot, taking 125.889 s and 98.786 s. The experimental manifest
has SHA-256 `25e11f25b9c4cd71a48bdc4a907df76df3ddf2191d75eb1651862d5b3214f73c`.
Its recovered compiler has the same bytes. The pair is retained only in
`.chibi/math-main/experimental-bootstrap` and the ignored validation copy;
the authoritative checked artifact is unchanged.

Experimental asset generation then compiles the bootstrap and facade on SLJIT
with zero bytecode/fallback entries, but packaging stops while loading
`project:asset_source`: `prepared authored layout work limit exceeded`.
The run took 283.591 s. A separate accepted-main snapshot at `3e2b28dc`, using
the checked 27-source compiler and current build host, completes asset generation
in 395.168 s and passes the source-inventory check. The owning codec, runtime
and compiler sources are unchanged between the two accepted bases. This is a
math integration regression, separate from the passing math and bootstrap
checks. Different build configurations make these times unsuitable for a speed
comparison.

The failure occurs when loading an importer fingerprints its retained compiler
dependency. The C prepared writer repeats full descriptor validation for each
function-owned alias header, and math's added scalar operations enlarge that
hot descriptor. A separate source-only prerequisite now reuses each validated
descriptor within one write while fully validating each new alias. Its bounded
import regression fails on the old writer and passes with the fix; seven focused
codec/runtime groups pass. Independent coordinator review is complete and the
fix is isolated in draft [PR802](https://github.com/waldnercharles/chibi/pull/802).
The full math asset replay remains pending. No format, ABI or work limit is raised.

The production source inventory now follows the supported bootstrap producer's
order, including its trailing math-policy and scalar-facts inputs. The earlier
inventory had the same sources in a different order, causing bootstrap reuse
to reject the experimental seed and fall back to compilation. The packaging
fixture now compares that inventory against the source-order helper shared
with the supported bootstrap producer. The old same-set order fails both
generator cases; the corrected order passes all four packaging cases with
24,922 assertions in 4.390 s. Actual reuse with the corrected order still needs
the asset replay. The checked compiler artifact remains unchanged. Normal
asset/SDK acceptance is still blocked.

The old record failures are no longer present on this candidate:
`--record-compatibility` passes its one current case and `--record-values`
passes all 15. Accepted-base fixture corrections and the math-owned initializer
repair now permit the strict standalone build above. Normal SDK acceptance is
still separate: PR802's Linux SDK relocation job stops at its existing
120-second `CompilerTools` operation bound, with attribution open. Its Linux
and macOS native jobs and all three compiler self-build platforms pass.
Checked bootstrap/runtime artifacts, SDK publication and merge eligibility
remain under the coordinator's ordered integration gates.

## Remaining acceptance

- Public recoverable result shape and corresponding call-site regressions.
- Source math constant folding and compile-time constant parity.
- User records containing math values, with the compositional-record prerequisite.
- Quaternion axis-angle/slerp/conversions.
- Prepared and bootstrap compatibility on the integrated compiler candidate.
- Native AOT execution and shader CPU/GPU checks, with actual target/layout facts.

Module tests are evidence for the module stage only. Report source compiler,
VM, native, shader, merged and deployed states separately.

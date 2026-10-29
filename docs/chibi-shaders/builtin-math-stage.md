# Built-in math implementation stage

Approved scope, 27 October 2026: built-in f32 `vec2`, `vec3`, `vec4`, `quat`,
`mat2`, `mat3` and `mat4`, writable swizzles, recoverable math failures, and
staged CPU/native/shader delivery. This document records the module stage and
the provisional source compiler integration; it does not mark S2 complete.

## Current integration base

The math worktree is now prepared on accepted #803 merge
`79743c5675e5a18eeea14193d729cc6e45bdf926`, tree
`93f816890023819ddd09b287287db46a2fc8b20a`. It retains private build protocol
14 and the accepted build-value lifetime repair. The 16 accepted paths do not
overlap the 44 math paths, so the forward port preserves the preceding math
source without conflict. The earlier `0abd96c6` checkpoint, its passing checks
and receipts remain separately frozen.

The coordinator selected the shared scoped-write correction from the reviewed
#801 owner checkpoint `6c0cee1c`. Only its separable production patch is
integrated, SHA-256
`e497dda37cd4cd57222040b82fbabb6df8f7439508ba7258164166bf4e93298c`.
The combined Steps has SHA-256
`a0a09a1594962cb119a402161814563a85c83b7cfe74b067e311730e8259aa75`.
Successful `writes=true` stores now schedule public WRITE alongside the
existing preparation fact. The false guard, failed steps and local-box stores
are unchanged. No other #801 source or its 22 native array metadata changes
are imported. This closes the identified source gap; combined runtime and
purity checks remain pending.

Accepted #817 package selection and compact closure rows remain intact,
including math's package requirement filtering and negative-package guard.
Its accepted closure assertions and import cases remain alongside math's
shared sample runner and oracle coordinates.

The earlier accepted `FinishBody` extraction at `51650ccf` is unchanged.
The combined Body has SHA-256
`7d8ea6d4dbeca38f1d6b099c88da6d6b1e1877525d4a99e434cdda6e9af021f3`.
Its finalizer tail and transfer of effects, facts, roots, calls and debug
tables are unchanged. The combined Compile has SHA-256
`a0a98b210626412331268afdf9d0ae0f2c6c86d62656f93c3b2822b935617cd2`.
Math source and tests outside Steps retain their prior bytes at this forward
port.

The accepted 27-source bootstrap pair is retained together: 20,792,064-byte
compiler SHA-256
`27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912`,
producer revision `d1315047c0a533a3848569a39af83a33c98a8799`. The E1 native
runtime pin is unchanged. Math still has 30 ordered source inputs; Compile,
Body and Steps differ from the historical f1a texts. **f1a reuse is not permitted** for
this candidate. Neither the accepted base bootstrap nor older private proofs
establish the combined math compiler's behavior.

The shared `WriteStep` correction is now in the staged source candidate. Its
earlier private GC proof belongs to the old Body and Compile. Fresh
scoped-write/purity controls, the full
math source and GC matrix, and prepared/schema/array roundtrips are required
at the coordinated combination. Prepared/native ABI, generated artifacts,
SDK acceptance and publication remain separate gates. Accepted #817 supersedes
reachability #790. The coordinator has not allocated a heavy producer to math;
this source preparation runs no compiler or asset producer. Main/CLI migration
remains with its existing owner.

The preceding affected native checks passed on frozen source tree `b4dbc77d`
at accepted `a253ef4b`:
`--compiler-fixture`, `--package-imports` and `--package-tests`, totaling 33
cases and 2,282 assertions. All 44 authored math module tests pass on each of
BYTECODE and SLJIT. Fresh strict Clang 22 Release configuration took 3.494 s,
build 23.557 s and serial policy checks 2.785 s. The existing asset-host guard
was selected off. This covers the C collection preparation, fixture root
import and module boundary; the nine authored source-compiler import samples
and combined math compiler matrix remain pending. No checks or builds ran
during the current #803/WRITE source preparation. No full compiler fixture,
artifact writer, self-build or asset producer ran. The receipt is
`.chibi/math-main/runtimea253-validation/build/validation-receipt.json`.

## Accepted runtime boundary and earlier checks

On 28 October 2026, the math worktree was first forwarded to accepted merge
`74a9ac60a44a0a8735149a89f1b5482bdd87d127`, tree
`4f20f3f06ef6e9e62143aeaaf81332b7b856f9cc`. PR802's reviewed native writer
and runtime pin are merged together. Final pin CI passed seven applicable
jobs with three expected skips; all three self-build platforms and the
installed native seed oracle passed. The inherited runtime manifest is
`e1adf32b577f9a20993e9db40d4c3777d2b3a8325d1bb8a19a57bf2256f23348`.
The coordinator's final receipt is
`.coordination/archives/2026-10-28/pr802-pin-normal-ci-40768924371/RECEIPT.md`.

That forward port applied without conflicts. It preserves the accepted
format/failure emitter migration to authored tests. Math's production patch
is byte-identical to its earlier frozen snapshot, and all 30 ordered compiler
paths and raw texts still match private f1a. The pre-port index, full patch
and recovery snapshot remain under `.chibi/math-main/runtime74a-prep` and
job-scoped Git refs.

The lightweight check at `74a9ac60` passes nine native groups: 235 cases
and 21,636 assertions, with all 44 authored math cases passing on each VM.
Fresh strict Clang 22 Release configuration took 3.656 s, build 28.074 s and
serial policy checks 2.700 s. The snapshot matches staged tree `6429cc98`;
686 tracked compiled dependencies and 487 object hashes are recorded. The
existing default-on asset-host dependency guard was selected off, with no
additional source helper. No full source-compiler fixture or producer ran.
The receipt is `.chibi/math-main/runtime74a-validation/build/validation-receipt.json`.

The accepted Body change is inherited as described above. Additional
unreviewed Body changes stay outside this candidate. At that earlier boundary,
the shared `WriteStep` correction remained only in its private GC proof; the
current source integration is recorded above. The
checked authored bootstrap remains the accepted 27-source artifact. f1a and
its recovery/self-build evidence remain historical and private. No compiler
writer, artifact regeneration, asset producer or publication ran during either
forward port. Earlier runtime test results below retain their original source
attribution.

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
error. Its current reviewed draft infers error checking from `@error` result
types; the earlier per-function `@fallible` marker is historical. Recoverable
math will follow that convention after the matching parser/runtime and results
prerequisite are admitted. The current adapters reject these operations
explicitly. Their internal tagged unions do not replace the public convention
or discard failure descriptors.

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
`afcd777b`, followed by accepted runtime merge `30837bd5`. The boundaries
preserve every production math compiler source file. The selected host/library
build passes at the first boundary; matching runtime and canonical fixture
validation are recorded below.

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

At runtime base `30837bd5`, native objects, arena/function headers and both
hosts were rebuilt together. Configuration took 4.200 s and the fresh strict
build 33.169 s. Before adding the pressure variant below, thirteen selected
groups pass 389 tests and 73,510 assertions in 123.975 s of policy wall time.
The additional checks cover prepared records,
borrows, component writes/layout and compiler argument sharing. All six native
target facts still match the experimental f1a seed, as do all 30 ordered source
paths and texts. No seed recovery, writer or asset producer ran in this check.
Evidence is frozen under `.chibi/math-main/runtime308-build`.

A separate native-math pressure variant retains the original three-getter
oracle and keeps a native vector location plus old swizzle components live
across a mutating outlined RHS call. It uses real allocation pressure and the
accepted runtime's managed storage binding. Both C-compiler target VMs pass
with 850 collection marks, mask 63 and three getter calls. The authored compiler
stops at strict declaration comparison: the helper has C WRITE effect 2 but
authored effect 0 on the frozen production math candidate.

Independent ordinary integer/record controls reproduce the missing WRITE
summary on accepted main and the effect-contract candidate. Scoped-reference
stores retain their optimization fact but omit public WRITE metadata. Hidden
local-box controls remain pure. The shared effect-contract owner has this
inherited compiler seam. A private validation copy overlays only the owner's
reviewed `WriteStep` summary correction, patch SHA-256
`e497dda37cd4cd57222040b82fbabb6df8f7439508ba7258164166bf4e93298c`.
It freshly embeds that changed source without reusing f1a. Its strict build
and native-math replay pass: 741 assertions, all twelve ordinary and pressure
executions, and declaration/effect/points/watches comparisons. The C pressure
paths report 850 marks each; all four authored pressure paths report 431 each.
Every pressure path returns mask 63 with three getter calls, with actual
backend-entry and no-fallback checks intact. Configuration took 5.450 s,
the fresh build 29.606 s and the policy replay 37.321 s. The authoritative
compiler inputs remain unchanged; the shared fix still needs normal
integration before publication. The private receipt and logs are under
`.chibi/math-main/runtime308-gc-effects-build`.

The original test setup
failure and its lifecycle repair are separately recorded under
`.chibi/math-main/runtime308-gc-build`, along with the unchanged pressure source,
metadata failure and frozen source identities. The frozen 30-source f1a inputs
for the writer-cache comparison remain separate from this new test.

The original native math regression passes with 431 assertions, including actual target
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

The earlier `7c6ca2f5` self-bootstrap passes with 30,371 assertions in 691.037 s: stage two reproduces
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
import regression fails on the old writer and passes with the initial cache.
The prerequisite is isolated in
[PR802](https://github.com/waldnercharles/chibi/pull/802).
No format, ABI or work limit is raised.

An isolated pair on accepted runtime `30837bd5` tests that initial cache against
frozen math tree `706782b1` and the unchanged f1a seed. Both attempts use the
corrected 30-source inventory, freshly rebuilt matching Release hosts and the
same target facts and limits. The baseline fails at the same `asset_source`
layout-work bound after 94.655 s. Varying only the reviewed writer source
completes packaging, embedding and generation in 98.996 s, and passes the
source-snapshot inventory check. Both attempts reuse the bootstrap with status
0 and compile the facade on SLJIT with zero bytecode/fallback entries. Their
completed facade bytes are identical. These elapsed times describe a failure
and a completed workload; they do not measure a speedup.

The pair proves admitted completion for this asset workload. Subsequent
independent review found that the value codec still performed full authored
validation before reaching the cache, while charging the cached linear cost.
The repaired candidate `43eec64f` on accepted base `dafe746` delegates authored
validation to its owning resolver and keeps
full charged freshness checks for nested schema-less headers. An isolated
whole-write diagnostic measures duplicate-name comparisons falling from
2,105,152 to 16,256 for 128 fields and 256 headers, with identical 6,853-byte
output and fingerprint. This counter proof is separate from the asset pair.
The final strict fresh build passes seven groups, 149 tests and 20,185
assertions, including native retirement, per-attempt validation and retained
schema/header/array cases. Configuration took 3.50 s, build 39.83 s and the
selected commands 2.346 s. Independent runtime and records compatibility
reviews found no blocking issue. The final candidate's separate counter
replay confirms the same 16,256 comparisons and exact output. Source, object
input identities and uninstrumented/diagnostic receipts are preserved in the
shared workspace archive:
`/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/prepared-record-write-cache/prepared-cache/`.
Its `schema-green-build/green-results.json`,
`schema-probe-new-build/record-prepared.log` and
`accounting-probe/final-schema-proof.json` retain their original bytes.
PR802's repaired source was pushed at that revision.
The coordinator subsequently published the hash-verified, commit-specific
native seed for `43eec64f`; bot pin `d6ee0ce2` changes only `runtime.json`.
The workflow refuses source-tag overwrites; GitHub does not mark the release
server-immutable. The four reviewed source files are unchanged.

A separate installed-seed proof now exercises the actual published macOS
ARM64 executables with the same tiny closed artifacts. The old `9e9c374a`
seed accepts the leaf-only control, then rejects its retained importer at
the layout-work bound. The new seed returns 37 on BYTECODE and SLJIT with
exactly one selected-backend entry and no fallback, WASM entry or overflow.
The fixture has 512 primitive fields and 256 shared payload-free headers;
its two artifacts are 13,017 and 13,310 bytes with an identical leaf row.
Both executable target queries match all six frozen facts, and input,
producer, executable, manifest and source hashes remain unchanged afterward.
The host's 512 MiB layout-work bound and invocation limits are unchanged.
The actual fixture and runner passed independent review; no math compiler,
f1a, bootstrap or asset producer runs in this proof. The archived receipt is
`/Users/cwaldner/Personal/chibi/.coordination/archives/2026-10-28/prepared-record-write-cache/prepared-cache/installed-seed-prep/installed-proof/receipt.json`.
The raw receipts keep their original paths; the archive's lossless packet
and path mapping preserve their identities and replay inputs. This proves
delivery of the native prerequisite. Source and pin
subsequently merged together as accepted `74a9ac60`.
The pair does not clear math's publication or SDK gates.
No Body/effect-contract source, seed regeneration or larger
quota enters either attempt. Receipts, commands, hashes and logs remain under
`.chibi/math-main/paired-cache-308`. The producer slot was released after the
two attempts and analysis.

The production source inventory now follows the supported bootstrap producer's
order, including its trailing math-policy and scalar-facts inputs. The earlier
inventory had the same sources in a different order, causing bootstrap reuse
to reject the experimental seed and fall back to compilation. The packaging
fixture now compares that inventory against the source-order helper shared
with the supported bootstrap producer. The old same-set order fails both
generator cases; the corrected order passes all four packaging cases with
24,922 assertions in 4.390 s. Both paired asset attempts now establish reuse
with the corrected order. The checked compiler artifact remains unchanged.
Normal SDK acceptance and math publication are still blocked.

The old record failures are no longer present on this candidate:
`--record-compatibility` passes its one current case and `--record-values`
passes all 15. Accepted-base fixture corrections and the math-owned initializer
repair now permit the strict standalone build above. Normal SDK acceptance is
still separate from the full math candidate. The older PR802 head's Linux
SDK relocation job stopped at its existing 120-second `CompilerTools` bound;
its attribution remains open. Final source head `43eec64f` passes normal CI
and all three compiler self-build platforms, including fresh candidate SDK
relocation (8 cases, 208 assertions). That SDK proof uses actual candidate
hosts copied unchanged into installation. The new pin's three-platform
self-build also passes; its final normal CI passed seven applicable jobs with
three expected skips. The source-linked wide-record group remains separately validated
because the normal CI selector list omits `record-prepared`.
Checked bootstrap/runtime artifacts, SDK publication and merge eligibility
remain under the coordinator's ordered integration gates.
The math worktree now inherits that accepted runtime pin at `74a9ac60`.
This source preparation does not promote historical math checks into proof
of the combined candidate or update its checked authored bootstrap. Preserve
source-order and regeneration identities at the next validation boundary.
Regenerating compiler data alone cannot update an older native executable.

## Remaining acceptance

- Public recoverable result shape and corresponding call-site regressions.
- Source math constant folding and compile-time constant parity.
- User records containing math values, with the compositional-record prerequisite.
- Quaternion axis-angle/slerp/conversions.
- Prepared and bootstrap compatibility on the integrated compiler candidate.
- Validate the selected shared scoped-reference WRITE fix on the combined
  source compiler, including the GC-pressure replay and purity controls.
- Native AOT execution and shader CPU/GPU checks, with actual target/layout facts.

Module tests are evidence for the module stage only. Report source compiler,
VM, native, shader, merged and deployed states separately.

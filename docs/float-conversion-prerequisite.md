# Floating conversion prerequisite

The owner approved this contract on 2026-10-24: typed float widens implicitly
to double; typed double requires explicit `float(value)` at declarations,
assignments, arguments and returns. Explicit narrowing rounds to nearest, ties
to even. Overflow becomes signed infinity, NaN remains NaN without a payload
promise, and signed zero follows the conversion. There is no added panic,
clamp or precision check. Ordinary untyped literals remain contextual.

The original test-only checkpoint `dbe4d4f22561e1ab818ebead5c758754c98c379f` starts at main
`7ffa3ef48b204658ba6d9f95f6b2716039fd8798`. It does not change production
compiler/runtime behavior, compiler flags, prepared format 20, protocol, ABI or
bootstrap artifacts, and does not adopt the unmerged M2 work in PR 687.

## Preserved math work

Original checkpoint `d039de3eeb2e66e34fc3fc5f97339f9ba6ead06e` remains on
`codex/core-math-inverse-lerp-20261024`, also preserved by
`codex/core-math-held-d039de3e`. Its four-file change applied cleanly to the new
main on `codex/core-math-main-20261024` as
`553cfedb09fca24c357f860b0a7aa242253850d9`. The math candidate remains held;
this conversion test branch does not include or modify it.

## Tests and independent answers

`test/language/float_conversion_cases.h` supplies shared admission fixtures with
literal expected results, rather than answers computed by either compiler or
its cast helpers. Stage zero analyzes the genuine package form. Both stage-one
compiler modes compile the same package text and execute admitted cases on
bytecode and the configured compiled backend.

The fixtures cover typed narrowing rejection, explicit narrowing admission,
implicit widening and contextual literals at each of the four scalar boundaries.
They also cover typed constants and constant returns, the 16777217 and 16777219
rounding ties, signed overflow, NaN and negative zero.

`test/language/packed.c` feeds exact C floating values into authored conversion
functions. Its 19 narrowing inputs include both halfway parities, adjacent
double values, the finite/overflow boundary, subnormal/zero boundaries, signed
zero, NaN and infinities. Its 12 widening inputs include extrema and subnormals.
Expected answers are exact literals, not host casts of the input. The runtime
checks execute on bytecode and SLJIT and inspect backend-entry counters.

The additional relation-matrix test compares literal admission booleans directly
with the C implementation and both VM executions of the authored relation
package. It does not derive the authored expected answer from the C result.

These are contract-first fixtures. Admission checks are expected to fail on the
unmodified main until the separately coordinated production prerequisite lands.
No expected answer is weakened to make existing production behavior pass.
After ownership coordination, two production mutation controls ran and were
restored. Their results are recorded in the implementation section below.

## Current-main findings before implementation

- Runtime narrowing and widening oracles pass, including exact ties, nonfinite
  values and signed zero. No runtime numerical change has been demonstrated.
- Stage zero incorrectly admits all six typed-narrowing fixtures relative to the
  newly approved policy, including a typed constant declaration and return.
- Stage zero accepts the positive explicit-cast, widening and contextual-literal
  fixtures.
- Stage one rejects `float(value)` as `Unknown function 'float'`.
- Stage one rejects implicit widening at declarations, assignments, arguments
  and returns with its existing type-mismatch diagnostics.
- Stage one rejects the positive typed-float constant case as
  `constant type is not supported yet`. Negative typed-constant rejections alone
  therefore do not prove that its narrowing policy is implemented.
- Both the native and authored type-relation tables currently admit F64 to F32.

The current stage-zero conversion is established behavior, not an accidental
math regression. The new owner decision authorizes changing that language rule;
its mismatch with stage one was already present before the math candidate.

## Production ownership

The parent subsequently authorized floating-only edits to `catalog.c`,
`Relations.chibi` and `Steps.chibi`. Those three files now implement the scalar
slice. `Compile.chibi`, `Scalar.chibi`, `Body.chibi` and `Number.chibi` remain
unchanged by this lane.

| File | Smallest relevant change to coordinate |
| --- | --- |
| `src/capabilities/catalog.c` | Restrict implicit numeric admission for typed F64 to F32. Preserve F32 to F64, integer rules and contextual literals. |
| `tools/compiler/type_relations/Relations.chibi` | Mirror the approved scalar admission rule in `NumericImplicit`. |
| `tools/compiler/package_compiler/Steps.chibi` | Admit explicit floating casts and implicit F32 to F64 at the scalar boundaries with the existing conversion instruction. Restrict implicit `Converts` paths without changing explicit cast behavior. |
| `tools/compiler/package_compiler/Compile.chibi` | Typed-float constant support, currently rejected during declaration collection. This is an additional parity gap exposed by the positive control. |
| `tools/compiler/package_compiler/Scalar.chibi` | Audit constant evaluation with the typed-float constant control; any required edit must be sequenced with active M2 ownership. |

Do not broaden `Body.chibi`'s exact `SameType` predicate. Numerical admission and
emitted conversion are separate from type identity. No change to `Number.chibi`
is justified by these runtime results; contextual literal parsing is preserved.
Additional literal/folding work requires evidence and coordination with M2.
No seed regeneration or bootstrap publication is part of this lane.

## Compatibility inventory

This is a source inspection inventory, not an exhaustive project-validation
claim. No projects, assets or compatibility aliases were modified.

| Existing consumer | Consequence to validate after the production rule changes |
| --- | --- |
| `tools/compiler/type_relations/Relations.chibi:51` | Its authored mirror currently accepts both floating directions; the new independent matrix detects this. |
| `test/language/program.c:3378` | Existing source already uses explicit float conversion and should remain accepted. |
| `test/fixtures/graph/MeshSurfaceSweep01.chibi:6` | A retained float receives `elapsed += double(dt)`; this requires review/migration under the approved narrowing rule. |
| `test/fixtures/refactor/references/behaviors/Weapon.chibi:34` | Float `elapsed` receives a double compound update and a mixed-width `min` result. The same file already uses explicit float conversion in `AdvanceElapsed`. |
| `test/fixtures/studio-workspace/content/behaviors/Weapon.chibi:36` | The corresponding fixture has the same boundary and needs coordinated expectation/source migration. |
| `packages/core/math/Math.chibi` on the separate candidate branch | Its float-only public signature needs stage-zero double-argument rejection before release. No API workaround is proposed. |

Broader reflected field, record, array and native-call conversion consumers need
owning-suite validation before integration. Static text matches do not establish
that every matched fixture currently validates or that every conversion is
within this first scalar slice.

## Reproduction and coverage

Build with `make build GRAPH_BUILD= BUILD_DIR=<build> BUILD_TARGET=test_language
JOBS=2` using the project's unsanitized Release flags. Then use the existing
supervised selectors, with `CHIBI_LANGUAGE_BUILD_DIR` and
`CHIBI_RELEASE_LANGUAGE_BUILD_DIR` pointing at `<build>/test/language`:

- `language-row-array` runs the C-fed cast and widening oracles.
- `language-semantic` runs stage-zero package admission fixtures.
- `language-type-relations-chibi` runs the independent admission matrix.
- `language-package-compiler-chibi-imports` and its `-compiled` variant run the
  stage-one fixtures and admitted source calls on both execution backends.

The final current-main results are deliberately split between runtime success
and the unmet approved admission policy:

| Selector | Result | Assertions | Test wall time |
| --- | --- | ---: | ---: |
| `language-row-array` | 18 passed, 0 failed | 18,693 | 0.951 s |
| `language-semantic` | 63 existing cases passed; approved admission case failed | 850 | 0.102 s |
| `language-type-relations-chibi` | 4 existing cases passed; approved matrix failed | 15,569 | 0.150 s |
| Stage-one imports, bytecode compiler | Existing import checks completed; approved admission failed | 7,423 | 5.127 s |
| Stage-one imports, compiled compiler | Existing import checks completed; approved admission failed | 7,221 | 3.953 s |

The matrix failure explicitly records the C implementation and both authored
backend executions returning true for F64 to F32 where the approved answer is
false. These red tests are the dependency checkpoint, not a claim that the
production contract is implemented. This branch is not ready to merge.

Initial configuration took 5.2 s and the initial driver build 44.2 s, separately
from test execution, using at most two jobs. Compiler/source formatting uses
clang-format 22. Current logs and ordinary build outputs remain under
`.chibi/float-oracles/`.

Wasm, generated C, native LLVM float execution, broad runtime/project/tooling
suites and bootstrap recovery remain unrun. Native LLVM float support is not
claimed. No PR, merge, release, force push or seed update has occurred. The
three authorized floating-only production edits remain isolated from M2.


## Implemented scalar slice, still held for the constant dependency

The source delta changes only floating admission and conversion emission in the
three authorized production files. Integer admission branches and integer
folding are preserved. Explicit floating casts use the existing conversion
instruction, with no finite assertion, clamp or precision check. Implicit float
to double conversion is actually emitted at local declarations, assignments,
arguments and returns. Call address temporaries are reserved after any new
widening temporaries, so admitting an argument does not merely relabel its bits.

Stage zero's shared numeric relation and its authored mirror now reject typed
double to float implicitly. The existing units test in `semantic.c` explicitly
spells its deliberate float conversion; its expected units diagnostic is
unchanged. No asset or user project was migrated.

The exact inspected M2 overlap was PR 687 head
`7096fdf1d97bad1d5f64b01a7bab8fa7a5ecb99f`, based on
`8b17a0edb6e67558608a81806882ec1b84bdfe01`. Of the authorized three files, M2
modified only `Steps.chibi`, including the integer cast path and constant folder.
No M2 source or semantics were imported. The coordinator must adapt the small
floating delta to M2's final accepted source, rather than replace that branch's
whole file. Prepared format remains 20 in this isolated source base.

Typed-float constants remain a separate dependency in `Compile.chibi` and
potentially `Scalar.chibi`. The fixture table marks those cases explicitly.
They run in a separate, visibly failing pico case in each compiler mode. Both
stage-one modes pass the existing import checks and all 22 nonconstant floating
fixtures, then fail only the positive `constant-round-even` case with
`constant type is not supported yet`. This is not an overall passing branch.

The implemented-source checks passed stage-zero semantics, all 64 pico cases
and 850 assertions, and the relation matrix suite, all 5 cases and 15,569
assertions. Runtime narrowing/widening checks passed 18,693 assertions. Existing
stage-one typed-package checks passed 3,594 assertions in bytecode compiler mode
and 3,222 in compiled compiler mode. Existing integer semantics checks passed
335 assertions while retaining their previously pinned conformance gaps; this
is not a new claim of complete integer frontend/backend parity.

Both production mutation controls were caught without changing fixture answers:

- Restoring broad implicit native floating admission made all six typed-narrowing
  rejection fixtures fail.
- Emitting typed zero instead of the widened value in the production helper and
  argument path left those cases compilable but produced `[0.0]` for `[1.5]`.
  The independent runtime answers caught the omitted widening across all four
  scalar boundaries and both execution backends.

The implementation bytes were restored, their hashes verified, all fixture hashes
remained unchanged during the controls, and the restored driver was rebuilt.
Logs and status JSON are in `.chibi/float-oracles/`, including
`production-results.json`, `mutation-results.json` and `restored-results.json`.
No bootstrap was regenerated or published. The math-only candidate remains on
its distinct branch and retains its release hold until coordinator integration.

## Independent-review fixture correction and retained receipts

The follow-up fixture correction leaves production source `83f9977b` unchanged.
Supported scalar rejections now require the intended narrowing diagnostic and
stable source position in both frontends; stage zero also requires exactly one
diagnostic. The two stage-one typed-constant negatives explicitly remain blocked
and earn no narrowing-policy credit. The positive constant-rounding case remains
red with its original acceptance and numeric expected answer unchanged.

Complete public-safe command/stdout/stderr receipts, the exact production
mutation driver, source/fixture hashes, and final statuses are retained in
[evidence/float-conversion-handoff.md](evidence/float-conversion-handoff.md) and
its linked text/JSON files. The unrelated-rejection production mutation is now
caught as well as the admission and wrong-emission mutations.

The retained [minimal M2 patch](evidence/float-conversion-m2-ab8c4a58.patch)
targets `ab8c4a58f6cabc8b74e9d94c37fde41cb634c2ac`. Its read-only textual apply
check passes and reversing the floating edit blocks restores exact M2 snapshots.
This is not a combined M2 build or execution claim. `Compile.chibi`,
`Scalar.chibi`, M2 integer machinery and generation remain coordinator-owned.

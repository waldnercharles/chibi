# Rich compiler integration: array debug order

This retained integration lane starts from accepted main
`19f457a9649a81ca0f816bd1141397a1d04dda12` on branch
`codex/rich-compiler-array-order-20261024`. It reconciles one bounded portion of
`aec822eb5d98cbdf987f81766e33cf02e2b33308`, preserved with evidence at
`0b1492796941d6bd4159dc8b920f7ca8fe35c2c9`.

## Selected contract

For the positional Append/Extend calls main already supports, capture the array
receiver's source output and first call input before evaluating the item. An
authored item function must not appear before its receiver in the debugger.
Keep the item input after its evaluation and the update output after mutation.

The change is confined to `package_compiler/Steps.chibi`. It moves the existing
receiver read/watches into the initial statement phase, using the handoff's
`CellRead` and typed root. Unlike the broader handoff, this debug-only slice
guards that read and root with `ops.debug`; debug-free compilation gains no
receiver read. It does not change native argument binding, update eligibility,
WRITE analysis, named arguments, value semantics, or the runtime ABI.

Eight C/pico fixture functions cover Append and Extend, local and parameter
receivers, and ordinary and method spelling. Literal expected results are
`[[1,7]]` and `[[1,7,8]]`. Both runtime backends execute the resulting closures.
An instruction-order assertion supplements the existing unordered source-watch
comparison and independently checks stage zero against the same ordering rule.
The sample also invokes nonempty `CompileClosure` on each compiler backend and
requires byte equality with the executed `Compile` closure. The debug-free
selector compiles the same sample, verifies absent debug metadata and executes
all eight functions on both runtime backends.

## Checkpoint evidence

Fresh isolated macOS arm64 checkout; existing pinned dependencies; two build
jobs. `make language-build JOBS=2` builds the standalone Debug driver using
Clang 23.1.1, without sanitizers. Tests use `tools/test_policy.py`, with
`CHIBI_LANGUAGE_BUILD_DIR=build-language`. Process-list access is needed for
the supervisor's cleanup; its sandboxed attempt started no test. The authorized
retry uses the normal supervisor. No test policy or runtime limit was changed.

Before the source fix, the new fixture failed on main: `AppendLocal` receiver
watch instruction 30 followed item invocation 23. Stage zero passed the same
ordering assertion; explicit result execution also passed before the structural
failure. The baseline run failed 1 case with 1,904 assertions, 10.334 seconds.

After the source fix and direct closure regression, checkpoint `c6b3f185e`:

| Selector | Cases | Assertions | Test wall seconds |
| --- | ---: | ---: | ---: |
| `language-package-compiler-chibi-collections` | 1 | 3,387 | 12.652 |
| `language-package-compiler-chibi-collections-compiled` | 1 | 3,142 | 10.310 |
| `language-package-compiler-chibi-debug-free` | 1 | 1,978 | 9.732 |

These are source-prepared compiler executions (bytecode and SLJIT), plus
bytecode/SLJIT execution of their output, not native LLVM execution or a new
bootstrap/recovery proof. Configuration/build time is excluded. Changed C
formatting with clang-format 22 and `git diff --check` pass.

## Preserved boundaries and next action

Read the actual rich handoff README and the pointer, values, collections, query
and lifetime documents at the supplied preserved commits. Pointer ABI21,
collections ABI22 and query ABI22 require a separate shared-contract integration;
the two ABI22 layouts are not compatible. The values/task checkpoint is
`f764c07c`, not the failed `74a0eb2c` recovery. The query lifetime draft's fixed
target set is not authored dependency inference. None is imported here.

Main's merged storage fix stays intact; the old eleven-helper workaround is
not used. M0/M1 scalar source, integer characterization, prepared ABI20 and the
checked-in compiler artifact/manifest remain unchanged. The retained artifact
SHA-256 is `d2fd2b2d9aacd7add71b4c70ce04a74ca72159a17e3220b1102020b8bd3414f7`.
This is an ungenerated source checkpoint, not an artifact/source-identity match.

Remote refresh after the focused checks found main `7ffa3ef48` (PR685 scalar
oracle improvements). Its disjoint test changes merged cleanly at `7d9850bfe`.
PR683 remains open at `8b17a0edb6e67558608a81806882ec1b84bdfe01`;
PR678 remains an open draft at `ab54e98f406d0f577b01f0ad762166b03d6d25a7`.
Neither is a dependency.

The parent subsequently identified stacked M2 PR687 at
`7096fdf1d97bad1d5f64b01a7bab8fa7a5ecb99f`, based on runtime PR683. Read-only
inspection of its exact source finds file overlap in `Steps.chibi` and
`package_compiler_chibi.c`. Its Steps hunks begin in `CallStep` and change numeric
conversion, operators and folding; this lane changes only `StatementStep`'s
receiver watches. M2 does not change the `CellRead` or `AddressSlot` helpers.
The C sample table and `run_samples` need explicit reconciliation: M2 adds an
`ahead` flag to bypass the native oracle for selected scalar samples. This
lane's array sample must keep its native oracle, emitted-order assertion,
nonempty closure comparison and literal results. No M2 code or semantic claim
is adopted here. Its prepared version21/protocol9/artifact remain outside this
lane's ABI20/protocol8 checkpoint.

Independent source review found no product correctness blocker. Its identified
coverage gap prompted one live observed `AppendLocal` invocation per compiler
and runtime backend. Callbacks check receiver `[1]`, then Item entry, input `7`,
and output `[1,7]`, in that order. They retain no borrowed payload. The test
requires the selected inspected compiled backend and rejects inspection fallback.
The development ASan/UBSan collection selector passes this observer extension:
1 case, 3,399 assertions, 57.422 seconds. The normal
`make build JOBS=2 BUILD_TARGET=test_language` also passes: native configuration
7.3 seconds, first host build 5.8 seconds, authored dependency preparation
113.2 seconds and authored graph build 255.7 seconds. This build is separate
from test time and publishes no bootstrap artifact. Final clean-candidate
checks remain pending at this source checkpoint.

The parent later confirmed the listed M2 compile-time rules were approved,
while its Scalar exact-fold implementation and prepared21/protocol9 integrated
gates remain unaccepted. The parent subsequently confirmed implicit typed
float-to-double, explicit double-to-float with ordinary ties-to-even/infinity
narrowing, and contextual literals; the separate float owner is preparing that
work. This slice neither implements nor changes any of those rules.

## Final source validation

Clean source checkpoint: `383c8fa2ce8b271059d1c17dcf8e74660cdc7b85`.
Tree: `bc544f1bf42e377b900e78f40f1a4ef8c54dc3b5`.
Fresh remote main remains `7ffa3ef48b204658ba6d9f95f6b2716039fd8798`.
The final evidence commit changes documentation/logs only relative to this
source checkpoint. Independent source and test review is clear, including
callback lifetime, bind-failure cleanup and actual inspected-backend counters.

All six selected checks pass using the normal supervisor and the normal
ASan/UBSan graph-built driver (`-fsanitize=address,undefined`, assertions on).
The collection selectors include actual live watch delivery on bytecode and
inspected SLJIT, with no inspection fallback. Native stage-zero is the
differential compiler reference; these are not native LLVM execution claims.

| Selector | Cases | Assertions | Test wall seconds |
| --- | ---: | ---: | ---: |
| `language-package-compiler-chibi-collections` | 1 | 3,399 | 56.665 |
| `language-package-compiler-chibi-collections-compiled` | 1 | 3,154 | 45.034 |
| `language-package-compiler-chibi-debug-free` | 1 | 1,978 | 41.706 |
| `language-package-compiler-chibi-scalar` | 3 | 10,251 | 44.431 |
| `language-execution` | 31 | 4,017 | 0.452 |
| `language-integer-semantics` | 2 | 335 | 209.553 |

Total: 39 cases and 23,134 assertions. The scalar check includes main PR685's
independent fixture-result oracle. Integer results characterize current main,
not the pending total-integer or M2 contract. Build time is recorded separately.
Changed C formatting and `git diff --check` pass. Full logs, command/environment,
source/tree identities and SHA-256 hashes are in
[the validation receipt](evidence/rich-compiler-array-order/validation.json).

There is no remaining source/test blocker for this bounded slice. The exact
generation request is to sequence `Steps.chibi` SHA-256
`892f77f0f2e0e334a1657d40fdc3c1d0f318fd680a9d2aa473a11e7498c66c6b`
with the accepted common compiler source, preserving this fixture during M2
test reconciliation, then have the single coordinator produce and verify one
matching artifact. Artifact publication is **pending**, not denied or attempted.
No generated compiler seed was published, and no main/PR merge was performed.
Normal ignored build/dependency directories and `.chibi/array-order` logs remain
in the isolated task workspace; no scratch executable or copied project remains.

Before artifact publication, the single generation coordinator must select
the final combined source checkpoint and regenerate/verify one matching compiler
artifact. This lane must not regenerate or publish that seed concurrently.
The source review and focused sanitizer/scalar-preservation checks above pass.
Broad native/material/project/language/tools groups, bootstrap fixed point,
isolated recovery, native LLVM, Wasm, renderer and installed SDK acceptance have
not run for this slice. Root owns final review and gated main integration.

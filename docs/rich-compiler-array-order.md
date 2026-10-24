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

After the source fix and direct closure regression:

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
oracle improvements). Its disjoint test changes will be reconciled and tested
before final handoff. PR683 remains open at `8b17a0edb6e67558608a81806882ec1b84bdfe01`;
PR678 remains an open draft at `ab54e98f406d0f577b01f0ad762166b03d6d25a7`.
Neither is a dependency.

Before artifact publication, the single generation coordinator must select
the final combined source checkpoint and regenerate/verify one matching compiler
artifact. This lane must not regenerate or publish that seed concurrently.
Independent review and focused sanitizer/scalar-preservation checks are pending.
Broad native/material/project/language/tools groups, bootstrap fixed point,
isolated recovery, native LLVM, Wasm, renderer and installed SDK acceptance have
not run for this slice. Root owns final review and gated main integration.

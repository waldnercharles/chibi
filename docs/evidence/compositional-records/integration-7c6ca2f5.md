# Records source integration after #694

This source-only candidate is based on accepted engine commit
`7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62`, on branch
`codex/compositional-records-integrate-694-20261028`. The original candidate
`bb7839acdf762a722e89b0c073f58aeda4234139` and its worktree remain frozen.
Its [earlier ledger](README.md) retains its original source and artifact attribution.

The port preserves main's charged prepared-name lookup and index-movement
accounting, every-row array-layout check, managed fixture lifetimes, immutable
text fixtures, strings discovery, native-char result checks and Linux Graph
registrations. The three textual conflicts were in `prepared_record.c`, language
CMake fixture registration and test dispatch. All sixteen authored package
compiler files and the prepared writer still match the frozen candidate.
Independent runtime/prepared and compiler/registration reviews found no new
blocker in those reconciliations.

## Build profile

The feature uses prepared format 27 and extends the native field descriptor.
Checked `bootstrap.json`, `compiler.chibip` and `runtime.json` remain byte-for-byte
equal to the accepted format-26 baseline. They were neither regenerated nor
published. The format-27 bundle from the frozen candidate is historical proof,
not the authoritative artifact for this source integration.

Source checks use the existing compiler-only SDK language harness and standalone
tooling harness: native, desktop and generated asset components are disabled for
the feature's recovery driver. The full native recovery attempt reached the cost
reporting module, which requires the generated asset service. The bootstrap
recipe now builds its first driver through the compiler-only harness with Ninja,
then restores the real native asset component after matching artifact generation.
The recipe was reviewed and dry-run; its artifact-writing stage remains held.

Clang 23 on the accepted source reports missing aggregate initializers,
intentional string concatenation and serial-mode unused scheduler state. The
diagnostic exceptions below apply only to these local snapshots:

```text
-Wno-error=missing-field-initializers
-Wno-error=string-concatenation
-Wno-error=unused-but-set-global
```

No permanent warning exemptions, portability fixture edits or serial-scheduler fixes are
included here. The portability owner retains those changes. These are source
behavior results with the stated build-only diagnostic exceptions, not strict
default-build or public SDK acceptance.

The sanitizer harness adds `-fsanitize=address,undefined` and
`-fno-omit-frame-pointer` to owned C sources and the corresponding executable
link flags. Actual Ninja compile flags were inspected. Configuration/build logs
are separate from test logs. Test wall times include in-process fixture setup
and exclude external configuration and compilation.

## Source execution

Logs are retained under
`/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/`.

| Check | Result | Test wall time | Log |
| --- | --- | ---: | --- |
| Release composition, final snapshot | 9/9; 12 authored tests per VM plus both compiler/target VMs; 9,702 assertions | 38.414s | `snapshot-language-compositional-records.log` |
| Release prepared, final snapshot | 18/18; 6,126 assertions | 1.374s | `snapshot-language-record-prepared.log` |
| Release types | 42/42; 5,103 assertions | 0.430s | `integration-types.log` |
| ASan/UBSan types | 42/42; 5,103 assertions | 2.476s | `integration-asan-types.log` |
| ASan/UBSan composition, final snapshot | 9/9; 9,702 assertions | 413.679s | `integration-asan-composition.log` |
| ASan/UBSan prepared, final snapshot | 18/18; 6,126 assertions | 1.770s | `integration-asan-prepared.log` |
| Serial/parallel dispatch controls, final snapshot | 4/4 | 1.591s | `snapshot-language-test-parallel.log` |
| Record runtime / semantics / documents | 9/9; 10/10; 5/5 | 0.509s; 0.395s; 0.188s | `integration-language-record-*.log` |
| Semantics / query program / program | 78/78; 88/88; 209/209 | 0.180s; 0.274s; 1.203s | `integration-language-*.log` |
| Accepted native-char result guard | 6/6 | 0.133s | `integration-language-native-char-results.log` |
| Source compiler types / collections | 1/1; 1/1 | 13.808s; 33.774s | `integration-language-package-compiler-chibi-{types,collections}.log` |

The earlier selected Release rows precede the removal of temporary harness
source edits; final composition/prepared/dispatch runs use the unmodified accepted
harness sources with only the recorded command-line diagnostic exceptions.
Full format checking passed in 65.2s (`integration-check-format.log`).

## Accepted-base controls

An unchanged detached checkout of the exact accepted commit was built with the
same compiler-only Release profile and warning-only diagnostic exceptions.
The accepted baseline has no asset-component omission option, so its ordinary
private asset resource was built. Its checked compiler/bootstrap/runtime hashes
and tracked source remained unchanged. This generated resource is separate from
checked bootstrap or runtime publication.

| Selector | Accepted baseline | Records source | Attribution |
| --- | ---: | ---: | --- |
| Prepared records | 16/16 | 18/18 | Two additional records boundary cases pass |
| Record values | 15/15 | 15/15 | The old `19ad570dd` exit-139 receipt is historical |
| Record compatibility | 1/1 | 1/1 | Compiler-only profile; native-service coverage excluded |
| Record state | 1/3 | 1/3 | Same two failures reproduced on accepted main |
| Authored prepared writer | 1/3 | 1/3 | Same two comparisons fail on accepted main |

Exact baseline flags, binary hash, test statuses, timings and source-clean checks
are in `integration-baseline-results.json` and `integration-baseline-identity.log`.
The final control configure took 2.754s and its build 230.689s, including normal
private asset generation. Its state/prepared/values/compatibility/writer tests
took 1.066s, 0.370s, 0.221s, 0.267s and 2.374s respectively.

## Ordered publication gate

The coordinator retains the dedicated format-27 runtime/bootstrap/SDK gate and
final merge on then-current main after the earlier library lane. That gate must
use matching headers/libraries and update the public runtime pin. Host admission
must be assessed against its then-current protocol and Make gate, including
actual stale/current and warm/cold routing fixtures; no provisional protocol
number is selected in this candidate.

Normal Graph/product builds, fresh authoritative fixed-point/artifact recovery,
installed SDK consumers, full test aggregates, other platforms, full LLVM
aggregate execution, GPU and Studio acceptance remain unverified here. This
source checkpoint does not clear those gates or other feature publication holds.

Local build directories, the detached accepted-base control, cache and logs are
retained for the ordered integration. No generated artifact is staged as a
source deliverable.

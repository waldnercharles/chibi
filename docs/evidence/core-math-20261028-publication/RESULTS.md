# Core math commit and PR preparation

This receipt follows the [original development snapshot](../core-math-20261027/RESULTS.md).
It records the subsequent independently reviewed commits and checks on accepted
engine main. SDK publication remains held. The math feature is a separate draft
from the general compiler prerequisite; no seed or ABI version changed.

## Source checkpoints

| Checkpoint | Source |
| --- | --- |
| Original development base | `7f35662ac53a3d6635eaac7b95af3f0cafbdc586` |
| Accepted retirement base | `c4f75f125ddb5e36b7f481584f1d12d26abe955a` |
| Accepted strings base | `286f69ddf8e118b099bdb50aa17767f24079e011` |
| General prerequisite, PR #790 | `b54ad3e5c6b475290d73781cac95402e172eda27` |
| Math feature tested after strings integration | `525f2be83b0702c9120f4ecc5fd8aa1b9b6a2007` |

[inputs.json](inputs.json) fingerprints all 34 changed code/build/test files of
the last checkpoint relative to accepted strings main. Subsequent documentation
commits do not change these inputs. The only rebase conflict was the language
group registration: math, package tests, core strings and Markdown each remain
registered exactly once, alongside the Wasm math selector.

The [general prerequisite PR](https://github.com/waldnercharles/chibi/pull/790)
contains four separable commits: fixture portability, imported-package
reachability, the recorded-core bootstrap bridge and a corrected reuse unit
fixture. Its eleven files contain no math package or native/graph math retirement.
Its merge remains queued behind costs PR #788 and the shared generator
input-role reconciliation.

## Math checks on accepted strings main

Native and Wasm math each run 29 authored tests on both selected backends, plus
the six C boundary cases. All rows below passed at the tested math checkpoint.
Times are command wall time, excluding builds/configuration.

| Check | Cases / assertions | Time |
| --- | ---: | ---: |
| Native package math | 6 / 4,132 | 1.441 s |
| Installed-SDK package math | 6 / 4,132 | 1.631 s |
| Native core strings/format controls | 6 / 14,750 | 19.062 s |
| Native Markdown controls | 14 / 1,778 | 2.176 s |
| Native compiler imports | 2 / 17,109 | 19.506 s |
| Native compiled-compiler imports | 2 / 16,740 | 15.098 s |
| Wasm package math | 6 / 4,132 | 0.767 s |
| Wasm core strings/format controls | 6 / 14,750 | 27.113 s |
| Wasm Markdown controls | 14 / 1,778 | 2.216 s |
| Wasm compiler imports | 2 / 17,109 | 22.335 s |
| Wasm compiled-compiler imports | 2 / 16,740 | 21.107 s |

The core controls execute 190 authored string/format tests per backend. Wasm
uses the existing embedded fixtures, with no additional filesystem access.
[wasm.json](wasm.json) records exact head/tree, selected source and artifact
hashes, commands, environments and log hashes. Selected output is in [logs/](logs/).
Tracked sources were clean before and after the Wasm run.
The [native receipt](native.json) records command environments, counts and log
hashes. The fresh [independent feature review](independent-review.json) found
no actionable issue in the exact 24-file diff.
Three checked-in failure-log copies have trailing whitespace removed for the
repository check. [log-normalization.json](log-normalization.json) records both
digests; the original task logs and their original hashes remain available.

The native SDK/language incremental build passed in 91.71 s after strings
integration. Wasm rebuilt in 22.093 s. The SDK installed successfully; the
installed consumer relinked and ran the actual installed math source. No public
SDK artifact was published.

At the preceding accepted-retirement math checkpoint
`d9cb7e26bc0adfbd33d0b98cf630fc3a6b62112d`, the full normal graph build passed in
201.38 s and the native group/feature coverage passed. Native imports and both
compiler modes also passed there. Those results are historical context, not
substitutes for the strings-integrated rows above. Earlier targeted ASan/UBSan
and numerical cost evidence remains in the original receipt; no sanitizer or
benchmark rerun is claimed for the later rebase.

## General prerequisite checks

The clean prerequisite on accepted retirement main passed the full native,
materials and projects groups. All 48 supported non-bootstrap language selectors
passed, including four completed separately after a bootstrap fixture failure
cancelled or deferred them. Linux-only scalar/native execution was explicitly
unrun on this macOS host. The full tools retry passed 219 cases and retained the
one baseline C-export failure described below.

After accepted strings main, the prerequisite's source SDK/language build passed
in 65.798 s. Native imports passed 18 cases/1,530 assertions; both compiler
import modes passed 2/14,937 and 2/14,604; package tests passed 5/270; authored
snapshot checks passed 16/286,093; and Markdown passed 14/1,778. The upstream
strings change corrected the previously contradictory Markdown assertion.

The full graph-driver refresh passed in 100.10 s on retry. The first attempt
ended during reflection without a useful diagnostic and remains in task logs.
The compiler-bootstrap unit selector passed 18 cases/204 assertions in 10.734 s
after the refresh. Production compiler and preparation source hashes are
unchanged from the earlier broader run; the accepted strings changes affect
discovery and test fixtures, and those affected checks were refreshed.

## Reuse unit fixture and real compiler provenance

The original bootstrap reuse unit test exported compiler sources from `HEAD`,
then expected the older checked artifact to regard them as its exact captured
request. Committing the compiler change correctly made production reuse refuse
that mismatch. The earlier uncommitted run had still exported the old sources.

The corrected unit fixture binds a temporary manifest to two controlled source
files. It keeps the checked closure's canonical entry, target, format/version,
length and digest guards. All ten changed-request variants, compiler/core disk
mutations, sibling additions, bad bytes/entry/format, write-error preservation
and the calibrated zero-instruction sentinel remain covered. An independent
review found no weakened check. Production reuse and the checked seed were not
changed.

That unit fixture does not prove current compiler provenance. Separately, the
existing current-source recovery selector passed 2 cases/17 assertions in
52.692 s, and the compiler self-build/fixed-point selector passed 1 case/30,097
assertions in 51.871 s. They ran against the same production compiler source on
the prerequisite before its test-only correction and accepted strings rebase.
Their proof remains separate from the controlled unit fixture and from any
future shared checked-artifact publication after the #694 sequencing gate.

## Studio companion and baseline failure

[Studio PR #104](https://github.com/waldnercharles/chibi-studio/pull/104) merged
as `be7709fd737cb82bae10fdd553772daba0bc749e` from reviewed head
`835849551d4690d2ba824071224b23cd7e4944b3`. Its two-file diff hash was unchanged
after rebasing onto accepted Studio main. Local independent review and GitHub
Codex review reported no findings; version, editor and selected-engine
build/startup CI all passed. Deployment was skipped.

[studio-merge.json](studio-merge.json) records the exact heads, merge and review
diff digest. The replacement preserves an exact double 0/1 offset from integer
`First >= 1` and the original `Count - 1 + offset` rounding order. It requires
no math SDK dependency and leaves the Studio manifest version unchanged.

The generated-C alias failure is conclusively pre-existing: a clean detached
`7f35662ac53a3d6635eaac7b95af3f0cafbdc586` standard CMake build with the fixture
enabled reproduced the identical missing `behavior_function_host_arguments`
symbol, 1 passed/1 failed and 755 assertions. The fixture, C emitter and runtime
helper source are unchanged. The accepted retirement did alter unrelated
project-document CMake linkage; the reconciled source-comparison record shows
that difference explicitly. See the
[baseline proof](../core-math-20261027/baseline-classification/classification.json).

## Remaining boundaries

The math draft still carries its explicit SDK publication hold. The general
prerequisite must reconcile the costs/library generator input roles before its
queued merge. Graph-indexed package storage must stay sparse through any future
generics fixed-point/reuse/validity passes, with compaction only at final public
enumeration. No held #694 code was introduced here.

The package's qualified `math.Step(value, edge)` remains distinct from the
separately owned lowercase synthetic vector builtin `step(edge, value)`. The
builtin owner acknowledged that it must lower without the retired native/graph
Step dependency and preserve ordinary qualified function resolution.

No Windows execution, full Wasm suite or additional GPU/UI coverage is claimed.
The baseline tooling failure is not presented as a green full release gate.
Original scratch probes, the baseline checkout and the Studio clone were
removed; ignored ordinary build products and diagnostic logs remain for review.

# Operation-contract recovery, 2026-10-22

Current recovery is based on main `1bc97b7305ee91102d3ee035e4852aa5341df31c`
and published PR407 `c541133b689c620a377982ee339c017b6d12b263`.
The scope is now Owner/Self and Delay because current main removed the public
Delta Time API. Assert remains an unknown operation. The private system Tick
instruction is preserved. Scalar validation includes the current collection
identity field, and Owner carries the invocation-read effect.

Current exact-candidate review, native builds/tests, Wasm validation and
exporter/import checks are pending. The historical native adaptation tree
`f8ea41fafb9a428e6a134a3d41acd3e269fd5189` is reported in the PR body but
was not found in the local primary object store or registered worktrees.
Its previous test outcomes do not validate this recovery.

The review thread at
[discussion 4286144466](https://github.com/waldnercharles/chibi/pull/407#discussion_r4286144466)
requests canonical zero/default dormant metadata. The published repair and
recovered fixtures retain those checks. The thread remains unresolved; the
cleared cancellation hold does not resolve the thread or supply merge approval.

All material below records historical evidence for the old Delta-containing
candidate. Its numeric test-duration policy has since been withdrawn and is
not a current restriction. No historical result supplies a current pass.

---

# Core operation contract validation

## Reviewed checked-write integration

Candidate `2ae4981bdf7ad6370c904d27a76c114cf05c39d7`, tree
`5be061a6509e066a57720cb09b9a90c652fdb5d0`, combines the independently tested
operation fix `f17e7b1406622e8f67f6105c9854aece366cc10c` with eligible main
`3c65bb60b6f3443aef6ccf4ab15acbe09b04b191`, including checked scalar writes.
Only the language source list, dispatcher and policy lists needed conflict
resolution. The lowerer retains the catalog-wide operation guard before the
checked-write path. The publication ordering, range checks, optimizer barriers,
register stack-cell reservations and backend implementation remain intact.
All previous group members and relative order remain; operation, module and
write selectors each occur once in the 31-selector language and 19-selector
Wasm groups. The structural sizing subset is not additionally selected.

Independent source/workload review approved this exact combination before the
five complete production commands below. All exited zero, ran serially, and
retained their original single shared 60-second deadline. Times exclude builds.

| Invocation | Selectors | Cases passed | Assertions | Seconds |
| --- | ---: | ---: | ---: | ---: |
| `group language` | 31 | 412 | 39,562 | 21.345 |
| `group wasm` | 19 | 256 | 35,641 | 3.531 |
| `tool native-tools` | 1 | 116 | 33,227 | 23.185 |
| `tool native-catalog` | 1 | 6 | 346 | 0.103 |
| `tool language-prepared` | 1 | 19 | 833 | 0.246 |

All nine operation cases ran once per language group, including the complete
36-rejection matrix and canonical accepts. Module and checked-write cases also
ran once. Prepared totals combine producer 6/199, runtime-only consumer 12/629
and parent 1/5. Native-tools uses the compiler-enabled minimal closure; the
catalog route uses the separately built native/compiler closure with both
native-runtime and preparation support. Four compile-only currency builds
passed separately. The 2,897 tracked source files, 89-file/63,497-byte corpus,
five compiled artifacts and policy worker retained their pre-run identities.
No unexpected failure, sanitizer report, crash or timeout occurred.

This is exact local-combination evidence, not a claim about a future published
head. Final documentation/content identity, remote-head verification, current
integration, server requirements and the explicit publication/merge holds still
apply. Native VM/SLJIT, Wasm VM/generated execution and the bounded generated-C
tooling case are covered; browser/GPU/full-engine and leak checks remain unrun.
The earlier isolated failure and five historical controls below stay separate.

## Isolated dormant metadata regression and repair

A review of published `b255df023158d6b7af3bce644d37c3ca640c29fa` found that
known-operation declarations rejected active component/aggregate/flow kinds but
accepted noncanonical dormant metadata. All twelve fields in the
[closed declaration contract](engine-operation-contracts.md#checked-declaration-and-execution-boundaries)
are zero in the actual shared and raw Owner/Delta/Delay producers. Active
unknown-operation contracts can use negative ordinals and are unaffected.
The three dormant IDs affect the capability ABI; dormant component/flow
ordinals are not hashed when their kinds are absent, but all twelve fields
participate in source-catalog snapshot comparison.

Independent review admitted the regression-only
`d581d39e3a8d393f89170e500bf5840f5640e009`, tree
`5268109b9919e52032a957336beed3aab49242a1`. The native production route failed
exactly at `Owner.component = 1`: required declaration rejection was absent.
It exited one with 8/9 cases passing, 161 assertions, in 0.092 seconds.
No malformed catalog was lowered or executed; there was no sanitizer finding,
crash or timeout. The failing source, binary and log are preserved.

Fix `f17e7b1406622e8f67f6105c9854aece366cc10c`, tree
`434bfcfff23473870c079bd19b46df46dae8151c`, adds only twelve explicit canonical
zero checks after the unchanged unknown-operation early return. The regression
inputs, loops and assertions are byte-identical to the failing baseline.
Independent source review and serial production routes passed under their
unchanged 60-second ceilings:

| Route | Cases passed | Assertions | Seconds |
| --- | ---: | ---: | ---: |
| `language-operations` | 9 | 208 | 0.097 |
| `wasm-operations` | 9 | 208 | 0.200 |

The full 36-rejection matrix and three canonical raw accepts were reached;
all eight existing cases passed. Source and five compiled artifact hashes were
unchanged after both runs. Four compile-only currency builds passed separately.
The fixed route has 82 catalog attempts (13 accepted, 69 rejected), seven source
prepare/lower attempts, four finalizations, eight dispatches and two synthetic
updates capped at 1,000 steps each; prepared bytes remain zero. Only catalog
construction grew. Newer checked-write main was excluded from this isolated
red-to-green proof and is integrated separately above. The five older controls
remain evidence for their exact guard removals, not tests of this new guard.

## Historical operation/module combination

The operation/module combination was independently reviewed and tested on
2026-10-15 at `a26d0246feb343c93f28979d96cc9e1dd1fd4df8`, tree
`a6926ee97b9dad41d3e99ab74429b8d2a2f7f95e`, against eligible main
`f1bfe133afbf214de679ca3da9a12c08b6511788`. Its other parent is the earlier
published operation head `b4d4c1f9e8ff2b98743958ac899038b49d8cc4c1`.
Operation implementations/fixtures match that parent; the module export fix and
fixtures match main. Only build-list, dispatcher and policy membership conflicts
needed combining. Every previous group member and relative order is preserved.

All five checks passed serially, each as one unchanged production 60-second
invocation. Times exclude configuration/builds; every command exited zero.

| Invocation | Selectors | Cases passed | Assertions | Seconds |
| --- | ---: | ---: | ---: | ---: |
| `group language` | 30 | 397 | 36,563 | 22.088 |
| `group wasm` | 18 | 241 | 32,680 | 7.618 |
| `tool native-tools` | 1 | 116 | 29,566 | 22.075 |
| `tool native-catalog` | 1 | 6 | 346 | 0.199 |
| `tool language-prepared` | 1 | 19 | 833 | 0.265 |

Each operation and module case ran exactly once per applicable group. Prepared
counts combine producer 6/199, runtime-only consumer 12/629 and parent 1/5.
Native-tools used the compiler-only tooling closure; native-catalog used the
native/compiler-enabled closure and retained all six preparation/native cases.
Four build-currency checks passed separately. The 64-file reviewed source
closure, 89-file corpus, policy worker and five output artifacts retained their
pre-run hashes. The backend/sanitizer limitations below still apply.

This is approval of the exact local combination, not approval of a future
published head. Final documentation/content identity, updated remote blobs,
current integration eligibility, server requirements and holds still need
verification. Earlier operation controls remain historical evidence for their
unchanged guards and fixtures; they were not rerun for this combination.

## Historical operation validation on the earlier base

The following results predate module integration and are not current-combination
runs. The earlier published `b4d4c1f9` tree also passed its separate five-check
review on base `62906a50`; integration was then blocked by main advancing. Its
retained report and hashes remain distinct from the later combined evidence.
The older implementation was validated on 2026-10-15 against eligible main
`62906a504fee6b2e0bd6349a8eeff1450529cd90`:

- Implementation and isolated controls: `986a100726357fdff43deba746ff59674bd53faf`,
  tree `c948328774187efa69cec3b9e6f0b40342f6362c`.
- Regular-gate integration: `5fbe3ce5ecc38a2316e57100fbfb92072817ac2f`,
  tree `c73937726968e019a1137302a4d2dca7c1766902`. Product and fixture sources
  are identical; the policy adds the operation selector once to each group.

This section records those historical tested heads. It covers
the [bounded Owner/Delta/Delay contract](engine-operation-contracts.md), not all L16.

## Historical review and build conditions

Independent review checked declaration shape, generated opcode identity, catalog
ABI domain separation, incremental invalidation, raw/custom binding validation,
and the whole-catalog boundary before state-read/write rewrites. Unknown host
operations gain no guarantees. Delay makes no no-reentry promise. The merged
query/scalar-read implementation remained intact; module work was excluded at
that earlier stage and is included in the later operation/module combination above.

Four build trees were current: native language, compiler-only tools, native tools
with compiler support, and Wasm language. Native uses Clang 19.1.7 Debug,
ASan/UBSan and assertions; leak detection is disabled for the existing tracing
limitation. Wasm uses Emscripten 6.0.10 Release and Node 24.19.0. Native tools retain
both native and preparation-gated catalog cases with desktop support disabled.
Official pinned dependencies and Debian libclang package checksums were verified.
Existing instrumented C++ dependencies require the C++ sanitizer and standard
runtime at link time; warnings and sanitizers were not suppressed.

Eight positional test initializers were converted to designated members in two
isolated prerequisite commits. Review checked every explicit value and implicit
zero against the actual struct fields; fixture inputs, calls and assertions are
unchanged. They compiled successfully. Their prototype/UI fixture selectors were
not independently executed by this matrix.

## Historical selected positive results

These seven routes ran on implementation `986a1007`, serially under the unchanged
production 60-second supervisor. Times include in-test setup and exclude
configuration/builds. All exited zero.

| Route | Cases passed | Assertions | Seconds |
| --- | ---: | ---: | ---: |
| `native-catalog` | 6 | 346 | 0.112 |
| `language-program` | 124 | 10,894 | 2.204 |
| `wasm-program` | 124 | 10,899 | 0.532 |
| `language-incremental` | 13 | 235 | 0.423 |
| `language-prepared` | 19 | 833 | 0.235 |
| `language-operations` | 8 | 153 | 0.083 |
| `wasm-operations` | 8 | 153 | 0.199 |

Prepared totals combine producer 6/199, runtime-only consumer 12/629 and parent
1/5. Native execution covers VM/SLJIT; Wasm covers VM/generated Wasm. Source and
all five output artifact hashes were unchanged after this selected matrix. The
subsequent regular-group integration is recorded below. The operation fixture's
then-admitted bounds were eight cases, 42 catalog attempts, seven source
preparation/lowering attempts, four tiny
finalizations, eight dispatches and two synthetic updates, with 1,000 steps per
dispatch/update; see [testing](testing.md#temporary-60-second-policy).

Earlier failures remain part of the evidence: scalar semantic flags were
incorrectly accepted; a state rewrite bypassed the call-only boundary; Delay's
initial no-reentry claim was false; fixture syntax and two missing raw Delay
bindings needed correction. Four existing semantic-negative program calls now
assert their intended diagnostic. An earlier native-catalog invocation ran zero
cases because its tools build lacked native support; the six-case result above
resolves that coverage gap. Those earlier failures are not counted as passes.

## Historical isolated negative controls

On implementation `986a1007`, each control removed exactly one reviewed
production guard/comparison, leaving fixtures and assertions unchanged. Every run exited one with 7/8 cases passing
and exactly one predicted assertion failure; no timeout or sanitizer finding.

| Control removed | Observed detection | Assertions | Seconds |
| --- | --- | ---: | ---: |
| A: known-operation ABI domain | Known and opaque identities compared equal | 147 | 0.135 |
| B: snapshot operation comparison | Inappropriate source reuse | 153 | 0.087 |
| C: native binding guard | Mismatched raw binding was accepted | 145 | 0.092 |
| D: custom lowerer binding guard | Required boundary diagnostic was missing | 153 | 0.087 |
| E: whole-catalog entry guard | Direct state-rewrite rejection requirement failed | 149 | 0.087 |

For D, the plan remained invalid with one diagnostic: downstream verification
still rejected it. This control detects loss of the required boundary diagnostic,
not unsafe execution. A/C stop before lowering the altered table; D/E never
finalize or execute altered plans. B uses the two already-admitted valid constant
libraries. Later unchanged valid cases continue after each expected case failure.

Exact clean source bytes were restored between controls; mutations never
accumulated. The final restored binary was byte-identical to the clean control
baseline and passed 8/8 cases, 153 assertions, in 0.092 seconds. The author tree
and binaries were untouched. Control builds were removed after preserving logs,
configuration, commands, patches and hashes. Log digests are recorded in
[evidence](evidence/engine-operations/log-sha256.txt).

## Historical regular gate integration

The standalone routes initially left a regular regression-coverage gap. After
independent workload admission, `5fbe3ce5` added `language-operations` once to
`GROUPS["language"]` and `wasm-operations` once to `GROUPS["wasm"]`, preserving
all previous members and their order. Both complete groups and the C tooling
policy integration then passed serially, each under one shared 60-second
production deadline, with no aggregate reconstruction:

| Invocation | Selectors | Cases passed | Assertions | Seconds |
| --- | ---: | ---: | ---: | ---: |
| `tool native-tools` | 1 | 116 | 29,566 | 23.114 |
| `group language` | 29 | 390 | 36,125 | 22.867 |
| `group wasm` | 17 | 234 | 32,242 | 3.289 |

Each of the eight operation cases ran exactly once in each language group.
`native-tools` used the compiler-only tools build and exercised the C-owned
policy integrations against the current checkout; it was neither the unit-only
route nor the wider native-enabled optional tooling closure. Native corpus
coverage used exactly 89 files, 63,497 bytes total, largest file 5,489 bytes.
Source, policy, corpus, all five output artifacts and the policy worker retained
their pre-run hashes. Configuration/builds were separate from these measurements.

The regular-gate coverage gap is closed. The native language gate is used by the
existing Linux/macOS CI workflow; the Wasm group is used by `make test-wasm`.
This does not add or activate a server CI job. These measurements establish
success for the exact candidate/environment, not a future runtime guarantee.
The earlier seven selected passes and five controls remain evidence for the
unchanged implementation, rather than being reported as reruns on `5fbe3ce5`.

## Scope of the result

Independent source/workload/test review approved the bounded implementation and
its regular-gate integration at the exact tested heads above.
Publication still requires final head/content identity, integration eligibility,
applicable server checks and all explicit holds. This matrix does not claim full
engine, browser, GPU, leak-detection or Wasm-incremental coverage. The five controls
are detection evidence for those exact removals, not exhaustive ABI verification.
Direct prepared semantics still require the manual semantic version guard.
Generic host ownership/effects and separate global/graph mappings remain future
work described in the contract.

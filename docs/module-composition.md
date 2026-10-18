# Qualified module composition: bounded prerequisite

Current source syntax is documented in [code-language.md](code-language.md).
Library declarations use `Scroll :: library { ... }`, named callables use
`Step :: (...) -> Type { ... }`, and qualified calls use `Scroll.Step(...)`.
The catalog now infers and propagates exported callable effects before consumer
analysis, including task starts and retained/invocation reads. These source changes
do not update the historical measurements recorded below.

The current module suite additionally covers imported generic specialization,
compile-time helper calls, constructed record metadata, owning-library source
provenance and generation invalidation. It also checks that invalid requests
publish no concrete generation and that floating compile-time values preserve
their exact representations during catalog comparison. These additions do not
change the historical acceptance results below.

This is a narrow proof of existing qualified library composition for the Studio
migration's L07 prerequisite. It does not complete L07, establish broader language
readiness (B02/L00/B04 or L24), or add import aliases, export/private declarations,
new storage syntax, records, lambdas, a package manager, or service lookup.

## Review correction status

The original six-case evidence below is historical. PR405 review identified that
library discovery also exported nested lexical functions. Independent execution
of the seventh regression case reproduced that blocker. Earlier approval of the
published head `0ada586a274b0137bbf93634124bac3a1b64ac43` is superseded.
Exact corrected commit `e3749f9fae281a099c553d02fbbead80fe65467b` passed
independent source/workload review and all five coordinated checks below.
This follow-up changes documentation/evidence only. The platform merge hold
remains; this evidence does not claim GitHub approval or resolved review threads.

## Concrete operation

`test/language/modules.c` translates the arithmetic in Studio's
`src/editor/rmlui/kit_code.c:scroll_sideways` at
`78cb4d4c47242db9f7af2b9573653d9756050e99`:

```
clamp(offset + cells * advance, 0, max(most, 0))
```

The fixed specimen uses `advance = 2`, `most = 6`, and four deltas
`1, 4, -1, -20`, producing `2, 6, 4, 0`. It executes the operation inline,
through a local `Step` function, and through `Scroll.Step`. A second library
exports a different same-spelled `Other.Step`; it must neither override the
qualified target nor capture the lexical helper. This is arithmetic/binding
parity, not a migrated editor scroll interaction, representative throughput,
parallel scheduling proof, or a benchmark. It uses no RML, native callable
binding, runtime service registry, or forced source flattening.

## Discovery and boundaries exercised

- The public `source_catalog_replaced` API supplies complete executable membership
  from in-memory overlays. It still discovers declarative metadata under the
  content root. The fixture owns an empty temporary root, so the inputs cannot
  acquire unrelated project declarations.
- Overlays use the existing `libraries/` and `behaviors/` paths, including nested
  library folders. Their exact paths are checked by the production catalog; the
  content-relative path supplies document identity. A library's declared name
  supplies callable qualification. A folder name is not an import alias.
- Forward and reverse overlay orders produce the same qualified resolution. The
  production catalog sorts source paths before declaration discovery, then
  `authoring_sources_prepare_catalog` links dependencies before callers. Reversing
  overlays does not test every possible filesystem order or rename.
- Distinct qualified names may have the same short spelling. An unqualified call
  is ambiguous unless an applicable lexical declaration resolves it. Two exact
  qualified callable spellings of the same capability kind cannot select a winner
  by document path. At authored export insertion, the catalog diagnoses the
  second declaration's name and location and refuses a publishable collection.
  This also rejects an authored export that would hide an existing native
  callable's exact name and kind. Native catalog aliases and events retain their
  existing insertion rules; distinct capability kinds are unchanged.
- Only authored top-level callables become qualified catalog exports. Two
  top-level functions can each declare their own `Helper`: their lexical scopes
  and callable IDs remain distinct, `Scoped.Helper` is not exported, and calling
  `Scoped.First()` and `Scoped.Second()` preserves their independent results.
  The export filter uses existing function-parent metadata; it does not remove
  nested definitions from semantic tables, lowering, or function-ID linking.
  Root macro entry/outcome ports remain associated by `callable_ports`.
- A two-library call cycle and its dependent caller have no publishable executable
  generation. This is cross-library dependency rejection, not a new rule banning
  supported local recursion or a detailed cycle-path diagnostic.
- Library state is absent from exported global values and cannot be read as
  `Store.secret` by the caller. The existing public function remains discoverable.
  This does not invent export/private metadata or a security boundary.
- Library retained state belongs to the consuming behavior attachment or system
  registration, including retention inside helpers. Module namespaces remain
  distinct, but all query matches of one system use the same library retained
  values. Coroutine locals and tasks stay with their individual invocation.
- Two simultaneously retained catalogs with the same document/module identities
  keep their own library bodies (results 3 and 7). A third catalog containing only
  the caller cannot acquire a missing library from either live collection.

## Existing coverage reused, not repeated

The following cases existed before this specimen. The corrected native language
group exercised program, publication, incremental and navigation coverage; no
additional dedicated rerun of those routes is claimed:

- `test/language/program.c`:
  `program_imported_library_preserves_owner_namespace_and_reader_after_replace`
  checks caller/library same-spelled state, distinct owners and programs,
  suspension, replacement, detach/reattach, and current owner reads. Its import
  binding is supplied by the fixture; it is not a full qualified discovery test.
- `test/language/publication.c`:
  `publication_preserves_detached_root_and_imported_shared_state` checks imported
  and root shared storage across registration publication and detached owners.
- `test/language/incremental.c`:
  `body_edits_rebuild_transitive_callers_and_preserve_unrelated_sources`,
  `signature_units_cross_libraries_and_invalidate_same_layout_callers`, and
  `invalid_edits_and_document_moves_preserve_previous_generations` cover body and
  signature invalidation, unrelated-source reuse, last valid generations, and
  explicit stable document keys during moves.
- `test/language/navigation.c` already rejects duplicate exported nominal type
  names and covers library-qualified type/signature discovery.

Remaining work includes a representative multi-feature application specimen,
qualified imported-state/publication integration, broader visibility/namespace
requirements driven by actual consumers, and any separately approved intermodule
scheduling proof. A pure imported helper still does not establish parallel safety.

## Fixed workload and validation

This section records the historical seven-case workload. The redesigned suite
now has seventeen cases; its current source-derived bounds are in
[testing.md](testing.md#qualified-module-prerequisite-admitted-fixed-workload).
The table, concurrency limits and execution observations below apply only to
the original seven-case suite.

The single existing C dispatcher gains `--modules`, containing seven pico_unit
cases. Numerical execution uses bytecode plus the platform compiled backend
(SLJIT natively; Wasm in the Emscripten build). It checks backend availability and
actual finished root/import backend identities. Diagnostic-only cases use bytecode.

Maximum work per complete selector invocation:

| Case | Catalog constructions | Source preparations | Programs | Dispatches |
| --- | ---: | ---: | ---: | ---: |
| Inline/local/imported scroll, two backends and two orders | 4 | 20 | 12 | 48 |
| Ambiguous unqualified name, two orders | 2 | 6 | 0 | 0 |
| Duplicate qualified name, two orders | 2 | 6 | 0 | 0 |
| Two-library cycle, two orders | 2 | 6 | 0 | 0 |
| Nonexported state, two orders | 2 | 4 | 0 | 0 |
| Independent catalogs, two backends | 6 | 10 | 4 | 4 |
| Nested lexical helpers, two backends and two orders | 4 | 8 | 4 | 4 |
| Total | 22 | 60 | 20 | 56 |

There are at most five sources, two libraries, and six function/handler bodies in
one collection, with at most four bodies per source. Only forward/reverse overlay
orders and fixed source bodies are used; the nested helpers return 2 and 3,
and the caller combines them into 23. The six cyclic
sources and six sources in rejected duplicate-name catalogs
never enter lowering, so the whole selector makes at most 48 source-lowering
requests and 42 finalization attempts (six invalid caller analyses do not
finalize). Each dispatch has a fixed 1,000-instruction budget. At most three
collections and one program are live together. Each case creates and removes one
empty temporary directory. It writes no source files and starts no child process,
frame loop, world, renderer, GPU work, network operation, or sleep.

Independent review admitted this seven-case workload and its native negative
measurement. It then reviewed the exact correction and admitted a bounded first
measurement of the expanded language and Wasm groups, run serially in the
coordinator's slot. `language-modules` is added once to the language group (28 to 29
commands), and `wasm-modules` once to the Wasm group (16 to 17 commands). No old
member is removed. Makefile's `test-language-all` and `test-wasm` invoke these
groups; native CI invokes `test-language-all`. Dedicated routes alone did not
guarantee that the module cases ran there.

At historical commit `cdf9adc540f1c1c0c9c8468820a1bf5b706629e4`, the unexpanded
language group measured 382 cases/35,964 assertions in 22.245 s, and Wasm measured
226/32,081 in 3.203 s. The complete changed groups were measured separately,
as recorded below; component timings were not used to guarantee their totals.
Each complete group retains one shared production 60-second deadline, including all child commands, without chunking or suppression.
Configuration/build time is separate. The existing complete `native-tools` route
also passed policy validation using its exact-source `policy_worker` and source
root. Neither language group invokes that tooling executable.

This addition does not claim literal coverage of every admitted selector:
`query-program` cases already run through `program`; the prepared producer/consumer
workflow belongs to the native group. The pre-existing standalone `wasm-format`
route remains outside the Wasm group and is not covered by another member.
No full/quarantined suite is enabled or reconstructed in chunks.

## Regression and compatibility

On the pre-fix tree `ef3cd89de26c9a54955e04e07ad2d07aea30cbe4`, independent
execution of the dedicated native selector reached six cases and 337 assertions:
five passed and the duplicate qualified-name case failed because the collection
was valid (0.169 seconds). The first run could not complete LeakSanitizer's
process scan; the retained diagnostic and rerun with `ASAN_OPTIONS=detect_leaks=0`
establish that leak checking was unavailable, not passed. AddressSanitizer and
UBSan remained enabled. The negative specimen is retained at commit `0dd6f87d`;
reviewed main `b1ada743` is integrated before the correction.

The old identity-only insertion check allowed two distinct executable documents
to export `Same.Step`. Exact-name lookup returned the first catalog entry after
path sorting. The correction is confined to authored callable export insertion,
not generic capability lookup or the shared insertion helper. Duplicate identity
handling, normalized short-name ambiguity, lexical resolution and other
capability kinds are unchanged. No source priority or new visibility policy is
introduced.

The review regression-only tree `3ce898152de5e23316f3a6e144b0460c0d6f6ab6`
on published head `0ada586a` reached seven cases/350 assertions: the original six
passed and `modules_nested_same_spelled_helpers_stay_in_lexical_scope` failed at
`valid(0)` with `Duplicate exported callable name 'Scoped.Helper'` (0.179 s,
exit 1). The inferred top-level bodies cause declaration analysis to discover
both nested functions. Export discovery previously flattened their names despite
different lexical IDs. It now skips authored functions with a nonnegative
`parent`, preserving true top-level duplicate rejection and lexical definitions.
This negative evidence is retained; it does not count as a corrected-source pass.

Static inspection found 17 engine fixture library files with distinct declared
library names within each content root and no nested authored callable
declarations. Studio projects at `78cb4d4c` contain no library sources. Celeste was unavailable for this inspection. No affected existing
project or serialized rewrite was identified, so no project-version bump or
migration is proposed. For a hand-made project with duplicate exact callable
exports, give the conflicting library/function a distinct name and update its
qualified references.

## Corrected-source verification

Independent review approved exact source commit
`e3749f9fae281a099c553d02fbbead80fe65467b`
(tree `4962e9a74bbac9935f38bee5ef3816435cd86d14`) on 2026-10-15.
Each check ran once, serially, under the unchanged production supervisor:

| Route | Commands | Cases | Assertions | Wall time |
| --- | ---: | ---: | ---: | ---: |
| `tool language-modules` | 1 | 7 | 438 | 0.204 s |
| `tool wasm-modules` | 1 | 7 | 438 | 0.251 s |
| `group language` | 29 | 389 | 36,402 | 23.985 s |
| `group wasm` | 17 | 233 | 32,519 | 4.223 s |
| `tool native-tools` | 1 | 116 | 29,566 | 21.699 s |

All five exited 0 with no failed cases. Each entire command/group retained its
shared 60-second ceiling, including fixture setup and owned process cleanup.
The group logs contain the module suite exactly once and reconcile to the prior
aggregate case/assertion counts plus 7/438. The native language group includes
navigation (29/329), incremental (13/235), program/imported-state and publication
coverage. The existing native-tools deadline-failure outputs are deliberate
negative-control fixtures, not outer retries or suppressed failures.

Native and tooling used Clang 19 Debug with ASan/UBSan; leak detection remained
unavailable under tracing (`ASAN_OPTIONS=detect_leaks=0`). Wasm used Emscripten
Release and Node 24. All three compile-only `-j2` currency checks reported no work
before execution, and source/artifact hashes matched before and after. These
functional-test timings are not benchmarks or a speedup claim. The documentation
follow-up preserves all tested product, test, policy and fixture bytes and adds
no runtime execution.

The [validation record](module-composition-validation.json) preserves the exact
tested commit, source/artifact/log hashes, commands, environment, workload bounds,
negative regressions and historical approval supersession. No full/quarantined
gate, representative whole-app proof, L07/L24 completion, prepared producer/consumer
run, or standalone Wasm formatter pass is claimed. Publication, final published-head
review and the explicit platform merge hold remain separate.

## Historical measured verification

These results predate the confirmed nested-helper blocker and do not approve
the current correction or authorize landing the earlier head. Independent review
and the four selected checks passed on exact source commit
`072be745a8f5c73e14c1e07b0a1704c4de8555a8`
(tree `5e61dfbe04f84e3260821f04297bc16e2adcf52e`) on 2026-10-15:

| Dedicated production-supervisor route | Cases | Assertions | Wall time |
| --- | ---: | ---: | ---: |
| `language-modules` | 6 | 348 | 0.168 s |
| `wasm-modules` | 6 | 348 | 0.227 s |
| `language-navigation` | 29 | 329 | 0.183 s |
| `language-incremental` | 13 | 235 | 0.430 s |

Every command exited 0 under its unchanged shared 60-second ceiling, including
fixture setup. Native used Clang 19 Debug with AddressSanitizer and UBSan;
`ASAN_OPTIONS=detect_leaks=0` reflects the documented LeakSanitizer environment
limitation. Wasm used Emscripten Release and Node 24. Builds were current before
execution. These subsecond timings are functional-test durations, not performance
benchmarks or evidence of a speedup.

The [machine-readable evidence](module-composition-validation.json) records exact
source/artifact hashes, command configuration, baseline failure and result
summaries. That initial integration added only documentation/evidence and the
approved docs-only roadmap from main `19a8439314a1057d4a1c7c4e0e5e8ba47da7bd02`;
its product, test and policy bytes were those of the tested commit. No new runtime
was run for those documentation-only changes. The current review correction
changed product, test and group-membership bytes; its new validation is recorded
above. Publication and final published-head review remain separate. At the initial
six-case stage, imported-state/program and publication cases were not rerun; the
corrected native language group now includes them. Full/quarantined gates,
whole-app behavior, L07 completion and broader readiness remain unclaimed.

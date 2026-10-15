# Qualified module composition: bounded prerequisite

This is a narrow proof of existing qualified library composition for the Studio
migration's L07 prerequisite. It does not complete L07, establish broader language
readiness (B02/L00/B04 or L24), or add import aliases, export/private declarations,
new storage syntax, records, lambdas, a package manager, or service lookup.

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
- A two-library call cycle and its dependent caller have no publishable executable
  generation. This is cross-library dependency rejection, not a new rule banning
  supported local recursion or a detailed cycle-path diagnostic.
- Library state is absent from exported global values and cannot be read as
  `Store.secret` by the caller. The existing public function remains discoverable.
  This does not invent export/private metadata or a security boundary.
- Two simultaneously retained catalogs with the same document/module identities
  keep their own library bodies (results 3 and 7). A third catalog containing only
  the caller cannot acquire a missing library from either live collection.

## Existing coverage reused, not repeated

The following are existing C cases, not new claims that they ran with this
specimen:

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

The single existing C dispatcher gains `--modules`, containing six pico_unit
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
| Total | 18 | 52 | 16 | 52 |

There are at most five sources, two libraries, and six function/handler bodies in
one collection, with at most two bodies per source. Only forward/reverse overlay
orders and the two fixed independent `Store` bodies are used. The six cyclic
sources and six sources in rejected duplicate-name catalogs
never enter lowering, so the whole selector makes at most 40 source-lowering
requests and 34 finalization attempts (six invalid caller analyses do not
finalize). Each dispatch has a fixed 1,000-instruction budget. At most three
collections and one program are live together. Each case creates and removes one
empty temporary directory. It writes no source files and starts no child process,
frame loop, world, renderer, GPU work, network operation, or sleep.

Independent review admitted this fixed workload, followed by separately
coordinated runs of the dedicated `language-modules` and `wasm-modules` routes.
Aggregate membership is unchanged. The production supervisor retains its one
shared 60-second limit per selected command; configuration/build time is separate.
No full/quarantined suite is enabled, and these cases cannot be used to
reconstruct one in chunks.

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

Static inspection found 17 engine fixture library files with distinct declared
library names within each content root. Studio projects at `78cb4d4c` contain no
library sources. Celeste was unavailable for this inspection. No affected existing
project or serialized rewrite was identified, so no project-version bump or
migration is proposed. For a hand-made project with duplicate exact callable
exports, give the conflicting library/function a distinct name and update its
qualified references.

## Measured verification

Independent review and the four selected checks passed on exact source commit
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
summaries. The final integration adds only documentation/evidence and the
approved docs-only roadmap from main `19a8439314a1057d4a1c7c4e0e5e8ba47da7bd02`;
product, test and policy bytes remain those of the tested commit. No new runtime
was run for those documentation-only changes. Publication and final published-head
review are separate from this source validation. Existing imported-state/program
and publication cases listed above were not rerun here. Full/quarantined gates,
whole-app behavior, L07 completion and broader readiness remain unclaimed.

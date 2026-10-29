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

# Static prototype source edits

`authoring/source_prototype_edit.h`, installed by `Chibi::source_catalog`, edits
one `.chibi` prototype without files, runtime actors, overlays or editor history.
The result owns both the candidate source and the validated `prototype`; free
both with `authoring_prototype_edit_result_free`. Registry descriptors and its
include context remain borrowed by the model.

The operation accepts reflected field paths, explicit component-creation
records, component removal and behavior-symbol attachment/detachment. Component
creation values must come from the consumer's existing native policy, including
contextual defaults. They must not be replaced by an empty record. Runtime
entity IDs are rejected; a zero entity slot is emitted as `null`. Existing
symbolic links are preserved, and explicitly setting a link to null clears it.
Prototype reference values use the script catalog's symbol resolution.
Behavior attachment rejects an already-present symbol. Detachment removes all
occurrences of that symbol, including duplicates inherited from bases or authored
locally. Surgical deletion keeps surviving order and comments; inherited
composition may be materialized as described below. This is the source-operation
contract; the legacy `prototype_remove_graph` helper is unchanged.

Count zero prepares an owned source/model without changing text and permits a
null script catalog. Editing needs a script catalog. The caller must verify its
source and dependency revisions again before adopting the result. No error
returns a partial source or model. Input strings and typed payloads are borrowed
and never changed. Batches reject overlapping field paths and component or
behavior operations that contradict each other. They are never partially
accepted. The public bounds are 1,024 operations and 1 MiB source/output; callers
must report a limit failure without splitting one history gesture.

Ordinary edits preserve bases and unrelated bytes. Existing authored field
values are replaced in their located spans; missing fields/components are
inserted using syntax-owned boundaries. Null strings in complete native records
are omitted, retaining the loader's zero initialization.

Nested structs retain the existing language contract: an authored nested record
replaces the whole record. Editing an already authored axis is surgical. Creating
an omitted ancestor record writes its current resolved sibling values too.
Those siblings become explicit values; this API does not add per-axis inheritance
or change `prototype.c` loading semantics.

Removing an inherited component or behavior has no suppression syntax. Only
that structural operation materializes the resolved composition and removes
the member. The result reports `materialized`; an editor should explain this
and retain the exact original source for Undo. Materialization preserves local
component/field bytes where possible, supplies inherited fields, removes bases,
and retains identity, children and their retained components, relationships, symbolic handles and behavior
order. A newly prepared candidate is compared with the original model before
removal. Unsupported composition fails before acceptance. Changed composition
properties may be rewritten; this is not a general source formatter.

## Build and install ownership

The root `CMakeLists.txt` includes `sdk` at line 190. The SDK's compiler branch
includes `source_authoring.cmake`, so the root products and installed consumers
compile the same `chibi_source_catalog` translation units. This change adds the
writer to that target and the public header to `chibi_sdk_headers`, whose normal
copy/install route installs it under the source-catalog include root. The target
now publicly propagates `chibi_prototype_document` because the public result owns
a `prototype *`. Existing `chibi_authoring` supplies catalog/literal/syntax types;
`support/text_edit.h` and its implementation already belong to `chibi_support`
through the existing package dependencies. No private include override or second
writer implementation is introduced.

## Focused validation workload

The existing `test_language` executable dispatches `--source-prototype-edits`
to eight C/pico cases in `test/language/source_assets.c`. It exits nonzero rather
than silently passing when `CHIBI_TEST_SOURCE_ASSETS` is absent. The installed
SDK build of `test/language` already links `Chibi::source_catalog` and compiles
that file when `Chibi::project_compilation` is available.

The cases contain 26 public API calls: 17 accepted and nine rejected; accepted
calls total 18 operations, with at most four operations in a valid batch. Two
rejected bound checks pass counts of 1,025 and -1 and are rejected before reading
the operation array. There are eight fixed tiny in-memory fixture cases, two native
components, one nested record, one enum, one prototype-reference capability and
one relationship. There are no files, worlds, hosts, frames, subprocesses,
network requests, rendering or GPU use. Fixture strings and materialized outputs
are each below 4 KiB by construction. Cleanup frees all source/model/document
allocations and restores the process type-catalog slot.

Coverage: normalized-spelling byte preservation and no-op; sparse inherited
fields following a later base edit; nested-axis current-value retention;
explicit creation defaults, escaped strings, enum, prototype symbol, nullable
string and integer extrema; local removal; inherited structural materialization
with child/retained component, relationship, symbolic handle, identity and ordered behaviors;
behavior array punctuation/comments, adjacent/tail/all-local duplicates and inherited-plus-local duplicates; a four-operation batch; explicit symbolic
link clearing; invalid source, nonfinite value, nominal mismatch, runtime handle,
missing reference/catalog, overlapping batch and operation-limit rejection.

Run the standalone production route for source-prototype edit coverage:

```
CHIBI_LANGUAGE_BUILD_DIR=<exact-installed-sdk-consumer-build> \
  python3 tools/test_policy.py tool source-prototype-edits
```

This selector runs through the production supervisor and is not added to an
aggregate. Freeze engine head, installed SDK identity, fixture
blob and executable hash. Configuration/build timing is separate from execution.
A first run is a supervised measurement, not prior proof of runtime.

For meaningful failed-before evidence, keep the final header and fixtures but
replace only the writer with its published
`c4a8b09a837a737126b8b560b3ff67af9b7af9c2` version (blob
`1586d46923a741f1ccbf9f60d3f9323b2079d2f8`). Build and run the same selector.
The behavior case must fail `!prototype_has_graph(bridge_result.value, "First")`
after its local duplicate detach; compile failure is not behavioral red. Restore
the final writer, rebuild and require all eight cases to pass. This exact prior
writer replaces the artificial SET_FIELD no-op as the required engine negative
control, without adding another runtime invocation.

A same-API SET_FIELD no-op (discard the normally validated candidate and return
`xstrdup(s->source)`) remains an optional diagnostic, not a required gate. It
cannot prove the duplicate-detachment correction. Likewise an optional
materialization mutant using `prototype_clone` as comparison baseline would
exercise unsorted graph order; do not change that legacy helper's semantics.
Neither control changes the loader or legacy `prototype_remove_graph` helper.

The selected root ASan/UBSan invocation exercises these eight cases for the
source/model/text ownership and rejection paths. It does not establish coverage
for another selector or a broad suite.

Local authoring checks are syntax-only. Linked tests, installed-SDK consumption
and hosted runtime results must be recorded for the exact candidate
before landing. This additive API changes no language syntax or project format;
no project version bump or migration is needed.

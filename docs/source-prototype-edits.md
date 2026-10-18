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

The cases contain 25 public API calls: 16 accepted and nine rejected; accepted
calls total 17 operations, with at most four operations in a valid batch. Two
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
behavior array punctuation/comments; a four-operation batch; explicit symbolic
link clearing; invalid source, nonfinite value, nominal mismatch, runtime handle,
missing reference/catalog, overlapping batch and operation-limit rejection.

After independent workload admission, run only the standalone production route:

```
CHIBI_LANGUAGE_BUILD_DIR=<exact-installed-sdk-consumer-build> \
  python3 tools/test_policy.py tool source-prototype-edits
```

The existing whole-invocation 60-second supervisor is unchanged. This selector
is not added to an aggregate. Freeze engine head, installed SDK identity, fixture
blob and executable hash. Configuration/build timing is separate from execution.
A first run is a supervised measurement, not prior proof of runtime.

For meaningful red evidence, keep the final SDK/header/fixtures and change only
the `AUTHORING_PROTOTYPE_SET_FIELD` branch: keep the normal `set_field` call,
and when it returns a nonnull candidate, free that candidate and substitute
`xstrdup(s->source)`. This retains normal validation and helper references while
discarding only the proposed edit.
Compile that same-API no-op mutant and run the same admitted selector; the first
case must fail its changed/source-value assertion. Restore the product branch,
rebuild/install, verify hashes, and rerun. An old SDK's missing-symbol compile
failure is not behavioral red evidence. An optional diagnostic materialization mutant that uses `prototype_clone`
for the comparison baseline should fail the unsorted behavior-order preservation
fixture; this is not an additional required gate. Do not change `prototype_clone`
semantics.

One additional root ASan/UBSan invocation of only these eight cases is admitted
for the source/model/text ownership and rejection paths, under the same unchanged
60-second supervisor. It does not authorize another selector or a broad suite.

Local authoring checks are syntax-only. Linked tests, installed-SDK consumption
and hosted runtime results must be recorded for the exact candidate
before landing. This additive API changes no language syntax or project format;
no project version bump or migration is needed.

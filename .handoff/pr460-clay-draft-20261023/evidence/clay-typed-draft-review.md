# Unapplied clay draft receipt

The complete fixture draft is in `clay-typed-draft/`. It contains 18 files and
59,424 bytes: 13 compiled data Build sources, three executable declarations,
README and the unchanged preview request. It contains no KDL asset files.
`clay-typed-draft-manifest.json` records every original/new path, byte count and
SHA256. `clay-typed-draft.diff` is a source diff against immutable published
aa7 fixture content, with paths normalized to `test/fixtures/clay-lab/`.

The hand mapping is recorded in `write_clay_typed_draft.py`; it does not parse
KDL or provide an asset loader. Its source generation is not compiler, engine
formatting, test or rendering evidence. The immutable original corpus remains
separate in `clay-source-aa7/`.

All eight KDL paths have `.chibi` replacements. The five existing prototype
paths now contain ordinary data Build sources. ClaySettings is byte-identical
to the published executable declaration. ClayControls retains the five named
actions, toggle/reset values and dirty writes, with explicit event signatures
and real spellings for its floating settings. ApplyClay retains the early
dirty return, has_form guard, one set_shape_parameter(Form), four
set_material_parameter calls and final dirty=false; only its Tick signature
and numeric cast spelling change. The preview JSON is byte-identical.

Project version is Int(33); scene version is UInt(1); graph versions are UInt(3).
IDs and counters use UInt. Graph positions/default floats and reflected native
float fields use Real. Colors use UInt. The old function formulas, IDs, pin IDs,
edges, nodes, scene order, overlays and preset inheritance were hand-mapped.
Terracotta parameters contain complete compiled source for material.BaseColor,
with three Real values, inside the native string field. No setter aliases,
native capabilities, serializers or schema changes were added.

The draft remains unvalidated. Concrete execution uncertainties are:

- It has not passed the final integrated compiler's ordinary asset Build,
  declaration/type analysis, engine canonicality or native reflected decoder.
  Current explicit event and float constructor spellings follow checkpoint310
  examples, but this draft has not been compiled.
- No converted model/material has been prepared or published into the real
  CPU resource host. Final prepared resource count/bytes and semantic setter
  behavior remain for the existing C/pico assertions. The converted source
  still fits the old fixture's 128-KiB source cap, with the same 18 files and
  directory layout; no source cap was increased.
- Current main/compiler/typed migration310 still needs a clean combined
  checkpoint. The material owner's review confirmed the source contract only;
  it was not a compiler, render or visual pass. No unresolved data-field or
  numeric-kind guess was substituted for that review.
- C tests, dispatchers, CMake/Graph source inventories, current policy routes
  and fixed fixture canonicality inventory have not been changed. The exact
  adaptation plan remains in `clay-typed-conversion-plan.md`.
- Current-candidate ordinary drawing, real Linux/glibc sanitizer evidence,
  matching immutable dependency archives and clay image inspection remain
  outstanding. Old KDL fixture results are not results for this source draft.

Independent agent `review_clay_draft` completed source fidelity review with no
confirmed mismatches. It checked graph/pin IDs, formulas, defaults, edge order,
11 scene entities and overlays, five prototypes and inheritance, numeric kinds,
executable semantics, complete terracotta Build text and exact setter names.
Compilation, canonicality and runtime behavior remain unverified.

The renderer repair candidate remains staged at tree
`adabdc085fa2d78f7f7a9977e0adeec1a353853f`, unchanged.
No engine checkout, retired repository or other owner's files were edited.
No engine build, test, GPU run, commit or publication was started.

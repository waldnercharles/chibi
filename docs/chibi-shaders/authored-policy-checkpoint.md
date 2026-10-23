# Executable authored shader policy checkpoint

This checkpoint recovers the authored shader handoff against main `530ada6c`
without introducing a second compiler frontend or changing production policy.

## Delivered

- `compiler_facts/Facts.chibi` is the preserved owned semantic contract, not an
  implementation of `Analyze`. Its `Accepted` payload calls the entry index
  `entry_function`: the handoff spelling `entry` is a reserved keyword.
- `shader_source/SourceMap.chibi` is the preserved pure provenance policy. Its
  `FindNode` continuation parameter is `after_node`: the handoff spelling
  `after` is also reserved. No policy behavior is changed by either spelling fix.
- `test_language --shader-source-chibi` compiles both packages through the
  existing package fixture and invokes the real authored policy on bytecode and
  the configured compiled backend. Assertions remain C/pico_unit. C only packs
  supplied rows and observes results; it does not manufacture compiler semantics.

The seven fixed cases cover validation, ranged rejections, full-width identities,
explicit alias result bindings, origin-only caller chains, ordered source
closures, changed imported text/identity, embedded NUL text, generation guards,
size limits, missing queries/call links/cycles, and detached-result lifetime after
input and prepared owners are destroyed. There are 84 explicit policy calls,
each with a one-million instruction cap, plus fixture preparation; four source
files are prepared once on each backend. No GPU, subprocess or source discovery
runs inside the selector. It is registered in the ordinary language CI group,
agent CPU routes, and both CMake and authored Graph test inventories.

## Provenance and validation

The source handoff is `6dc3219759f5aac491b1bc94231de63d75845e0c`, with exact
preserved blobs `1f3e18588dc51ed73c9ad3f4f17d63f771907003` (Facts) and
`bc3e72e1d50292543b9723e9f6156f8c6d03a75f` (SourceMap). The original declaration
fails source parsing at Facts line 183; after fixing that spelling the original
SourceMap fails at line 295. The same focused check passes with both corrections.
Historical handoff/evidence branches remain intact.

Local validation uses the published Linux SDK for exact main `530ada6c`, with
its SHA-256-verified archive, to build the ordinary language dispatcher. It
exercises the existing package-preparation path, not the missing authored
`Analyze` export. Other platforms, renderer/GPU and Studio are not covered by
this checkpoint. The broad native/material/project/tool suites are not implied
by the focused result.

## Remaining boundary

Production inspection still uses the already merged CPU `source_debug` policy.
The authored package is tested preparation source, not an installed prepared
service and not a second production authority. A future atomic service cutover
must remove overlapping C policy while keeping native memory/IR validation and
float evaluation in their existing owners. Aggregate `SourceResult.field` rows
are not yet transportable through the current native scalar/vector result API.

There is still no ordinary owned `Analyze` producer and no source-to-shader-IR
acceptance. Private scalar optimization facts, bytecode or compiler work records
cannot fill that gap. A separate bounded facts-to-IR consumer can now use the
compiled contract; it must still be labeled a supplied-facts test until ordinary
compiler production exists. Nested material records, typed source-material
finalization, prepared resources, same-generation rendering and Studio remain
separate integration gates. No project contract used by an existing project is
changed, so no project version bump or migration is needed.

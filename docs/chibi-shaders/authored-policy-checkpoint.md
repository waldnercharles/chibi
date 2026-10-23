# Executable authored shader policy and scalar lowering

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
- `shader_source/Lower.chibi` now consumes that public facts contract for one
  nullary pure source function returning a nonnominal float. It lowers exact
  finite float32 constants, add/multiply and initialized immutable locals. It
  does not parse or resolve source, read private compiler records, or decode
  prepared instructions.

The seven policy cases cover validation, ranged rejections, full-width identities,
explicit alias result bindings, origin-only caller chains, ordered source
closures, changed imported text/identity, embedded NUL text, generation guards,
size limits, missing queries/call links/cycles, and detached-result lifetime after
input and prepared owners are destroyed. There are 84 explicit policy calls,
each with a one-million instruction cap, plus fixture preparation; four source
files were prepared once on each backend in the first checkpoint. No GPU,
subprocess or source discovery runs inside the selector. It is registered in the ordinary language CI group,
agent CPU routes, and both CMake and authored Graph test inventories.

## Scalar consumer checkpoint

The five additional cases run 66 lowering calls, for twelve cases and 150 explicit
authored calls total. Five source files are prepared once on each backend. The
C adapter only copies scalar opcodes, operand indices, exact bits and source IDs
into the existing `shader_graph`; it then calls existing validation/evaluation.
There is no new evaluator or production native adapter.

- The supplied-facts arithmetic fixture evaluates `(0.5 + 0.125) * 2` to `1.25`.
  Two local bindings and their reads share the original node through explicit
  principal-result rows, not an inferred node-order convention. A shared-child
  expression DAG emits each expression once, reusing the same operand node.
- Repeated lowering and both backends retain deterministic, nonzero bundle-local
  IDs. The origin-table position determines this scalar slice's ID; opaque CST
  anchors are never dereferenced and full-width compiler occurrences stay
  separate. These IDs are not live program-generation tokens. Calls are rejected,
  so this slice produces no helper-expansion call contexts.
- Negative zero, the smallest subnormal, maximum finite float32 and positive
  zero retain exact bits through both transport and native CPU evaluation.
  Nonfinite constants and nonzero high bits are rejected, not silently narrowed.
- Twenty-five mutations check ranged rejection of effects, parameters, unsupported
  statements/calls/construction, writes/ref escape, forward reads, nominal or
  non-float/qualified types, bad spans/references, expression cycles and invalid
  initializations and conflicting origins. Ordinary compiler diagnostics pass
  through unchanged. Unsupported unused initializers are still rejected; two bad
  operands report source argument order rather than expression-pool order.
- Both policy and lowering results are inspected after caller inputs and prepared
  owners retire. The retained lowered bundle can still produce the same native
  CPU graph result. No runtime source compiler fallback is involved.

`ScalarTarget` supplies matching linked float storage and native opcode values;
it is not an untrusted semantic schema or an ABI fingerprint. `ScalarNode` is a
small scalar-only transport view of existing graph fields. No material output
layout, uniforms, calls, conversions, projections, records or boolean entry is
accepted. All principal result fields are `-1`. Ready bundles retain the whole
supplied document closure; rejections retain the owned message/range but require
the caller’s document context to render it. Entry/catalog/lowering identity and
a native generation must still be bound by a future production bundle/service.

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
`Analyze` export. Maximum accepted table sizes, total-closure byte capacity,
aggregate fields and production transport are not exhaustively tested. Other
platforms, renderer/GPU and Studio are not covered by this checkpoint. The broad native/material/project/tool suites are not implied
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
cannot fill that gap. The bounded facts-to-IR consumer remains explicitly a
supplied-facts test until ordinary compiler production exists. Nested material
records, typed source-material finalization, prepared resources, same-generation rendering and Studio remain
separate integration gates. No project contract used by an existing project is
changed, so no project version bump or migration is needed.

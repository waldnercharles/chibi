# Chibi material and shader source

Status: CPU source-inspection foundation implemented locally, 2026-10-22. No authoring cutover or
GPU parity claim is established by this plan.

## Ownership and checkpoints

Engine checkout: `/Users/cwaldner/Personal/chibi/.worktrees/chibi-shader-authoring/chibi`.
Branch: `codex/chibi-shader-authoring-20261022`, based on main
`1bc97b7305ee91102d3ee035e4852aa5341df31c` with ABI20.

This increment owns new shader source packages, shader source inspection,
associated fixtures and this document. Initially reserve
`src/runtime/shaders/inspection.[ch]` and new `source_debug.[ch]` files.
Coordinate additions to SDK/test dispatch files with their owners.

Typed assets owns `packages/game/assets`, `tools/compiler/asset_{compiler,source}`,
asset compiler adapters, source asset/catalog/edit services and migrated assets,
including paired Studio changes. Its local checkpoint `a504d8e59` is historical;
current-main integration and a reviewed clean dependency checkpoint are pending.
Its contract is ordinary packages with `pub Build() -> byte[]`, typed constructors,
and `data.Encode` with exact Int/UInt/Real identity. Graph families retain their IR.

The compiler/default owner reserves package compiler, native catalog/exporter,
StageOne, checked bootstrap and the source-library/native graph projection bridge.
Use ABI20 and exact catalog identities. Do not independently regenerate bootstrap
or import proposed pointer, query or ABI21 changes. Their next reviewed checkpoint
is pending in `codex/selfhost-named-results-20261022`.

## Existing implementation

`src/runtime/shaders/program.h` defines the authoritative topologically ordered
`shader_graph` and `shader_node`, including `source_id`. `program.c` validates,
evaluates CPU values and emits GLSL from that IR. The initial limits are 1–4 float
lanes, 32 parameter slots, 9 surface outputs and 1024 nodes. Matrices do not fit
this IR yet. `src/graph/shader/shader_adapter.c` lowers the editable graph model;
`shader_graph_from_graph` and `shader_graph_from_function` preserve source IDs.

`src/runtime/shaders/inspection.[ch]` currently reports static cost estimates.
CPU inspection can reuse `shader_graph_eval_values`, retaining each intermediate
value, without adding another interpreter. Existing CPU consumers, prepared IR,
GLSL emission, material/shape rendering, picking and baking must continue using
the same program.

`src/authoring/source_graph_library.c` is a limited scalar, pure, single-return
projection from native semantic analysis. It silently omits unprojectable
functions. It is not a suitable complete shader source frontend and remains
reserved to the compiler owner until an explicit interface is agreed.

## Source contract

Author shaders with ordinary Chibi declarations and named
records. A material names an entry function and its uniform defaults through the
typed asset contract. The entry takes explicit sample inputs and parameters and
returns a typed surface record. Imported ordinary pure functions are reusable
shader libraries. Source is authoritative. Graphs, source maps, prepared IR and
backend code are derived products of the same revision.

The authored compiler supplies syntax and typed declaration/expression facts.
An authored Chibi shader-domain pass validates reachable functions and lowers
those facts directly to existing shader operations. Do not add a new parser,
embedded shader strings, native semantic compiler, prepared-bytecode decompiler
or a second CPU implementation. Native adapters only invoke prepared code,
transport validated owned IR and expose runtime/platform operations.

First source subset: finite `float` and 2/3/4-lane records with 32-bit fields,
explicitly typed parameters, immutable locals, component construction/projection,
scalar arithmetic, supported current IR math built-ins, explicit sample
position/UV/time inputs, one surface return and nonrecursive pure helper calls.
`number`/`double` arithmetic is not silently narrowed into shader floats. Include
named Surface construction, sample/parameter record field projection and boolean
literals at the Unlit boundary. The compiler owner confirmed there is no current
package compiler built-in vec3 type. Start with ordinary float-field domain
records and scalar component arithmetic as shown in [typed-facts.md](typed-facts.md).
Vector operator sugar is a later language increment, not a prerequisite for the
first shader.
Surface fields follow the existing output defaults: Base Color `vec3(1)`,
Emission `vec3(0)`, Roughness `0.5`, Metallic `0`, Normal `vec3(0)`,
Opacity `1`, Unlit `false`, Height `0`, Occlusion `1`. The zero normal preserves
the geometry normal. The first material is opaque; mask
and blend settings stay material metadata with the existing opacity lowering.
Reachable unsupported statements, types, effects, mutable storage, recursion,
dynamic indexing, arrays/maps/pointers/tasks, unknown intrinsics and raw GPU
effects produce diagnostics at their source range. Do not silently omit them.
Loops, general control flow, matrices and additional rendering stages follow
only when they have one CPU/GPU contract and IR representation.

The first fixture is an unlit position-based color material with a named helper,
a uniform gain and nonconstant intermediate values. It must be a real renderable
material, not just a serializer or disconnected compiler demonstration.

## Source-level CPU inspection

Associate each source operation ID with document identity, byte range and the
exact source revision. One source expression can lower to several IR nodes;
synthetic nodes may inherit their originating expression. Each principal source
result binds explicitly to its IR result node and width; aliases can bind an
existing node. Auxiliary-node provenance is separate. Repeated helper expansions
have distinct contextual source IDs, the original occurrence ID and a call-site
chain, so two calls with different arguments remain distinguishable.
A debug snapshot owns
the evaluated values and source-map association for one immutable program/sample.
Inspection lists source expressions and their typed intermediate values, with
caller-supplied coordinates, time, uniforms and texture resolver. A stale source
revision is rejected before values are attached to the editor buffer.

Initial inspection provides source watches and node lookup. Interactive stepping
and breakpoints are a later Studio increment. They follow shader data dependencies
in a single sample, not GPU lane or fragment scheduling. Source breakpoints select mapped operations; helper
expansions retain the originating document and call context. Graph selection and
source selection resolve through the same IDs. Failed evaluation reports the
shader operation and mapped source, rather than exposing stale/partial values as
a successful snapshot. Initial SDK CPU inspection is headless; Studio consumes
the shared inspection API after the source/graph contract is proved.

## CPU/GPU equivalence limits

Both backends execute the same validated float-lane operations and uniform
values, but floating point is tolerance-based. libm, GPU approximations, fused
operations, rounding, denormals and finite-difference thresholds can differ.
Tests select tolerances for observable operations. Snapshots preserve nonfinite
intermediates and flag each value and the snapshot, without misreporting an
arithmetic overflow as a missing-resource failure.
Do not claim bit identity or full-frame equivalence from a single sample.

CPU textures use resident top-to-bottom straight-alpha RGBA8 base-level pixels,
explicit sRGB/linear conversion, nearest/bilinear filtering and repeat/clamp.
The current GPU image path builds mip chains and may use anisotropy and
`textureGrad`. Minification and implicit footprint/LOD cannot match a CPU base
sample in general. Inspect this distinction explicitly. Missing image data is an
evaluation diagnostic. General sampler objects, arbitrary GPU formats and
readbacks are outside the initial source subset.

CPU normal/bump/triplanar detail uses the existing explicit coordinate gradients
and finite differences. These are not hardware fragment quad derivatives.
Arbitrary dFdx/dFdy/fwidth and implicit LOD require a defined neighborhood and
must be rejected by the initial source pass. Star Field has a fixed CPU angular
footprint and a GPU fwidth footprint; label its preview approximate. Rasterization,
lighting, blending, MSAA, discard and divergent lane execution are outside a
single material sample. Preserve existing validated derivative nesting limits.

## Stages and acceptance

1. Record/review this design and obtain clean dependency checkpoints and file
   reservations. Independently audit existing IR, CPU/GPU paths and Studio flows.
2. Add a narrow SDK source-map/CPU snapshot API over the existing evaluator.
   Prove source ID/range lookup, shared expressions, stale revisions, failure
   mapping, parameter/sample changes and numeric values with C/pico_unit cases.
   Cover two expansions of one helper, multiple auxiliary nodes for one source
   result, aliases emitting no new node and flagged nonfinite intermediates.
3. Agree compiler typed-fact transport with its owner. Implement the shader-domain
   validation/lowering in Chibi and source diagnostics. Prove ordinary functions,
   locals, vectors and imported helper calls with positive and rejection cases.
   Keep compiler bootstrap changes with the compiler owner.
4. On the typed asset checkpoint, connect material definition, source compiler,
   derived graph and existing render catalog. Render the fixture, compare CPU
   samples and GPU pixels at controlled coordinates and tolerances, and prove
   source edits update graph/backend/debug products together. Exercise a second
   material with different content and no dependency on the motivating scene.
5. Expose source/graph/CPU inspection in Studio through shared SDK APIs. Verify
   source navigation, dirty edits, error ranges, undo/redo and invalid-source
   recovery with the owning editor conventions. Extend source operations and
   libraries incrementally with the same checks and documented limits.

Before publishing each behavior increment, independently review affected source
and contracts and run appropriate focused cases plus required repository groups:
`make test-native`, `make test-materials`, `make test-projects`,
`make test-language-all`, `make test-tools`, and applicable build/format checks.
GPU checks require an actual supported backend. Report elapsed time and unrun
coverage; there is no arbitrary global test-duration ceiling. Preserve explicit
holds, source self-build proof on the three pinned platforms, and dirty primary
work. PRs carry exact tested commits and evidence, not historical test claims.

## Current checkpoint and next dependency

Stage 2 is implemented in `src/runtime/shaders/source_debug.[ch]`. It owns source
documents, origin-only call chains, node values and principal results; validates
the complete current document closure before returning values; preserves the last
snapshot on capture failure; and retains/flags nonfinite intermediates. Source
storage is bounded at 256 documents, 512 KiB each and 8 MiB total, with 4096-byte
identities. SDK and Chibi build-graph registration use the same sources/headers.

Independent source/contract review found and resolved missing origin-only call
provenance, the Decal Mask gradient flag and unbounded source copies. The twelve
new C/pico_unit cases pass with 144 assertions. The tooling group passes with
116 cases / 27,281 assertions in 54.311 seconds, excluding configuration/build.
The required `make test-native`, `make test-materials`, `make test-projects` and
`make test-language-all` groups pass in 25.605, 14.678, 85.544 and 23.382 seconds
respectively, excluding configuration/build. The focused agent route passes,
and the runner integration cases pass with 15 cases / 165 assertions in 32.695
seconds after the new command mapping was added. Native Debug and Release
products built through the Chibi graph. Evidence is retained in
`.chibi/shader-source/`, including build logs, focused checks and required groups.
The repository-wide `make check-format` passes in 112.7 seconds.
These are CPU snapshot
checks with supplied IR/source maps; they do not establish source compilation,
material rendering, GPU parity or Studio interaction coverage.

Stage 3 depends on agreed typed-fact transport. The compiler owner confirms
there is no existing public typed expression table, and will own its semantic
export. [typed-facts.md](typed-facts.md) records a representative gradient shader
and the proposed stable facts, avoiding private compiler records and prepared
bytecode reconstruction. Stages 4–5 additionally depend on the reviewed typed
asset checkpoint. No shared compiler, asset, bootstrap or Studio edits yet.

### Review follow-up: sampled SDF failure diagnostics

The integration review reproduced a valid resident 2x2x2 atlas being reported as
missing when upstream `FLT_MAX * 2` produced a nonfinite coordinate. The shared
evaluator combined resolver and volume-evaluation failure in one diagnostic.

`shader_volume_eval` now accepts an optional `shader_error *` and distinguishes
invalid metadata/atlas, nonfinite coordinates and nonfinite evaluated distance.
The shader evaluator reports actual resolver failure separately and forwards
volume diagnostics with the originating source ID. Successful calculations are
unchanged. No alternate evaluator or per-sample full-atlas validation is added.

The regression first failed with the original missing-image message. It now
checks coordinate overflow, finite-coordinate distance overflow, actual resolver
failure, invalid resident atlas, preservation of the previous snapshot and
recovery. The focused source-debug selector passes 13 cases / 165 assertions.
The direct sampled-model caller also checks its metadata diagnostic from a
cleared error value. Independent source/contract review found no remaining
defect. Follow-up evidence is retained under `.chibi/shader-source/sdf-*`.
The five required Make groups passed again. Materials, projects and language
took 18.460, 69.532 and 7.679 seconds; tooling passed 130 cases / 27,374 assertions
in 87.158 seconds. The final native run after the fresh-error assertion passed
in 50.825 seconds. Configuration/build time is excluded. `make check-format`
passed in 70.0 seconds. Source and binary fingerprints are recorded in
`.chibi/shader-source/sdf-evidence.json`.

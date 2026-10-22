# Declarative SDF modeling plan

> Historical implementation record. Commands, MCP recipes, helper scripts and
> validation results below describe the recorded revision. The MCP product and
> its authoring helpers are retired. Current authoring uses saved `.chibi`
> files and [native checks](runtime-authoring.md); see [testing](testing.md) for
> admitted checks. These results do not verify the current checkout.

Status: implementation is in the isolated declarative-modeling worktree. See [the implemented contract](declarative-sdf-modeling.md) and [validation evidence](declarative-sdf-modeling-validation.md). The original design and scope below remain the implementation reference.

## Summary

Add a declarative modeling layer that preserves named parts, parameters, and construction operations in our existing compiled Chibi graph format. Compile those definitions into the existing bounded SDF rendering pipeline. Blender-compatible Python and MCP interfaces are explicitly outside this plan.

## Context

The SDF experiments are in `<local-evidence>/c-blueprints`, inspected at HEAD `631f9a710bfccbf2b0843017304322d71798a46d` with additional working changes possible. Implementation must establish its own current source baseline and coordinate with that work before integrating dependencies.

The current system already stores shapes, materials, functions, parameters, prototypes, and scenes as compiled Chibi graphs. The lab authors sphere, rounded-box, torus, Boolean, and animated examples through the existing public graph operations. The runtime compiles shape graphs into shader code and evaluates their distance outputs inside bounded proxies.

Current shape edges carry scalar/vector samples, not reusable geometry values. A modeling operation must preserve an unevaluated shape definition to transform or instantiate a complete part. The new layer must also keep authored construction information rather than replacing the saved asset with flattened arithmetic.

## Scope

The deliverable is editable procedural SDF assets in Studio and compiled Chibi, with deterministic compilation, parameterized reuse, conservative bounds, material assignment, diagnostics, and focused validation scenes. Existing Chibi graph authoring and inspection tools remain the authoring interface for automation.

Excluded: Blender API emulation; a new Python SDK or MCP server; exact Blender modifier behavior; arbitrary face/edge editing; mesh reconstruction into primitive graphs; generative model installation; character skinning; cloth/hair simulation; mesh export; new GI techniques; and the parent task's renderer optimization work.

## Existing code and constraints

Paths below are relative to the implementation checkout. Features under `projects/sdf_lab`, `SdfSurface3D`, and the SDF material shader currently depend on the SDF worktree; their presence is not a claim that they are integrated into main.

| Existing path | What it supplies |
| --- | --- |
| `src/graph/model/behavior_asset.h` | Asset domains, named functions, variables, and graph ownership |
| `src/graph/model/graph.h`, `pin.h`, `type.h` | Stable graph/node identities, typed edges, nominal types, and reflected values |
| `src/graph/document/serialize*.c` | Saved graph structure, references, and values |
| `src/support/asset_document.c`, `asset_value.c` | compiled Chibi parsing and common document values |
| `src/graph/shader/shader_adapter.c` | Scalar/vector graph lowering and function expansion |
| `src/graph/shader/shader_graph.c` | CPU evaluation and shader generation from the shader IR |
| `src/render/shader_catalog.c` | Compiled assets, parameter binding, revisions, and shader source |
| `src/render/mesh_surface.c`, `mesh_material.c`, `shaders/sdf_material.frag` | Bounded SDF drawing with mesh depth and existing materials |
| `src/automation/graph_edit.c`, `controller.c`, `render_preview.c` | Generic graph edits, asset validation, discovery, and previews |
| `projects/sdf_lab/author.mjs` | Existing construction of SDF graphs through those tools |

The existing shape graph returns one distance. The inspected shader IR permits 32 parameter slots and 1,024 nodes; these are current limits, not targets to raise automatically. Shape asset bounds are two-dimensional, while `Geometry3D` currently supplies the three-dimensional rendering proxy. The new model contract needs explicit three-dimensional bounds ownership.

## Recommended architecture

Introduce `domain "model"` and a `content/models` asset family using the existing graph document and compiled Chibi serialization. Keep numerical `shape` graphs useful as field programs. A model graph describes construction: it connects geometry definitions, parameters, and named parts.

```text
compiled Chibi model graph, parameters, and referenced model assets
    -> validated construction DAG and dependency graph
    -> compiled distance program, bounds program, and part/material mapping
    -> existing shader catalog and bounded SDF renderer
```

Use one authored source of truth. Generated shader IR, bounds, source mappings, and cache entries are derived products. Do not save a second generated shape compiled Chibi file that users can edit independently.

Add capability modules under `src/graph/modeling/`, separate from the existing graph data structures in `src/graph/model/`. The modeling compiler traverses construction nodes with an explicit position context and emits existing shader IR wherever possible. Add low-level shader operations only when an operation cannot be expressed efficiently by the current IR; CPU and GPU implementations must agree.

A new model domain is preferable to embedding semantic markers in arbitrary shader formulas. A float wire such as `length(p) - radius` is a distance already sampled at `p`; a geometry wire must remain evaluable at any position. This distinction enables transforms, repeated references, bounds derivation, and part identity without guessing intent from arithmetic.

### Geometry and identity

- Introduce compiler-only nominal geometry types, initially `Geometry3D`, then `Profile2D`. Use the graph's existing nominal type mechanism, with explicit validation. Geometry connections resolve to construction nodes, never ECS handles or serialized process pointers. Unconnected required geometry inputs are errors.
- Reuse stable node/function IDs and asset references. A named Part node labels a geometry subtree. A part instance is identified by its asset, reference-instance path, and part node ID. Renaming a part changes its label, not its identity.
- Reusing one definition does not duplicate its authored source. A transformed instance can still increase shader evaluation work; source sharing is not a runtime performance guarantee.
- Functions and referenced models are acyclic. Report cycles and missing dependencies at the authored reference. Do not introduce an additional general scripting interpreter.

### Parameters and numerical contracts

- Reuse typed scalar/vector parameters and pure arithmetic for dimensions and transforms. Distinguish structural parameters, such as array count, from runtime uniform parameters, such as radius. Structural edits recompile; uniform changes reuse code where the dependency contract permits.
- Public parameters have stable identities, readable names, defaults, and declared valid ranges. Reject invalid overrides with a source diagnostic. Include parameter identity and declaration changes in dependency invalidation.
- Geometry is evaluated in local project units: X/Z horizontal and Y vertical. Use radians for model rotations to match runtime transforms. Fix and document rotation order with an asymmetric test object. Adapt to existing shader nodes that use degrees internally.
- Support positive uniform and nonuniform scaling. Nonuniform scaling requires a conservative distance bound, not a claim of exact Euclidean distance. Reject zero or negative scale; provide an explicit reflection operation for mirroring.
- Each supported node defines its surface, conservative step-distance rule, bounds rule, parameter validity, and empty-result behavior. Arbitrary mathematical deformation and arbitrary shader-expression-to-geometry conversion are outside the initial contract.
- Determinism means stable construction, IDs, compilation, and evaluation within stated numeric tolerances. Do not promise bitwise identical GPU images across backends.

### Bounds and runtime ownership

Derive a local 3D AABB from the construction DAG. Bounds use the same validated parameter snapshot as distance evaluation. Union encloses both children; subtraction remains within its left operand; intersection can produce an empty bound; transforms enclose the transformed child bounds. Smooth union expands its bound by a proven envelope for the chosen formula.

Evaluate the bounds program when relevant uniform parameters change, not per ray step. If a parameter source cannot provide a finite conservative bound, reject that model path until an explicit finite contract exists. Preview sampling is a validation aid, not proof that bounds are conservative.

For model-backed SDF objects, compiled model bounds are authoritative. Integrate a local bound center and half extents into the proxy and inverse transform; off-center objects must work. Preserve the existing manually bounded raw shape path for its separate purpose. Do not silently reinterpret `Geometry3D.width/height/depth` as model scale; model dimensions belong to parameters and instance scale belongs to visual transforms.

Existing `Shader.shape` may reference a compiled model through the common shape catalog. Extend reference discovery and pickers to accept both asset families. Reject ambiguous name collisions and update references on rename through the existing reference index. No second entity geometry binding is required.

### Materials and parts

Start with the existing entity-wide material. Add material slots on named model parts after geometry and bounds work. Slots reference existing Material assets; referenced models expose slot overrides without copying the definitions.

Trace only distance during the march. Resolve the winning part and material at the surface hit using the same construction tree and parameter snapshot. This avoids running every material shader at every distance evaluation.

For the first material contract, hard union/intersection choose the active operand with a stable tie-break. Subtraction inherits the left operand's surface material, including the cut surface. Smooth union uses the dominant operand's material; blended material transitions are a later extension. These rules must be visible in node descriptions and tested. Parameter names are scoped per referenced material and flattened into bounded compiled slots, with explicit collision/limit diagnostics.

### Loading, editing, and failure

Load and link dependencies, validate the graph, derive bounds, compile the program, then publish the complete generation atomically. Runtime readers use immutable compiled generations; edits do not mutate the generation currently being drawn.

Track referenced asset revisions in cache keys. Parameter-only changes invalidate parameter/bounds data without recompiling unchanged geometry. Geometry or material contract changes invalidate dependent programs. Cache lowering results by construction identity, transform context, and arguments; caching only by node ID would incorrectly merge distinct instances.

On an invalid edit, keep the authored edit available for correction. Studio may retain the last successful preview but must label it stale and show the current diagnostic. Save retains editable source; validation and packaging must reject an invalid current generation. Undo, reload, successful retry, asset deletion, and project close must release or invalidate derived state through existing ownership paths.

## Implementation stages and scene gates

Implement each stage as a reviewable change with its own scene and evidence. Do not start a later stage to hide an unresolved earlier failure. Scene names below are proposed. Use a new `projects/modeling_lab` project so the active SDF optimization fixtures remain undisturbed.

### 1. Model asset and first primitive

Register the model domain, typed geometry connections, Model Output, Sphere, and literal parameters. Implement the construction-to-shader lowering path and minimal computed bounds. Add load/save, validation, graph creation, and scene binding through existing catalog conventions. Create assets with the current generic Chibi authoring tools rather than inventing a new modeling API.

Scene: `ModelPrimitiveStudy`, with model spheres and independent analytical reference shapes at matched poses.

Acceptance: create in Studio or the generic graph tool, save, reload, compile, and render. Verify distance samples, depth, silhouettes, stable IDs, missing-input diagnostics, and preservation of unrelated assets. This establishes the complete vertical path before expanding the operation set.

### 2. Solid operations and transforms

Add Box, Rounded Box, capped Cylinder, Capsule, Torus, Translate, Rotate, Scale, Reflect, Union, Intersection, Difference, and Smooth Union. Define dimensions unambiguously, including whether rounding changes outer dimensions. Implement operator-specific bounds and conservative distance rules.

Scenes: `ModelBooleanStudy` and `ModelTransformStudy`. Include nested cuts, an empty intersection, asymmetric rotations, off-center geometry, a mirrored part, nonuniform scaling, and a smooth join.

Acceptance: independent analytical/reference comparisons; finite outputs; correct inside/outside signs; no clipped surfaces at bounds; no overshooting of the reference surfaces under supported transformations. Nonuniform scaling tests must verify conservative stepping as well as matching the zero surface. Invalid radii, invalid scale, and excessive node expansion produce source diagnostics.

### 3. Named parts, reuse, and variants

Add named Part nodes, geometry functions, referenced model instances, parameter overrides, finite linear/radial arrays, and stable instance paths. Array counts are bounded structural inputs. Reuse the existing graph/reference machinery for rename, dependency lookup, and signature updates.

Scene: `ModelReuseStudy`, assembling repeated engine-like modules and a mechanically unrelated shelving fixture from shared definitions. Give instances different valid dimensions and transforms.

Acceptance: a shared-definition edit reaches every dependent instance; a local override changes only its instance; rename preserves references; deleting a referenced definition gives a precise error; a reference cycle is rejected. Save/reload and copy/paste preserve identity correctly without accidental cross-asset references. Arrays report expanded work before exceeding compiler limits.

### 4. Bounds and parameter changes

Complete bounds evaluation for the entire operation set and integrate the local bound center into preview, culling, proxy creation, depth, and instance preparation. Expose supported runtime parameter overrides through the existing parameter mechanism. Define a separate structural-change path.

Scene: `ModelBoundsStudy`, with dimensions changing through their declared ranges, parts extending beyond the origin, camera movement, and an overlapping mesh control. Drive changes through existing behavior capabilities where supported; add only a shared parameter setter if that interface has a verified gap.

Acceptance: no clipping, stale proxy, stale parameter upload, or unnecessary shader recompile on uniform-only edits. Check agreement between rendered geometry, bounds, selection framing, and offscreen culling. Invalid overrides fail before reaching GPU work. Repeated edits and project reload do not retain obsolete generations.

### 5. Profiles and reusable solid construction

Add `Profile2D` with circle, rectangle, and a finite simple closed convex polygon; then centered linear extrusion and full revolution around a documented axis. Validate polygon winding, degeneracy, self-intersection, and valid radial coordinates. Treat vertex count and profile structure as structural data. This supplies useful wings, brackets, housings, and lathed parts without introducing mesh face editing.

Scene: `ModelProfileStudy`, containing an extruded wing, a bracket, and a revolved vessel. Include a profile edge near the viewing direction to expose depth and normal errors.

Acceptance: analytical section comparisons and an independently constructed mesh control, valid surface normals, conservative bounds, and precise rejection of unsupported/open profiles. Arbitrary curve sweeps, lofts, and concave profiles remain explicitly deferred rather than silently approximated.

### 6. Part materials and surface identity

Implement the material-slot and winning-part rules above. Integrate existing material graphs, image resources, and parameter scoping with compiled model evaluation. Provide object/part coordinates consistently for procedural materials; UV unwrapping is not part of this milestone.

Scene: `ModelMaterialStudy`, with painted body, metallic inserts, an emissive part, a cut, and a smooth join. Reuse one component with different slot overrides.

Acceptance: material assignments survive transformations, reuse, and save/reload. Verify cut and overlap ownership, tie-breaks, procedural coordinate stability, normals, and depth. A material edit updates shading without rebuilding unchanged geometry. Measure the extra hit evaluation and shader-resource use; reject unsupported resource counts explicitly.

### 7. Complete the authoring workflow

Expose the new domain in existing Studio asset creation, graph search, typed pins, Properties, dependency navigation, diagnostics, and previews. Extend existing generic Chibi graph tools to see the same contracts; add no Blender-compatible endpoint, Python package, or separate modeling tool protocol. Include orbitable scene previews and parameter controls through existing capabilities.

Follow `docs/editor-ui-references.md`, `docs/editor-graph-references.md`, the UX laws skill, and `.claude/skills/chibi-graph-legibility/SKILL.md`. Reuse the Studio kit and automatic graph layout. Geometry sockets must read as geometry, numeric parameters use numeric editors, and material/model references use typed asset pickers. Preserve mathematical expressions inside implementation functions while showing meaningful operations in authored models.

Scenes: `ModelShipStudy` and `ModelPropStudy`. The ship is a static visual study with editable hull, wings, and engines; the prop uses different content and no combat or encounter mechanics.

Acceptance: create a model, name a part, expose a dimension, reuse a component, assign materials, change an override, undo, save, close, reload, and preview using the real editor. Validate the same assets through generic Chibi tools. Capture and inspect both graph and surface views. Test the editor workflow in native and Web builds.

### 8. Packaging and bounded cost

Package model source and dependencies through existing project mechanisms. Make static program sharing, parameter updates, and cache invalidation observable with focused work counters. Preserve the SDF renderer's existing batching and LOD contracts; add shared integration fixes only when the new model path exposes a concrete regression.

Scene: `ModelCostStudy`, comparing one asset, many identical instances, parameter variants, and a deliberately complex construction at matched screen coverage. Include the raw shape equivalent of a representative model to isolate compiler overhead.

Acceptance: stable canonical save/load; deterministic dependency resolution; successful packaged launch; no rebuild for rigid instance motion; no duplicate compile for identical model generations; explicit diagnostics at existing limits. Report source node count, expanded shader node count, compile time, program count, parameter/bounds updates, draw work, and matched frame timing separately.

The 4080 Super is available for asset generation only and is not a game performance target. Record the actual test device/backend and workload. Do not claim target performance or choose a universal frame budget until target requirements exist. The test scenes establish correctness and relative cost without adding full-game complexity.

## Files expected to change

| Area | Planned files and responsibility |
| --- | --- |
| Construction model | New `src/graph/modeling/model_nodes.*`, `model_ir.*`, `model_compile.*`, `model_bounds.*`; operation contracts, semantic compilation, source mapping, and conservative bounds |
| Asset model and persistence | `src/graph/model/behavior_asset.*`, nominal type registration, `src/graph/document/serialize*.c`, `library.c`; model domain, persistent part/reference/parameter metadata, and round trips |
| Asset discovery | `src/assets/new_asset.*`, `behavior_loader.c`, `reference_index.*`, `asset_source.*`; creation, loading, dependency navigation, and compiled Chibi intelligence |
| Publication lifecycle | `src/runtime/behaviors/live.c`, `src/render/shader_catalog.*`; validated compiled generations, bounds/parameter revisions, material dependencies, and cleanup |
| SDF integration | `src/render/mesh_surface.*`, `mesh_material.*`, `scene_renderer.c`, `shaders/sdf_material.frag`; model bounds, off-center proxies, material resolution, and batching compatibility |
| Compiler target | `src/graph/shader/shader_graph.*`, `shader_adapter.c`; narrow target operations or entry points needed by model lowering, preserving numerical shape graphs |
| Existing tooling | `src/automation/controller.c`, `graph_edit.c`, `render_preview.c`, `src/editor/graph/search.c`, relevant Studio/workspace files; expose one shared model contract |
| Tests and fixtures | New focused model document/compiler/bounds tests, extensions to `test/shader_graph.c`, `test/content_catalog.c`, SDF renderer tests, editor tests, and `projects/modeling_lab` |
| Documentation and builds | Graph/model contract documentation, `docs/mcp.md`, source/test registration, and package asset discovery |

These are responsibility boundaries and starting paths, not a requirement to create empty modules or modify every listed file. Keep all implementation logic in C. Factor a module only when its responsibility exists.

## Migration and scope controls

- Integrate the required SDF capability from the active work only after recording its exact source state and preserving unrelated changes. This plan does not authorize merging or editing that worktree now.
- Model assets are an additional capability. Raw numerical shapes, mesh assets, and behavior graphs remain supported for their distinct uses; do not label them obsolete merely because models exist.
- If an existing saved contract must change, migrate the affected assets directly and remove the superseded contract. Bump `CHIBI_PROJECT_VERSION` and every relevant `project.chibi` when older builds can no longer load those projects. The model domain itself needs a version review; do not silently write it under an incompatible project version.
- Retain authored graphs as the editable source after optimization. Do not add mesh-to-SDF baking, runtime geometry scripts, skeletal animation, a replacement editor, or sampled-volume rendering to solve an early milestone.
- No performance promise follows from compact compiled Chibi. Keep source reuse, compiler expansion, field evaluations, and frame cost separate.

## Verification and completion

For every stage, retain its scene, validation result, fixed camera/parameter settings, and inspected images. Numerical checks must use independent expectations or references, not merely repeat the lowering implementation. Visual output alone does not establish distance safety, bounds correctness, or speed.

Run the affected focused tests while developing, then `make build` and `make test` for a completed integration. Run `make web` and the native/Web authoring checks for editor and shared compiler changes. After hand-editing compiled Chibi, run `build/chibi_ctl --project-root <root> --format`, then its check mode. Inspect packaged asset discovery and launch separately from a successful engine build. Avoid rebuilding the active parent worktree for this plan.

Completion means both final study assets can be constructed, revised by named parts and parameters, saved, reloaded, validated, packaged, and rendered through the supported operations. References and material assignments survive edits; bounds remain conservative; invalid input is diagnosed; repeated instances retain expected program sharing. The staged evidence must identify any unsupported backend or unverified workflow rather than treating it as passed.

The implementation and verification record is maintained separately in the linked validation document.

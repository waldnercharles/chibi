# Game creation interface

I would make every authoring capability available through one shared SDK, with a
CLI for agents and an editor for people. Both should edit the same readable
sources, report the same errors and produce comparable evidence. The useful unit
is a complete workflow: create an asset, inspect it, change it, validate it, try it
in a scene and explain the result.

This document separates the implementation in the asset-authoring candidate from
recommendations for later work. Source references describe available contracts;
they do not establish that a build, editor interaction or device test passed.

## What this change provides

| Capability | CLI/controller | Studio | Current limit |
| --- | --- | --- | --- |
| Discover asset grammar | `schema [KIND]`, reflected field descriptions | Shared KDL completion and hover metadata | Structural schemas declare completeness; loaders still own semantic constraints. |
| Find assets and resources | `assets`, `resources` | Existing asset browser | Resource inventory/consumer/cost presentation needs a dedicated editor view. |
| Validate and repair | `validate [PATH]`, structured source diagnostics | Shared source-language assistance | Precise source ranges cover supported structural errors; generic compiler/load errors can remain. |
| Create a material | `material create`, richer `create_material` JSON operation | New Material form with presets, colors, surface factors and alpha settings | CLI shorthand exposes a subset of the JSON fields. |
| Inspect a shader | `inspect PATH`, `inspect PATH --sample` | Rendered preview, editable authored parameters and collapsed Diagnostics with a CPU sample | CPU outputs are values before lighting; static costs are estimates. Rendered appearance and platform verification require separate evidence. |
| Import and reimport models | `model plan`, `model import --recipe`, `model reimport --check` | Existing import entry points | Saved-recipe review/reimport UI remains follow-up work. |
| Preserve review evidence | `--evidence` around direct/NDJSON commands and rendered previews | Export evidence for the current material/shape preview, including a linked snapshot, PNG, report and replay request | General scenario evidence review, rerun and comparison UI remains follow-up work. |

The owning contracts are [asset schemas](../src/assets/asset_schema.h),
[resource inventory](../src/authoring/resource_catalog.h),
[material creation](../src/authoring/material_create.h),
[shader reports](../src/authoring/shader_report.h),
[import recipes](../src/assets/model_import_recipe.h) and the
[CLI adapters](../tools/chibi_ctl/commands.c). Studio consumes the material and
shader SDK modules directly.

## A material workflow today

These are implemented command spellings. Replace `/path/to/project` with a
project and use the controller built from the same engine candidate as Studio.
Evidence directories must be new and outside project content.

```sh
build/bin/chibi_ctl --project-root /path/to/project schema material
build/bin/chibi_ctl --project-root /path/to/project resources
build/bin/chibi_ctl --project-root /path/to/project material create Steel --preset metal --roughness 0.25
build/bin/chibi_ctl --project-root /path/to/project inspect content/materials/Steel.kdl --sample
build/bin/chibi_ctl --project-root /path/to/project --evidence /tmp/steel-validation validate content/materials/Steel.kdl
```

For a bounded rendered preview, save this request as `steel-preview.json`:

```json
{"operation":"preview_shader","path":"content/materials/Steel.kdl","width":320,"height":180,"time":0}
```

```sh
build/bin/chibi_ctl --project-root /path/to/project --evidence /tmp/steel-preview --render-request steel-preview.json
```

The preview runs through the existing native hidden-window renderer and its
execution policy. Its response contains the PNG and actual backend/video-driver
metadata; the evidence directory also contains the decoded PNG for inspection.
Full verification is not implied. Studio's New Material form creates an
editable graph through the same generator. Material Properties shows a retained
rendered preview and typed parameter defaults. Drag the preview to orbit, scroll
to zoom, or edit the view and fixed time under Preview settings. Diagnostics
contains static costs and an explicit CPU sample. Export evidence saves a
portable linked snapshot, verified frozen image dependencies, the rendered PNG,
sample results and a CLI replay request. See [material authoring](material-authoring.md).

Model iteration has separate review and publication steps:

```sh
build/bin/chibi_ctl --project-root /path/to/project model plan models/robot.glb --name Robot
build/bin/chibi_ctl --project-root /path/to/project model import models/robot.glb --name Robot --recipe
build/bin/chibi_ctl --project-root /path/to/project model reimport Robot --check
build/bin/chibi_ctl --project-root /path/to/project model reimport Robot
```

Saved imports require sources inside the project. Plans report geometry,
dependencies, warnings and proposed output actions. Reimport preserves edited
material/prototype files in full, deferring source updates to those files. It
rejects conflicts rather than guessing how to merge authored changes. Publication
rechecks snapshots, stages files and attempts rollback on reported failure.
Restoration errors name retained originals and other residual files. If publication
commits but cleanup fails, the response still reports `applied: true` alongside
the error; do not retry that plan. This is not crash-atomic, and callers must
coordinate concurrent writers.

## Make changes explicit and reversible

Keep KDL and executable `.chibi` sources authoritative. Graph layout, selection,
camera position and tool panels are authoring state; they must not secretly change
game behavior. Generated files should identify their recipe, generator version,
inputs and ownership. Editing a generated output should offer a deliberate choice
to change its recipe, preserve an override or detach it.

My next shared operation would be an asset edit transaction. It would accept a
bounded set of edits and expected source hashes, return a readable diff, validate
in memory and publish only when every precondition holds. Studio would turn one
brush stroke or inspector edit into one undoable command. The CLI would return
the same change description and an inverse patch. Failure should leave the prior
source intact and identify any recoverable backup explicitly.

Separate preparation from publication for expensive operations. Plans should
state which declarations and files change, dependencies, estimated work, warnings
and conflicts. A human should see these facts in an ordinary review dialog with
clickable source locations. Agents should receive stable error codes, property
paths and related declarations, with enough context to make a precise repair.

## Meshes, SDFs and procedural content

Chibi already has editable construction graphs, bounded mesh baking and
provenance. [Generative model authoring](generative-model-authoring.md) describes
profiles, extrusion, revolution, sweeps, attachment frames and material mapping.
[Shader graphs](shader-graphs.md) distinguishes 2D shape distance functions from
3D geometry and materials. Extend these systems instead of introducing another
incompatible asset format.

I would add discoverable construction recipes with named dimensions, parts,
attachment points, coordinate systems and material slots. A mesh operation should
report bounds, triangles, normals, material assignments and collision/export
choices. Useful validation includes winding, degenerates, finite coordinates,
negative-scale behavior and bounded silhouette comparisons from saved cameras.
Studio needs primitive handles, profile/curve editing, snapping, part isolation,
section cuts and a clear source of each selected generated part.

SDF tools should expose explicit bounds, sign convention, units, transform rules
and sampling budgets. Provide named point/line/grid probes, zero-surface previews,
normal checks and a bake comparison. Distinguish approximate sampled residuals
from guaranteed error bounds. Humans need the same probes as viewport overlays
and inspector controls, including a visible indication of clipped bounds.

Procedural generation should save a seed, algorithm identifier/version, parameter
snapshot, stable element IDs and work limits. Seed alone is insufficient when a
random-number generator, iteration order or algorithm changes. Each generator
should declare whether it promises identical bytes, equivalent geometry or only
a deterministic result within a specified platform/build. Keep generated output
separate from hand-authored adjustments. Regeneration should preview its diff.

Start with small useful generators: room layouts, terrain heightfields, scatter
placement and prop assemblies. Report sampled heights, occupied regions,
connectivity, spawn clearance and budget use. Studio should offer seed controls,
locked instances, regeneration previews and an explanation of why a placement was
accepted or rejected.

The following spellings are proposals, not implemented commands:

```text
chibi_ctl mesh build recipe.kdl --plan
chibi_ctl sdf probe shape.kdl --points probes.kdl
chibi_ctl generate level.kdl --seed 42 --plan
chibi_ctl asset apply changes.kdl --expect-source-hash HASH
```

## Material and shader tools next

Continue the material workflow first. Add editable CPU probe contexts, saved
sample grids, parameter sweeps, image-dependency diagnostics and preview comparison.
Use one shared report for CLI and Studio. Offer node search by purpose, typed
connections, named outputs, source navigation and compiler diagnostics attached to
the offending node. [Blender's shader-node organization](https://docs.blender.org/manual/en/4.4/render/shader_nodes/index.html)
is a useful interface reference, not a requirement to reproduce its full system.

A shader should expose parameters with names, types, defaults, units and intended
ranges. Reports should distinguish IR size, texture calls, helper evaluations,
compile time, GPU time and raymarch work. They are different measurements. Keep
raw shader editing available where supported, with explicit backend compatibility
and equivalent error/source mapping. Do not invent CPU samples for unsupported
GPU operations.

## Tilemap editing should be a complete tool

The [current tilemap runtime](../src/runtime/tilemap.h) parses cells, resolves
frames and supports spatial queries. That does not establish an editor comparable
to Tiled, Ogmo Editor or LDtk. I would make tilemap editing the next complete
CLI/editor workflow after materials.

Begin with an orthogonal map document, stable tileset/tile IDs, named layers,
explicit cell dimensions and a shared mutation API. Deliver brush, erase, fill,
rectangle, stamp, selection, copy/paste, flip/rotate, layer visibility/locking,
entity placement, collision overlays, typed properties, undo/redo and playtest
from the current camera or selected spawn. Show missing tiles and invalid entity
references where they occur. One drag stroke should be one undo step.

Then add semantic integer-grid layers and deterministic autotiling. Keep gameplay
meaning separate from chosen artwork so repainting a wall does not change its
collision accidentally. Explain which rule produced an automatic tile, let users
preview rule coverage and save the rule seed/version. Tiled's
[tileset collision and metadata tools](https://docs.mapeditor.org/en/latest/manual/editing-tilesets/),
[layer/object model](https://doc.mapeditor.org/en/latest/manual/layers/) and LDtk's
[IntGrid layers](https://ldtk.io/docs/general/intgrid-layers/) provide concrete
references for these choices.

Interop should start with an explicitly supported orthogonal subset. Preserve
external IDs and source provenance; report unsupported orientations, tile flags,
object types or rule systems before writing. LDtk's
[layer-instance schema](https://ldtk.io/docs/game-dev/json-overview/layer-instances/)
is a useful import contract. Add round-trip export only for fields whose fidelity
is tested. Importing rendered autotile results is a smaller commitment than
translating another editor's entire rule system.

## Evidence and efficient verification

Current bundles save requests/responses, invocation and replay arguments, engine
identity, accessible executable FNV-1a hashes, exact bounded project manifests and
bounded content inventories before/after, and an explicit replay environment allowlist. FNV hashes detect ordinary drift; they
are not cryptographic authenticity proofs. Replaying mutating requests requires
restoring original inputs. A bundle is not a project backup. External imports,
per-request alternate roots and inventory-limit exclusions remain explicit limits.

Keep structural validation, CPU shader samples, CPU simulation and GPU-rendered
images separate. A preview can claim rendered coverage only with a successful
native response, generated image, dimensions and backend metadata. An image is
not a performance benchmark. Cross-backend pixels need stated tolerances; fixed
simulation inputs do not prove cross-platform floating-point identity.

Reuse existing bounded scenarios and record/replay for behavior checks. Save the
initial state, frame count, fixed timestep, input events, RNG state and selected
assertions. Prefer invariants such as expected entity counts or collision outcomes
to brittle whole-image comparisons. Stop at the first actionable failure.

Broader evidence UI should let a person rerun a saved check, inspect source diffs,
open failed assertions and compare captures. Cache checks by source/dependency,
build, parameters and backend identities. Run static checks before CPU probes and
bounded renders. Honor the [60-second policy](testing.md#temporary-60-second-policy);
an unexecuted or quarantined check remains unverified.

## Delivery order

1. Finish and validate the material/shader creation, inspection, sampling and
   evidence workflow in both interfaces.
2. Add shared edit transactions, recipe/reimport review and general scenario
   evidence review in Studio. Extend failure preservation and undo coverage with
   small fixtures.
3. Deliver basic tilemap authoring with the shared operation model, then integer
   grids, autotiling and the first restricted importer.
4. Expand construction and procedural tools using the same recipes, probes,
   budgets and review bundles. Require a second, different game example before
   treating a motivating game's convenience as an engine-wide contract.

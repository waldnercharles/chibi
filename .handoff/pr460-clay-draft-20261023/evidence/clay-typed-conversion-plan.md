# PR 460 clay fixture: direct typed-source conversion plan

This is source research only. No fixture has been imported or converted in an
engine checkout, and no build, test or GPU command was run for this plan.
The renderer repair remains staged at tree
`adabdc085fa2d78f7f7a9977e0adeec1a353853f` on fa1.

The original content is pinned to published PR head
`aa7b4fcabe62fb91ee90d8e80800724a9c30e6e4`. Exact `git show` copies and SHA256s
are in `clay-source-aa7/` and `clay-source-aa7-manifest.json` beside this file.
The conversion contract is the newer clean typed-assets migration checkpoint
`31098151bd5c0f3042f74996d678c0b784f3f25f`, rather than historical a504.
Contract sources and SHA256s are in `typed-reference-310/` and its manifest.
The optional decoded-module cache `30cd287d1b86250d13b42c3716a94d747fa87a9b`
is separate and is not a prerequisite for writing clay assets.

## Contract to use

Each declarative asset is an ordinary Chibi source file:

```chibi
package asset

import data "game:assets"

pub Build :: () -> byte[] {
    return data.Encode(data.Object([
        data.Field.Named("version", data.Value.Int(33)),
        data.Field.Named("name", data.Value.Text("Clay Material Lab")),
        data.Field.Named("content", data.Value.Text("content")),
        data.Field.Named("start_scene", data.Value.Text("scenes/MaterialLab.chibi"))
    ]));
}
```

This is the complete planned `project.chibi`. The old `project.kdl` is removed;
there is no additional manifest or loader. All eight KDL paths below are renamed
directly to `.chibi`, and their authored references change in the same patch.

Use `data.Field.Named`, `data.Object`, `data.List` and explicit `data.Value`
variants throughout. The approved project version is `Int(33)`. Scene version
is `UInt(1)`. Graph version is `UInt(3)`; IDs, counters, reference IDs, port IDs,
expression modes and any serialized index fields use `UInt`. Real coordinates,
distances, scalar parameters and reflected float fields remain `Real`, even
when their value is mathematically integral. Boolean and text identities remain
explicit. Do not flatten these distinctions through JSON text or infer them
from their formatted spelling.

At checkpoint 310, `Assets.Encode` carries numeric kinds in its private
`CHIBI-ASSET1` transport. Authored assets call `Encode`; they do not reproduce
that transport or add compatibility parsing. `Schemas.Scene` and `Schemas.Graph`
now emit UInt versions, and InputMap/Scenario integer fields were corrected too.
The signed-version findings from a504 are historical and superseded. Project
readers accept signed or unsigned integers (`av_is_int` accepts both), so old
UInt(33) examples were not a project-reader failure. The clay manifest still uses
the approved Int(33) contract.

Source references: `packages/game/assets/Assets.chibi`,
`packages/game/assets/Schemas.chibi`, `tools/compiler/asset_source/Write.chibi`;
native consumers `src/assets/project.c`, `src/assets/scene_document.c`,
`src/graph/document/serialize.c` and `src/support/asset_value.c`.

## Declarative paths and data

All paths in this table are beneath `test/fixtures/clay-lab/` in the engine.

| Old path -> new path | Exact data to retain or change |
| --- | --- |
| `project.kdl` -> `project.chibi` | Four fields in the example above; version 26 becomes current 33 and start scene extension changes. Name and content root stay the same. |
| `content/input.kdl` -> `content/input.chibi` | Root object keys `Form`, `Fingerprints`, `Variation`, `Oiliness`, `Reset`. Each value is an object with `sources: List(Text(key))`, with keys F, G, V, O, R respectively. No controller slot or firing flag is needed; preserve their absent/default behavior. |
| `content/materials/ClaySurface.kdl` -> `.chibi` | Graph object: version UInt(3), name ClaySurface, domain material, id_counter UInt(2000), original description and graphs. Retain all 10 event nodes, 10 edges and two function graph records; details below. |
| `content/models/ClaySphere.kdl` -> `.chibi` | Graph version UInt(3), domain model, id_counter UInt(1000), original description. Node100 Model Sphere with Radius Real(18); node200 Model Output; edge 100.Geometry -> 200.Geometry. |
| `content/models/ClayCapsule.kdl` -> `.chibi` | Same metadata shape, name ClayCapsule. Node100 Model Capsule with Radius Real(12) and Half Segment Real(12); node200 Model Output; one Geometry edge. |
| `content/models/ClayBlock.kdl` -> `.chibi` | Same metadata shape, name ClayBlock. Node100 Model Rounded Box: Half Size default object x Real(18)/y Real(16)/z Real(16) and Radius Real(4); node200 Model Output; one Geometry edge. |
| `content/models/ClaySculpt.kdl` -> `.chibi` | Graph version UInt(3), domain model, id_counter UInt(2000), original description. Retain 11 event nodes, 11 edges and two expression function records; details below. |
| `content/scenes/MaterialLab.kdl` -> `.chibi` | Root version UInt(1), name MaterialLab, ordered entities list. Preserve 11 entities: View, Stage, Key, four Neutral specimens, four Terracotta specimens. Prototype names are unchanged. Components retain all explicit overlays and booleans. |

`MaterialLab` keeps the original planar HRC/SDF presentation; do not turn it
into a scene3d fixture. View/Stage/Key name ClayView/ClayStage/ClayLamp. Within
each specimen row, x positions are Real -108/-36/36/108; y is Real -30 for
Neutral and Real(34) for Terracotta. The first three specimens override Shader
shape to ClaySphere/ClayCapsule/ClayBlock and ClaySettings.has_form to false.
The sculpt inherits ClaySculpt and has_form=true. There is no authored index
field in the old scene: preserve list order instead of adding one. If current
canonical serialization emits an index field, retain its UInt identity.

Every graph keeps the existing data schema: root `graphs` is a list; each event
graph has kind/name/nodes/edges; each node has UInt id, Text type, original Real
layout coordinates and its original named inputs/outputs/reference fields.
Inputs/outputs are objects, including nested pin metadata and default objects.
Edges keep Text `from`/`to` pin references in their original order. Function
records keep UInt id/entry_id/expression_mode and Text math_expression. Do not
replace authored formulas with native shader implementations.

`ClaySurface` node IDs and contents:

- 100 ObjectPosition; 200 BaseColor Shader Parameter with ShaderVec3 default
  Real(0.46, 0.43, 0.38); 300 Roughness ref1/default Real(0.68); 400 Variation
  ref2/default Real(0.06); 500 Fingerprints ref3/default Real(1); 600 Spacing
  ref4/default Real(2.4).
- 700/800 Behavior Expression keep their exact formula text, port IDs701-703
  and801-804, float/ShaderVec3 type metadata and inputs. 900 Shader Bump keeps
  Distance Real(0.12). 1000 Shader Surface Output keeps Alpha Mode UInt(1).
- Function1100/entry1101 keeps the fingerprint height formula; function1200/
  entry1201 keeps the roughness formula, both expression_mode UInt(1). All ten
  connections remain exactly as published. Fingerprints affect bump height;
  Variation affects roughness; neither changes pigment or model geometry.

`ClaySculpt` keeps nodes100 rounded box; 200 Form parameter with Minimum Real(0)
and Maximum Real(1); 300 expression `vec3(14 * form, 3 * form, 2 * form)` with
ports301/302; 400 sphere Radius Real(12); 500 Translate; 600 Smooth Union with
Smoothness Real(3); 700 expression `vec3(-7, 38 - 15 * form, 6)` with ports701/702;
800 sphere Radius Real(10); 900 Translate; 1000 Difference; 1100 Output.
Function1200/entry1201 and1300/entry1301 retain those formulas. Form=0 retains
the calibration block behavior, and Form=1 retains the worked silhouette.

## Prototypes and executable sources

Convert the five existing `content/prototypes/*.chibi` files in place from old
prototype declarations to package asset/Build sources. Root object fields are
Text `name`, Object `components`, optional List(Text) `includes` and
List(Text) `behaviors`. Component values are Objects of reflected fields.

| Path | Components and exact authored values |
| --- | --- |
| `ClayLamp.chibi` | Transform x0/y-76; Appearance width220/height6/relief72 as Real and color UInt(4294967295); Material scene/albedo/emission_color UInt(4294967295), emission_intensity/emission_multiplier Real(1). |
| `ClayNeutral.chibi` | Transform x/y Real(0); Input move object x/y Real(0); Geometry3D width/height/depth Real(36) and elevation Real(24); VisualTransform scale Real(1); SdfSurface3D enabled Bool(true); Material scene/albedo UInt(4294967295) and edge_light Real(1); Shader material Text("ClaySurface") and shape Text("ClaySculpt"); ClaySettings form/fingerprints Real(1), roughness Real(0.68), variation Real(0.06), spacing Real(2.4), has_form/dirty Bool(true); behaviors List(Text("ClayControls")). |
| `ClayStage.chibi` | EngineSystem asset Text("ApplyClay"). HRC2D keeps resolution Real(0.5), adaptive false, target_fps Real(60), ambient Real(0.3), intensity/falloff Real(1), shadow_floor Real(0.03), radiance_hysteresis/bounce_hysteresis Real(1), feedback false, surface_radius Real(24), surface_mode Text("Depth thickness"), surface_depth_radius Real(320), surface_depth_gain Real(1), surface_depth_quality Text("Balanced"), surface_depth_visibility Text("Bounded segments"), surface_thickness Real(24), surface_relief Real(1.5), surface_bevel/specular Real(1). target_fps is a reflected float in src/runtime/components.h:1342 at checkpoint 310. |
| `ClayTerracotta.chibi` | Root includes List(Text("ClayNeutral")); only Shader.parameters is overridden. Its value is Text containing a complete compiled asset source, described below. Do not duplicate the Neutral defaults or attach a second ClayControls. |
| `ClayView.chibi` | Transform x/y Real(0); Camera width Real(320), height Real(180), pitch Real(0.35), target_elevation Real(12); SurfaceQuality resolution Text("Full"), sdf_normal_mode Text("Tetrahedral"), sdf_pixel_error Real(0.25). |

`Shader.parameters` remains a native reflected string, but the string's content
must be ordinary compiled asset source. Replace the old
`(array)material.BaseColor 0.52 0.16 0.08` KDL string with this complete source,
quoted as `data.Value.Text` by the outer prototype Build:

```chibi
package asset

import data "game:assets"

pub Build :: () -> byte[] {
    return data.Encode(data.Object([
        data.Field.Named("material.BaseColor", data.List([
            data.Value.Real(0.52),
            data.Value.Real(0.16),
            data.Value.Real(0.08)
        ]))
    ]));
}
```

Existing typed fixture examples use exactly this Text-of-source contract for
Shader.parameters. This is not a legacy string parser or another asset file.
The scoped parameter name remains `material.BaseColor`.

Keep `content/components/ClaySettings.chibi` executable declaration source.
Preserve its ranges/units and bool fields; do not convert it to a data Build.
Keep `content/behaviors/ClayControls.chibi` and
`content/systems/ApplyClay.chibi` executable sources too. Adapt them to the
current explicit event syntax (`on Form :: ()`, and the other four actions;
`ApplyClay :: system(settings: ClaySettings) on Tick :: ()`). Input-map actions
still declare events by name (`src/authoring/source_catalog.c:934-960`), so no
polling redesign is needed. Replace old `cast<float>(settings.field)` with the
current numeric constructor spelling `float(settings.field)`, as existing
fixture scripts do. Preserve per-press toggles, oiliness0.5/0.68, Reset restoring
all five defaults, has_form gating only Form, and dirty=false after publication.
Keep five settings values floating point and distinct from bool fields.

The original aa7 ApplyClay body already uses the correct exported names:
`set_shape_parameter(self, "Form", ...)` inside the has_form guard and
`set_material_parameter(self, ...)` for Fingerprints, Roughness, Variation and
Spacing. Keep those exact names and parameter names. The renderer native
capability pack exposes `set_material_parameter` and `set_shape_parameter` in
`src/render/shader_parameters.c` at checkpoint 310, with generic float inputs
in its header. No `set_shader_float` or `set_model_float` aliases exist; do not
introduce them. Keep finite scalar float values and the existing `material.`
and `shape.` scoping contract.

The material owner reviewed this source contract: complete game:assets Build
text in native string fields, numeric transport, finite scalar values, scoped
names, formulas, ports, parameter IDs and these setter names are consistent.
The owner's pending source-debug work does not change those contracts. This
is source review only; it supplies no build or visual acceptance and must be
rechecked on the final combined checkpoint. No new native action, package API,
query ABI or compatibility alias is proposed.

## Test and wiring patch

Retain `test/tools/clay_fields.c` as five C/pico field-contract cases through
the existing graph dispatcher. It uses real model compilation and independent
analytic/bracket references; it does not depend on the old asset syntax.
Review its calls against the final model checkpoint before carrying it over.

Adapt `test/tools/clay_lab.c` to the current typed project and real prepared
resource host. Preserve one project, one bytecode player, 8 specimens/11 scene
objects, 26 simulation steps, five settings, pigment distinction, held-key
non-repetition, Reset of edited spacing, dirty-state checks, no model recompiles
and 24 model setter validations. The real shader/model publication assertions
remain; a fake CPU shader host would weaken the check. Set preparation's
existing `.read_only = true`; use project.chibi version 33 and no migration path.
Recount the converted source and actual resources before freezing limits. The
old checked-in fixture has 18 files/24,615 bytes, before compiled-source
expansion. Keep legitimate file/resource caps, but update measured bounds if
conversion requires it and explain why. Do not recreate global time admission.

Use current CMake and Graph ownership, not an old whole-file transplant:

- Add clay_fields.c beside the current model graph test sources in
  `test/tools/CMakeLists.txt` and `tools/graph/engine/Targets.chibi`'s test_tools
  file list; register --clay-fields in graph_dispatch and its existing
  CHIBI_TEST_MODEL_GRAPHS path.
- Add clay_lab.c to the current source-features consumer, whose render,
  project-preparation, graph-catalog and native-host dependencies already match
  this check. Mirror the source in Graph's chibi_source_features_consumer list.
  Prefer its existing feature gate/dispatcher ownership over restoring the old
  direct test_tools source/link block and separate CHIBI_TEST_CLAY_LAB gate.
- Add both selectors to execution_selectors.h, tools/agent.py CPU mappings,
  tools/test_policy.py TOOLS routes and the current test-policy route assertions.
  Add concise current inventory documentation. Choose native/projects group
  placement from actual resource ownership; clay-lab prepares a disk project
  and therefore belongs with project checks rather than the CPU-only native
  inventory. Preserve all current fa1 selectors and the renderer --draw-uniforms.
- Include the new project in `test/tools/fixture_format.c`'s fixed project list
  after the owner reviews actual formatting cost. Use the existing typed
  fixture-format check instead of restoring clay-format/clay-format-check aliases.
- Update clay fixture README, preview.json only if its current owner contract
  requires a changed path, and clay-sdf docs for .chibi paths/current commands.
  Preserve renderer/visual holds and scene3d limitations; remove obsolete global
  time/admission wording, not real operation/resource requirements.

Omit the old PR's sdk/dependency_options.cmake, sdk/desktop.cmake warning wrapper,
cmake/tests/desktop_dependencies/warnings fixtures and corresponding
desktop_dependencies.c test. Current fa1 desktop consumes prebuilt vendor
archives; that source-building warning wrapper is obsolete and build owners
own the current vendor route. Omit unrelated designated-initializer cleanup in
test/authoring/reference_index.c and test/tools/material_surface_mesh.c,
terrain_checkpoint.c, terrain_seams.c, terrain_volume.c. Do not restore the old
test_policy.py NATIVE description table, shared wall-clock cap, workload
admission/quarantine, or stale successful-check claims. Reuse only the narrow
current selector/route additions needed for these tests.

## Remaining owner checkpoints

1. Root supplies a clean reviewed integration of current main/compiler and the
   typed migration 310. a504 constructor findings are already fixed; no current
   constructor defect remains established by this research. The optional cache
   can stay separate. Actual conversion remains reserved until that checkpoint.
2. Material/shader owner has confirmed the checkpoint 310 source contract above;
   recheck it on the final combined base. That confirmation is not a build or
   visual pass. HRC2D.target_fps is already confirmed float on checkpoint 310,
   so it is Real(60) in the plan. Do not touch owner serializers,
   compiler resources or SDK build files to make a stale fixture appear valid.
3. After conversion, independently review the full source and both build
   inventories, then run the focused field/player/canonicality checks when
   resources are released. Broader exact-candidate requirements and Graph/CMake
   parity stay with the coordinator. No previous CPU result proves the converted
   fixture passes or the prepared compiler/SDK combination is valid.
4. Renderer ordinary-draw acceptance on the current source, genuine
   Linux/glibc sanitizer checks, matching immutable dependency archives/pins and
   actual clay visual acceptance remain separate outstanding obligations. This
   inventory makes no whole-PR readiness or merge claim.

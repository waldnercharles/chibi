# A 3D arena game and the tools to build it

Tracked in [the game and editor milestone](https://github.com/waldnercharles/c-blueprints/issues/28), with child issues [M1](https://github.com/waldnercharles/c-blueprints/issues/29), [M2](https://github.com/waldnercharles/c-blueprints/issues/30), [M3](https://github.com/waldnercharles/c-blueprints/issues/31), [M4](https://github.com/waldnercharles/c-blueprints/issues/32), [M5](https://github.com/waldnercharles/c-blueprints/issues/33), [M6](https://github.com/waldnercharles/c-blueprints/issues/34), and [M7](https://github.com/waldnercharles/c-blueprints/issues/35).

## Active goal, September 9, 2026

Deliver one playable, visually deliberate arena through this editor. Keep the
existing movement, independent aim, single weapon, pursuit, damage, death and
restart loop. Use a fixed arena. Additional weapons, dash/strike, shifting arenas,
upgrades and bosses are outside this first iteration. The older full-game
milestones below remain follow-on ideas, not prerequisites for this slice.

Use [the latest reference review](research/arena-reactive-slice.md) for visual
acceptance. The supplied Sektori frames establish a dark triangular floor,
action-driven emissive triangle outlines, faceted silhouettes and a clear event palette. They do
not establish its renderer implementation. Keep the theme provisional. Color,
shape and motion must communicate the interaction before decorative detail.
The intended style mixes 3D actors with deliberate 2D graphic effects, taking
Arcane as an aesthetic reference. Treat the player as the center of attention:
announce threats before they arrive, show where actions originate, and make
feedback explain what just happened. There is no gameplay HUD in this iteration.

The current pass uses rasterized polygons and Surface materials. Evaluate bounded
SDF geometry later against the same actor and scene, after the authored polygon
version is attractive and the frame cost has been measured.

### First playable visual iteration

- [x] One authored scene launches in editor Play and standalone Play, with held
  mouse/Space firing, reliable focus, defeat/completion and restart. No HUD.
- [x] A permanent outer triangular lattice surrounds smaller breathing triangle
  outlines. Only lines emit; cyan, rose and amber reactions stay local and subtle.
  The fixed arena boundary follows complete lattice edges without clipped cells.
- [x] The short ship is a forward-pointing pyramid with filled NeonSwarm Chevron
  faces. Aim-relative strafing banks it gently; reversing lifts the nose. Its engine stays at
  the rendered nozzle and leaves a coherent fading tail.
- [x] Cube shots tumble around varied axes and leave shrinking 2D triangle trails.
- [x] Hollow cube-frame enemies stay smaller than the player, tumble independently
  and visibly bump each other. The renderer and death behavior consume the same
  canonical frame parts, including source pose, scale and material.
- [x] Enemy formations are announced at their actual arrival positions before
  becoming dangerous. Alternate edge rows, arcs and pincers; give the player
  readable space and time to respond. Wave floor pulses reinforce those arrivals.
- [x] Small, hollow 2D XP triangles begin at random rotations, breathe and spin
  softly on the beat, then shrink away when uncollected. Restrained curved trails
  trace collection into the player. Arrival credits once; final collection
  finishes before encounter clear.
- [x] Movement, firing, damage and death produce connected, readable feedback.
  Useful controls live in components, materials, prototypes and behavior graphs,
  with persistence and preview paths.
- [ ] Actual rest, movement, hit/death and crowded-play captures are reviewed at
  normal size and with bloom disabled. A new light/effect can be edited, saved,
  reopened and played through the editor.
- [ ] Record frame-time distributions in a representative combat scene on the
  current Mac and check the supported browser path. Target 1920x1080 at 60 FPS,
  p95 <=16.7 ms and p99 <=20 ms after warm-up. These remain targets until measured.

The shared camera stays 10 degrees from vertical with a 35-degree vertical field
of view. Judge the game in standalone Play as well as the editor. Completion
requires visible quality and the authoring loop; passing renderer tests alone
cannot complete this gate. GitHub #28 tracks this scoped first iteration and #32
tracks visual acceptance.

## Technical direction

- Use the existing Cute 3D renderer for meshes, instancing, depth and camera
  transforms. Keep gameplay on an explicit arena plane initially. Spatial
  visuals, colliders and simulation state have separate ECS components.
- Add familiar surface and effect graphs with textures, procedural patterns,
  ramps, mapping, math, reusable groups, and useful previews. Share the graph
  model, history, search and parameter system already in the editor.
- Use Box2D for planar contacts, shape casts and controlled movement. Evaluate
  Box3D for elevation and moving 3D bodies when a game feature needs them.
  Do not make a new SDF rigid-body solver a prerequisite for the game.
- Use meshes for repeated opaque geometry and bounded SDF rendering for
  authored blends/deformations. Compare a whole-scene SDF experiment against
  the same crowded scene before expanding its role.
- Choose lighting by comparative images, temporal stability and frame cost.
  Direct lights, shadows, HDR emission and bloom establish the baseline.
  Evaluate 3D radiance cascades against that baseline and a bounded probe-based
  alternative. The current 2D cascades are not 3D GI. `three-rc` informs the
  experiment; its implementation is not a dependency.
- Read component data in ECS queries and batch rendering by primitive/material.
  Keep damage, weapons, encounter rules and upgrades visible in behavior graphs.
  Add general engine capabilities where repeated graph work exposes a missing
  feature. Avoid native nodes that conceal entire game-specific behaviors.

## Broader milestones and acceptance

The following broader backlog predates the scoped first iteration above. Dash,
expanded game structure and SDF/GI work are deferred. Each milestone has an issue,
an editable example, tests for its real behavior,
and a captured play session or rendered comparison. Checkboxes mean the complete
acceptance gate passed, not that a subset of code exists.

### 1. Playable 3D foundation

- [x] A saved Arena3D scene renders true solids, depth and normal-based lighting
  using reflected 3D geometry. Perspective selection, dragging, fit,
  resizing and prototype placement agree with the rendered image.
- [ ] Move and aim independently with mouse/keyboard and controller; held fire
  follows the authored cooldown, including tapping, focus loss, pause and restart.
  A bounded encounter includes enemy pursuit, damage, death and restart.
- [x] Verify the renderer with overlapping solids and a repeated-actor workload.
  Keep a current limitation list: initial direct lighting is a foundation, not
  the required visual finish. Native shader graphs must later drive this path.

### 2. Materials a designer can use

- [x] Add a Surface output with Base Color, Emission, Roughness, Metallic, Normal
  and Opacity, plus Unlit. Preview and scene use the same compiled material.
  UV, object/world position, normal, view direction and time are typed inputs.
- [x] Support image sampling, linear/sRGB intent, wrap/filter controls, mapping,
  gradients, Noise/fBM, Voronoi, color ramps with editable stops, color mixing,
  remap, smooth thresholds, vector operations and Fresnel. Keep math available
  without requiring a pile of arithmetic nodes for common effects.
- [ ] Support local node groups with published inputs, inline defaults, color and
  ramp controls, context search, thumbnails/live previews and source-linked
  diagnostics. An artist can make a moving energy surface, dissolve and impact
  ring from these nodes without editing C or generated shader code.
- [ ] Preserve valid rendering during invalid graph edits; Undo and save/reload
  restore graphs and per-instance parameters. Migrate supplied materials directly
  as contracts change. File-format import is deferred.

### 3. Robust combat and simulation

The [fixed-step physics notes](research/arena-fixed-step-physics.md) record
the implementation, ownership boundaries and remaining acceptance gates.

- [x] Introduce shared 60 Hz simulation, renderer interpolation and Box2D
  configured for 32 project units per meter. Fast projectiles cross thin targets
  safely; contacts survive destruction and slot reuse. Collider authoring supports
  Body Type, Circle/Box geometry and World filters with Undo/Redo/save/reload.
- [ ] Validate dash against thin/rotated walls, oblique surfaces and corners.
- [ ] Add dash/strike, pursuer, ranged and armored enemies with readable tells,
  player damage feedback, defeat, restart and encounter completion. Preserve
  behavior inspection and editing during play.
- [ ] Validate at 30/60/120 display FPS, controller disconnect, pause and reload.
  Document the Box3D trial boundary instead of building an unused physics wrapper.

### 4. Visual showcase and effects authoring

- [ ] Build a representative arena composition with elevated geometry, animated
  patterned floor, emissive rim, distinctive faceted enemies, depth-aware trails,
  sparks, shockwaves and impact deformation. It must look deliberate at rest,
  in motion and during the densest combat.
- [x] Add HDR targets, exposure/tone mapping and controllable bloom through
  a reflected Environment component, with scene/preview parity and native/Web
  verification.
- [ ] Add shadows, distortion and graded color. Keep the player and dangerous
  projectiles legible with the showcase effects enabled. Reduced-motion/effect
  settings remain useful.
- [ ] Expose effect spawning and parameters through reusable ECS components and
  behavior nodes. Effects are assets with previews and instance overrides.
  Review reference and in-engine captures side by side after every visual pass.

### 5. Run structure and content

- [ ] Complete score, combo, rewards, upgrade choices and loadout progression;
  choices materially alter weapons, movement or defense. Add sound/music,
  spatial effects and controlled camera feedback.
- [ ] Author reusable encounter patterns, safe arena transformations and
  multi-stage bosses. Build a complete run from start/menu through completion
  or defeat, with restart, settings, saving and a packaged playable build.
- [ ] Use seeded encounter variation for regression/replay tests; verify balance,
  target readability and frame behavior through sustained crowded play.

### 6. SDF geometry and lighting experiments

- [ ] Add one bounded 3D SDF graph asset with blends, cuts, extrusion and
  deformation. Its proxy ray trace writes correct hit depth and normals and
  interacts correctly with opaque geometry. Compare visual quality and cost
  against a mesh version. Add bounds/step diagnostics to the authoring preview.
- [ ] Compare direct lighting, 3D radiance cascades and another bounded indirect
  lighting approach in the same showcase. Test moving/offscreen emitters,
  occluders, thin walls, camera changes and dense combat. Record artifacts and
  pass timing separately. Choose the default from evidence, not the name of the
  algorithm. Keep a documented rejection or adoption decision.

### 7. Production editor, performance and release

- [ ] Finish 3D camera/object manipulation, surface/effect/geometry asset
  workflows, contextual search, diagnostics, content dependency updates and
  reliable play/stop/reload. Author a new enemy, weapon and effect through the
  editor as an end-to-end usability test.
- [ ] Benchmark a deterministic dense case, initially targeting 200 enemies,
  2,000 projectiles and 10,000 transient particle instances. These are load
  targets to measure, not verified capacities. Record frame-time distributions,
  memory, draw calls, simulation time and GPU pass costs.
- [ ] Default development target is the current Mac at 1920x1080, 60 FPS,
  p95 frame time <=16.7 ms and p99 <=20 ms after warm-up. Confirm minimum
  hardware through measurements. Maintain a real browser rendering check for
  supported features; record platform differences explicitly.
- [ ] Validate the complete game and authoring loop, packaged assets, input,
  audio, settings, persistence, sustained play and visual quality. The full
  objective stays open until these gates and the preceding milestones pass.

## Review and delegation

Start each slice with a concrete image or playable behavior. Delegate by ownership:
renderer/material backend, gameplay/ECS and authored assets, editor/workflow,
then an independent review. Integrate at narrow APIs and run affected tests before
the complete suite. Avoid simultaneous build mutations in the shared build tree.

After every gameplay milestone, review behaviors for misleading names, unused
functions/state, duplicate calculations and overly wide event graphs. Extract
coherent functions where names improve comprehension; keep short flows directly
visible. Review ECS query patterns, state lifetimes and unnecessary per-entity
work. Remove hacks by extending the appropriate node, editor or engine facility.
Verify gameplay before and after cleanup, including suspended/latent functions.

Run the same review when a mechanic introduces a repeated workaround. In each
review, check that exposed settings have consumers, shared functions receive
their variable choices through inputs, and input systems cannot overwrite AI
state. Measure readability by whether a designer can follow a decision and
change its outcome. Reducing the number of visible nodes is not a goal by itself.
Use a separate reviewer where possible and record concrete findings in the
milestone issue. Cleanup stays with the affected feature and its verification.

Routine verification uses `make build`, `make test`, focused native editor/render
checks and `git diff --check`. Screenshots and recordings are intentional review
artifacts in `/tmp` or a named review directory, never generated every dev frame.
Update the relevant issue with evidence and remaining work. Do not close a broad
milestone from a narrow green test.

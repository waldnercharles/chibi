# M3: fixed-step planar physics

Implementation notes, 2026-09-09. The first part of [M3](../arena-roadmap.md#3-robust-combat-and-simulation) now has a shared fixed clock, Box2D bodies, swept projectiles, renderer interpolation and collider authoring. Native and Web verification passed for this foundation. Dash/strike, enemy variants, defeat and encounter completion remain open.

## Before this increment

- [Engine](../../src/runtime/engine.c) ran one variable-delta simulation update per display frame. [Prototype preview](../../src/runtime/scenes/prototype_preview.c) duplicated that loop; [scene runtime](../../src/editor/workspace/scene_runtime.c) separately accumulated material/preview time. Fixing physics alone would have left graph movement and timers dependent on display cadence.
- Contacts used circle overlap at final positions, without collision response or continuous collision. ArenaBoundsBehavior clamped player coordinates; visible boundary geometry had no physical authority.
- Box2D **3.1.1 is already available** through the [pinned Cute dependency](../../CMakeLists.txt). Cute compiles its C17 sources into the library and exports headers/symbols for static, shared and Web builds. Use its existing `<box2d/box2d.h>` API; do not add a second dependency. Evidence: [pinned Cute CMake](https://github.com/RandyGaul/cute_framework/blob/a13f4661f5ba27e8d45027b52f121c1704786425/CMakeLists.txt), also inspected in `build/_deps/cute_framework-src`.
- [Queued events](../../src/runtime/behaviors/entities.c) own scalar payloads and skip destroy-queued targets, but reject struct/array payloads. Box2D result pointers cannot be borrowed into that queue.

## Implementation

### Shared simulation clock and explicit actions

The engine owns one **60 Hz simulation clock**, with zero or several ticks followed by one render per display frame. Prototype preview delegates to the same engine. Behavior waits, particles, deformation and material animation use simulation time. Rendering interpolates previous/current solved positions without modifying authoritative ECS transforms. Authoring and paused Step show the current position; teleports bypass stale interpolation.

Box2D steps at 1/60 second with four substeps. A display frame completes at most eight simulation ticks. Incoming elapsed time is capped at 250 ms; the remaining accumulator is also bounded. Excess time is reported through `engine_frame_timing.dropped_seconds`. A 100 ms stall retains its six ticks. Pause excludes wall time and clears accumulated time; Step advances exactly one tick. Stop/restart/reload clear input, clock and physics state. [Box2D simulation guidance](https://box2d.org/documentation/md_simulation.html)

Movement and aim samples persist across catch-up ticks. Fire/Dash press and release transitions queue until a tick consumes them, including taps entirely between ticks. Only entities with Input receive local actions; losing control sends one zero movement sample. Unknown action names are rejected. Directed authored events remain available to AI and other entities.

Spawn initialization can wait on simulation time while ordinary Tick/physics is held. Debugger pauses freeze that clock. The pipeline retains its current phase across a pause so resuming a contact handler cannot step physics twice.

### Body and contact ownership

Each live world owns one Box2D world, full-generation ECS/body mappings and a private broad-phase tree for swept motion. Component removal, queued destruction, slot reuse, configuration edits and authored teleports are reconciled at explicit tick boundaries. Box2D IDs never enter authored components.

Bodies are zero-gravity and fixed-rotation. Dynamic player/enemy bodies block the physical boundary. Collider masks control mutual blocking: players and enemies block each other; supplied enemies do not block other enemies. Static walls use Box geometry. Visual primitives and colliders remain independent.

Continuous sensors use swept Circle/Box queries because Box2D sensors do not provide continuous collision detection. Sweeps use relative motion against conservative candidate bounds, handle initial overlap, select the nearest hit, respect both masks and owner exclusion, and deduplicate contacts. Non-sensor continuous bodies use Box2D bullet CCD. The native module has no weapon, damage or destruction rules. [Box2D shape definitions](https://github.com/erincatto/box2d/blob/v3.1.1/include/box2d/types.h), [cast APIs](https://github.com/erincatto/box2d/blob/v3.1.1/include/box2d/collision.h)

Tick order is: finish initializers; consume input and pending events; run authored Tick/continuations; apply commands; synchronize bodies and simulate; queue Contact Begin; dispatch reactions and flush destruction. The reaction pump permits eight isolated event batches before carrying excess into the next tick. `event_backlog` makes that carry observable. Damage and impact effects remain visible in ProjectileHitBehavior; a wall hit destroys the projectile without requiring a Health component.

### Units and migration

The implementation uses **32 project units per meter**, seconds and radians. Box2D's existing `b2SetLengthUnitsPerMeter(32)` configures its length tolerances before any world/default initialization. The same authored coordinates feed physics, rendering, shaders and picking directly. The public Box2D manual labels custom units experimental; the bundled 3.1.1 implementation and focused collision tests are the authority for this integration.

This replaces the handoff's proposed 1/32 migration of every visual asset. The arena remains 640×360 project units (20×11.25 m); player speed 180 units/s is 5.625 m/s. Gravity is zero. We deliberately author density/friction in the physics module; unit configuration does not automatically scale them.

Movement graphs now set `Velocity`; `Body` selects Static, Dynamic or Kinematic and continuous motion. `Collider` selects Circle/Box, category/mask, sensor and a typed runtime owner exclusion. Visual dimensions are independent. Decorative inner rails use their own visual prototype; physical boundary walls have static box colliders. Owner handles save as null and are supplied by the shared Fire Projectile graph at runtime. Projectiles spawn at the shooter origin so a muzzle offset cannot bypass a thin wall.

`Projectile.direction_x/y`, graph position integration and ArenaBoundsBehavior were removed from supplied content. Projectile velocity controls flight and facing. Enemy pursuit computes arrival speed in its graph and clears velocity when its target disappears.

## Implementation ownership

| Owner | Boundary |
| --- | --- |
| Simulation | Engine, preview loop reuse, scene clocks, action queue, timing/pause tests. |
| Physics | Live-world lifetime, Box2D mappings/casts, contact replacement, headless collision tests. Agree on tick order with simulation owner first. |
| Authoring/content | Coordinate one component-schema change; collider/body controls, assets and shared graph migration. Keep important gameplay decisions visible at graph roots. |
| Integrator | Renderer interpolation and unit-sensitive views, build/test wiring, native/dev/Web checks. Assign generated-schema ownership before parallel edits. |

## Acceptance gates

1. Replay identical tick-stamped input at **30/60/120 display FPS**: equal tick/shot/damage counts, cooldown/Delay timing and final poses within stated tolerance. Include unequal deltas, a 100 ms stall, a multi-second stall, zero-tick frames and brief button transitions.
2. A fast projectile crosses a thin target with both endpoints outside it: exactly one impact/damage. Verify nearest target, filters/owner exclusion, initial overlap, moving targets and cast/contact deduplication.
3. Dash into thin/rotated walls, oblique surfaces and corners at every display rate: no escape or growing penetration. Define recovery for an initially overlapping spawn.
4. Destroy targets/projectiles during contact delivery, remove bodies live and reuse ECS slots: no stale dispatch, invalid Box2D access or repeated damage under ASan/UBSan.
5. Pause, Step, focus loss, controller disconnect, Stop, preview looping and reload clear appropriate input/physics state. Preserve player/AI ownership and directed AI events.
6. Real collider/body editing, Undo/Redo and save/reload reproduce collision and framing. NeonSwarm and ShaderDemo remain usable; verify native static, native dev reload and Web.

## Verification of this foundation

- `make test` passed under the native ASan/UBSan build, including generation, reflection, reload/checkpoint checks and the complete headless suite. `make build` and `make web` passed.
- `build/test_suite --simulation` passed scene/preview movement and Delay comparisons at 30/60/120/144 display Hz, stalls, queued taps, exact Step, debugger resume, interpolation and bounded same-tick reactions. Position tolerance is 0.00001 project units in the cadence fixture. Floating authored cooldowns are still quantized to fixed ticks; their deadline tests account for float rounding.
- Fast-shot regressions use a 4800-unit/s projectile and a 2-unit wall 14 units from the shooter. Physics tests also cover moving targets, initial overlap, nearest-hit filtering, Circle/Box sweeps, removal, stale generations and invalid geometry. Nonlethal and simultaneous lethal damage/destruction finish within one fixed tick.
- Native `--arena-ui` passed actual Body/Collider controls, dimension visibility, World masks, typed owner display, Undo/Redo/save/reload, real wall blocking, aim/fire, pause and standalone restart. `--render-checks` passed Surface, shader images/effects, 3D geometry, picking and existing 2D rendering.
- Arena3D and NeonSwarm each ran 180 fixed display frames without diagnostics. The actual Arena3D Web build rendered and supported restart, mouse aim/fire, Escape pause and Enter resume without console errors or warnings. The standalone native capture remains nearly top-down; this pass changes simulation rather than the camera or art direction.

The full M3 gates above remain open where they require dash, enemy variants, physical controller disconnect, a dev-reload play session or crowded combat/performance comparison. This increment does not establish those results.

## Encounter outcomes remaining in M3

[ArenaWave](../../content/behaviors/ArenaWaveBehavior.json) stops after 40 spawns but never detects cleared enemies; Elapsed continues increasing. Its player-life guard is Input existence. [HealthBehavior](../../content/behaviors/HealthBehavior.json) destroys at zero without an outcome signal. [Standalone presentation](../../src/editor/workspace/presentation.c) has pause/restart/quit but no victory/defeat. Restart already restores authored state.

Recommend an authored Active/Cleared/Defeated component readable by presentation, plus explicit enemy membership. For one encounter, existing Has Entities With Component can answer whether enemies remain; scoped membership can follow when multiple encounters require it. Keep wave/death transitions authored, stop elapsed accumulation after completion, and release input on the resulting outcome. End is a lifecycle event, not necessarily death. Presentation must not infer outcomes from prototype names or duplicate health rules.

The [earlier physics research](arena-gameplay-physics.md) supplies rationale, but its dependency and failed-video-access notes predate the bundled engines and later [successful visual review](arena-visual-direction.md). The vendored 3.1.1 headers are the implementation authority; the verified public manual identifies itself as 3.1.0.

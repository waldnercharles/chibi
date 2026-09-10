# Arena gameplay and physics direction

Research checked 2026-09-08. Recommendations below are design choices for this project, not claims about the reference games' internal implementations.

## What the references establish

Sektori's developer describes short, demanding twin-stick runs, changing arenas, varied enemies, evolving bosses, ships and upgrade decks. Its waves, levels and bosses are handcrafted pieces that are combined and randomized. The stated aesthetic draws on fast, saturated techno music. This supports an encounter authoring system with reusable patterns and controlled variation. It does not establish a need for fully procedural levels or a particular rendering technique. [Official Sektori site](https://www.gimblll.com/sektori/)

Waves 2 explicitly links chained kills to a combo, adrenaline and a powerful burst attack. It also offers configurable loadouts and enemies that grow more dangerous during a run. These are useful examples of rewarding skilled aggression through a readable resource loop. [Developer's Steam description](https://store.steampowered.com/app/217120/Waves_2_Notorious/)

The Waves 2 developer separately documents floor deformation as a graphical effect, camera shake after explosions, a dash utility, and camera tuning for aim, walls and player tracking. Its 2019 notes also report a performance improvement after switching Unreal's deferred renderer to its forward renderer. Those observations favor testing the desired image and control response directly instead of choosing the most elaborate renderer first. [Developer update history](https://store.steampowered.com/news/?appgroupname=Waves%C2%B2&appids=217120&feed=steam_community_announcements), [2019 camera and rendering notes](https://store.steampowered.com/news/posts/?appids=217120&enddate=1569878393&feed=steam_community_announcements)

Direct visual observations from inspected official screenshots:

- Sektori uses bright arena outlines over a dark patterned floor, compact geometric enemies, strongly separated color families, dense local impact fragments, and a score/combo HUD outside the central action. The illustrated arena has narrow connecting passages and large open combat areas. [Inspected Sektori screenshot](https://www.gimblll.com/sektori/sektori_screenshot_basic_c_lowres.png)
- Waves 2 uses raised-looking geometric objects, a dark hexagonal floor, glowing boundaries and circular indicators, and a very bright player/effect center. Its crowded screenshot demonstrates why bloom and particles need readability controls. [Inspected calm frame](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/217120/ss_4c8f24d7707ff4761801bb9c9f5e627fd70fc336.1920x1080.jpg), [inspected crowded frame](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/217120/ss_9f1a15a440da266481b75fec89d56e3eebc4cdca.1920x1080.jpg)

The supplied [Sektori video](https://www.youtube.com/watch?v=3QfnePRu-KE) resolves to the Nintendo Switch 2 launch trailer, 83 seconds. The supplied [Waves 2 video](https://www.youtube.com/watch?v=Lk8y2nPPRds) resolves to a 1,056-second gameplay video. Metadata retrieval succeeded; video playback retrieval returned HTTP 403. No timings, dash invulnerability windows, movement curves, or exact damage rules were inferred from inaccessible footage. A later motion review should measure representative play directly.

The intended game should match this combination of control, combat density, dimensional presentation and feedback with project-authored ships, arenas, enemies, effects, names and music. Reproducing Sektori's branded content is not a dependency for matching its quality and gameplay ambition.

## Repository baseline before this slice

At the start of this research, the playable scene already contained an Encounter and a Player. The older gameplay plan's suggestion to add a wave controller had been implemented. `WaveBehavior` spawns eight enemies at 1.5-second intervals, alternating sides at an offset of 260 units. The player has movement, health, contact damage, trails, a manually triggered weapon and an automatic weapon. [Scene](../../content/scenes/NeonSwarm.json), [wave asset](../../content/behaviors/WaveBehavior.json), [player prototype](../../content/prototypes/Player.json)

The main control gap was independent aiming. `GameInput` stores only a movement vector. The original host sent Fire on a button press, and `AutoWeaponBehavior` finds a nearby enemy to aim at. That baseline was closer to a survival shooter control loop than a player-aimed twin-stick shooter. The new Arena3D slice addresses this with a separate `GameAim` component and authored twin-stick behavior. [Components](../../src/runtime/components.h), [input host](../../src/editor/workspace/gameplay_input.c), [automatic weapon](../../content/behaviors/AutoWeaponBehavior.json), [slice implementation and remaining work](../arena-development.md)

Collision currently means discrete circle overlap with category/mask filtering and a Contact Begin event on new pairs. It uses sweep-and-prune candidate sorting, but no rigid-body integration, contact resolution or continuous collision. `GameTransform` is a two-component position and orientation is a separate planar angle. These are concrete migration points. Fast projectiles and a dash need swept collision before their speeds increase. [Contact implementation](../../src/runtime/scenes/gameplay.c), [components](../../src/runtime/components.h)

## Physics choice

| Choice | Verified capability and maturity | Recommendation |
| --- | --- | --- |
| Box2D | Portable C17; shape casts, overlap queries, a dynamic tree, continuous collision, sensors and contact events. The latest published release checked was v3.1.1, dated 2025-06-04. | Use for the planar arena simulation. Its collision/query routines can support explicitly controlled arcade movement without forcing every actor to behave like a free rigid body. |
| Erin Catto's Box3D | Portable C17; convex hulls, spheres, capsules, triangle meshes, height fields, casts, continuous collision, sensors, a character mover and rigid bodies. v0.1.0 was published 2026-06-30 and its author explicitly calls the release alpha. | Evaluate when combat needs elevation, ramps, jumping between levels, or interactive 3D bodies. Pin the version and test the gameplay cases we rely on. |
| Custom SDF physics | An SDF can provide distance and a local normal. That does not supply broad-phase search, continuous collision, stable contact resolution, friction, stacking, joints or event lifetimes. | Keep SDF distance queries useful for visual picking and controlled special interactions. Do not build a general physics solver as the prerequisite for the game. |

Sources: [Box2D manual](https://box2d.org/documentation/), [Box2D v3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1), [Box3D features and build](https://github.com/erincatto/box3d), [Box3D initial release](https://github.com/erincatto/box3d/releases/tag/v0.1.0). The custom-SDF assessment is an engineering inference from the responsibilities currently supplied by the two libraries.

Both libraries use the MIT license. Both have CMake integration; Box3D exposes `box3d::box3d` and its core needs only the C runtime plus `libm` on Unix. Its C++20 requirement applies to samples. Reuse the project's pinned CMake dependency pattern and build only the selected physics library for the application. Preserve the upstream license notice. [Box2D license](https://github.com/erincatto/box2d/blob/v3.1.1/LICENSE), [Box2D build instructions](https://github.com/erincatto/box2d/blob/v3.1.1/README.md), [Box3D integration instructions](https://github.com/erincatto/box3d#using-box3d-in-your-project)

A planar simulation is compatible with a real 3D camera, geometry, materials, depth, shadows, floor deformation and elevated effects. Define a ground plane in the 3D world, project aiming rays onto it, and keep gameplay collision geometry explicit. The first game needs movement across the arena; arbitrary six-degree-of-freedom combat is a separate capability. This is a recommended initial simulation choice, not a reduction of the requested 3D engine and visual goal.

When physics lands, use fixed simulation steps and an explicit world-unit conversion. Existing positions, speeds and radii are pixel-like; migrate supplied assets together instead of spreading conversion constants through behavior nodes. Keep runtime physics handles out of serialized components. Queue contact events after stepping so authored damage/spawn/destruction remains safe. Box2D's manual explains its units and post-step events. [Box2D simulation concepts](https://box2d.org/documentation/)

## First playable improvement and follow-through

The first visible slice should let the player move and aim independently in a bounded arena while the new 3D renderer develops in parallel. Use keyboard movement plus mouse aim/held fire, and controller left-stick movement plus right-stick aiming/firing. Preserve the last valid aim through the dead zone. Release movement and fire on viewport focus loss. Weapon cooldown, damage, projectile spawning and upgrades remain behavior-authored.

Follow that with a timed dash, a pursuer and a telegraphed ranged enemy, encounter start/clear/death/restart, score/combo feedback, and a small set of upgrades that changes decisions during a run. Add arena transformations and a boss once the basic combat survives crowded play. Give each slice an authored example that a designer can edit from the existing Scene/Graph workspace.

Suggested acceptance scenarios:

- Strafe left while firing right; aim consistently across resized viewports and both camera projection modes. A stationary aiming player can shoot. Diagonal movement remains normalized.
- Hold fire through several cooldowns and then release; no extra shots occur after release, focus loss, Stop or restart. Controller disconnect clears actions.
- Fire a fast projectile across a thin enemy and dash across a boundary at 30, 60 and 120 display frames per second; contacts and arena constraints stay consistent under the fixed simulation step.
- Play a complete encounter, die and restart without stale bullets, contacts, cooldowns or input. Inspect a crowded frame for visible player, threats and boundaries with glow enabled and disabled.
- Edit weapon rate, enemy speed, an encounter pattern and a material parameter without changing C gameplay rules. Native editor checks must exercise search, inline values, graph navigation and Undo for the authored assets.

These scenarios define incremental evidence. Passing the first slice does not finish the larger game, material editor, 3D authoring or rendering goals.

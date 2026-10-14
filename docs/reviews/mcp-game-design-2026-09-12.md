# MCP readiness for Space Jobs

Reviewed against `docs/game-design.md`, draft 0.1, on September 12, 2026.

The existing MCP could inspect and edit much of the engine, but it could not
support this design end to end. The largest missing capabilities were authored
player UI, durable progression, wall-blocked visibility and map memory, sound,
camera/control authoring, and rendered scene feedback. Those capabilities now
exist in the runtime, editor and MCP.

There is no known native authoring blocker for the draft's first design test.
This is acceptance of the authoring capabilities, not a claim that the proposed
game has been implemented or its pacing validated. The 8–12 minute game remains
the next content task. The larger region, parts catalog and later survey jobs
use the same capabilities. Procedural region generation is explicitly later
design work.

## Requirement coverage

| Design requirement | Implementation route through editor/MCP | Review result |
| --- | --- | --- |
| Connected authored cave region, chambers, passages, shortcut and docks | `edit_scene`, prototype composition, transforms, collision geometry, materials and shape graphs | Existing composition retained. New instances inherit prototype behaviors unless explicitly overridden. World coordinates remain authored; graph placement is automatic. |
| Movement, independent aim, primary fire and scanner utility | Input/Aim components, movement/physics nodes and named events; `ActionBinding` maps physical sources to actions | Removed hardcoded Fire/Dash mappings from the input adapter. Keyboard, mouse and controller mappings are authored components. |
| Follow the ship and frame the range | `Camera` with Transform, span and offset; behavior setters animate it | Added an optional camera shared by editor Play, standalone Play and scene previews. Invalid or duplicate enabled cameras produce an error. |
| Ordered carrier/modifier/payload recipes | Behavior variables and collections, functions/macros, conditions, bounded loops, entity spawning and typed events | Existing language supplies the rules. MCP now supports graph selection, signature edits, split/exposed fields, input bindings and local values. Named function references and event declarations resolve without requiring numeric graph IDs. |
| Energy, firing interval, fit limits, one secondary stage, first-impact consumption | Authored state, arithmetic, gates, delays, contacts and component setters | Keep costs, limits, consumption and stopping rules in behavior assets. No native recipe format or game-specific upgrade framework was added. |
| Movement capacitor, scan marking, recoil and target filtering | Movement/velocity reads, state and timers, entity queries, visibility checks, raycasts and impulses | Added `Raycast` with nearest entity, point, normal and distance. It sees new colliders and authored teleports and filters independently of contact masks. |
| Safe docking, job acceptance/progress, settlement, repair and departure | Authored job state, trigger/contact events, damage eligibility, encounter enablement and UI events | Existing generic runtime primitives cover these rules. Dock safety and local escalation must be explicitly authored and verified with game content. |
| Refit panel, ordered slots, compatibility, summary, free testing and undo | `Widget` labels, panels, buttons, slots, sliders and progress bars; typed Source/Value events | Added player UI independent of editor graph UI. Behaviors own transfers, compatibility, summary strings and undo. Native tests exercise actual clicks, drag/drop and keyboard activation. |
| Rewards, salvage, optional extraction and new starting options | Authored inventory/reward choices, plus Read/Set/Remove Saved Text, Commit Save and Discard Save Changes | Added pending and committed save state. A fresh MCP scenario can import committed progress. Failed disk commits preserve the previous save; the Failed output supports retry UI. |
| Current vision blocked by walls | `Fog`, `Vision`, `Visibility At`, and optional `RenderState` culling | Added bounded visibility grids and collider ray queries. Both 2D and 3D scene rendering consume the current-vision mask. |
| Remembered terrain and landmarks without live enemy tracking | Remembering vision sources, map widgets and `MapMarker` | Explored terrain persists independently of current vision. Markers store authored positions and labels; discovery gates them unless explicitly always visible. Map colors are authored. |
| Temporary sensor contacts | `SensorContact` stores position, remaining lifetime and color | Contacts expire independently of terrain memory. Behaviors decide detection and refresh; no automatic enemy tracking is introduced. |
| Thruster wakes, projectile paths, scanner pulses and blast clearings | Temporary shrinking vision sources, existing particles/trails/shader graphs and timed behavior effects | New vision sources support temporary clearing without permanently exploring terrain. Shared rendering contains no Space Jobs choreography. |
| Audible and visual cues, reduced flashes/shake/motion | `Play Sound`, authored particles/materials, camera offsets, UI/settings variables and saved settings | Added bounded swept-tone/noise synthesis and native/browser presentation. Volume can be zero; fog drift can be zero. Authors control effect intensity and timing. |
| Readable surfaces and restrained glow | `Lighting2D`, materials and existing 3D environment/post settings | Exposed 2D ambient, emission, light haze and shadow-floor gains. This fixes the prior hardcoded zero ambient lighting for non-emissive surfaces. |
| Evaluate the actual authored result | `runtime_snapshot`, `run_scenario`, `profile_scenario`, recording/replay, `preview_shader`, new `preview_scene` | Added whole-scene PNG capture with authored camera, fog and player UI. Tests inspect image dimensions and pixels, not just success flags. |

## MCP changes

Graph edits now include `select_graph`, `rename_graph`, `remove_graph`,
`remove_variable`, `set_pin`, `remove_pin`, `split_pin`, `recombine_pin`,
`expose_fields`, `hide_fields`, `bind_input`, `clear_binding` and `create_local`.
Component bindings support a field or a whole component. Variable selectors
support names, IDs and request aliases. References are checked against the
selected graph. Edited graphs are arranged automatically.

Custom event declarations supply payload pins when adding or updating Event,
Call Event and Send Event nodes. Host events expose their standard payloads.
Reflected text/name setters retain strings correctly, which permits live recipe
summaries and objective text.

Scene and prototype execution now share preparation and input handling. The
runtime prototype catalog includes dependencies that are spawned later, even
when they are absent from the scene's initial placements. Scene input supports
movement, aim, held physical controls, named actions, typed events addressed to
named actors, and UI activation/change/drop.

`run_scenario` and `runtime_snapshot` accept up to 108,000 fixed steps, or
30 simulated minutes at 60 Hz. A sparse timeline supplies up to 600 input
changes. Omitted movement, aim and controls retain their last sampled state;
explicit zero vectors and empty control arrays release them.

Recordings use one v2 representation. Duplicate v1 field aliases and the
separate prototype-only scenario path were removed. Replay checks the runtime
asset manifest and hashes before execution. Evidence includes reflected ECS
state, committed saves, visibility memory and sound cue history. This is not a
serialization of every suspended interpreter value or renderer resource.

The existing Arena assets were migrated to authored action bindings, preserving
their mouse, keyboard and controller controls. No Space Jobs game rules were
added to shared engine code.

## Verification

The full sanitized `make test` run passed using `/tmp/chibi-mcp-build`. It
included generation and reflection checks, reload/development checks, MCP
protocol/authoring/optimizer checks, the native test suite, scene-document tests
and headless app startup. The final `make build` also passed. Isolated build
outputs avoided racing other work in the shared checkout.

`tools/mcp/design-smoke.mjs` creates its fixtures entirely through public MCP
requests in an empty content directory. It verifies:

- Semantic graph edits, typed custom events and raycast-dependent behavior.
- Spawning an initially unplaced prototype dependency and inherited behaviors.
- Physical control press/release and named scene actor input.
- Fitting a part by UI drop, changing its visible summary, emitting a sound,
  undoing the fit, and running a sparse 72,000-step timeline.
- Banking an unlock, importing it into a fresh run, and excluding uncommitted
  progress from the exported save.
- Recording/replay, including rejection after a dependency changes.
- Actual 960×540 captures of 2D visibility, 3D visibility, refitting and docking.
  Pixel checks verify visible ship/wall geometry, concealment behind a wall,
  remembered map content and a visible dock button.

Native capability tests additionally check failed save publication, reopening
committed data, wall occlusion, terrain memory after movement, sensor expiry,
camera framing, synthesized PCM energy/fade, real ImGui click/drag/keyboard
interaction, disabled controls and gameplay focus release.

The WebAssembly app built successfully with a separate Emscripten cache. The
browser loaded and rendered the editor without reported console errors. Browser
inspection exposed and led to a fix for enum-backed control choices appearing
as numeric fields. Browser compilation and startup are not a complete browser
playthrough of all new capabilities.

A focused visibility benchmark in the sanitized native build used a 128×128
grid, 65 walls, radius 220 and 120 moving-observer ticks. Replacing linear
collider scans with a reusable Box2D spatial tree reduced the observed mean
from approximately 64 ms to 3.5 ms. This is a focused algorithm measurement,
not a release-build frame-time or game-performance guarantee.

## Practical limits and next content acceptance

- Native scene preview requires GPU/display access. It renders at most 600
  fixed steps and returns 64–1024 pixel images. Recording and profiling also
  remain bounded to short segments; longer tests use run/snapshot timelines.
  The adapter's configurable process timeout can still bound an expensive run.
- The web 2D renderer applies ambient/emission gains directly. Native indirect
  radiance, light haze and shadow-floor controls are not equivalent on WebGL.
- Save keys are namespaced text. Native Play uses atomic user-data files; Web
  Play uses localStorage. Editor preview saves last for the live world.
  Scenarios use isolated, explicit `saved_state` and never touch player saves.
- Sound currently supplies synthesized cues. Imported music/recorded sound is
  outside this draft's implementation needs. Automated checks validate the PCM
  and cue path; they do not certify subjective audio quality or device output.
- Visibility is a finite grid, up to 256×256 cells. Choose cell size and sight
  radius against cave scale and profile representative moving scenes.
- The runtime dependency manifest is conservative: it covers the runtime asset
  catalog, not only the minimum reachable subset. Rendering assets are checked
  through preview/validation rather than the scenario state hash.

The next implementation should author one complete recovery-to-refit-to-hunt
loop, then test the draft's actual acceptance questions: a behavior-changing
first reward, a useful bank shot, visible scan marking, guaranteed dock safety,
feasible fog-boundary warnings, and an extracted unlock available on the next
run. Those are content and playtest assertions, not evidence supplied by a
generic capability fixture.

See `docs/mcp.md` for field contracts and examples. `make mcp` rebuilds and
starts the adapter. Reconnect an already-running MCP client to obtain the new
tool schema, including `preview_scene`.

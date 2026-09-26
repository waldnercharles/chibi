# Working on the arena game

Run `make play` to launch the Arena2D project in a standalone game window. A
project is a directory containing `project.yaml` and its own `content` tree.
Use `make play PROJECT=. SCENE=scenes/Arena3D.yaml` to launch the Arena3D
reference project, or pass `--project-root PATH` to `build/chibi` directly. Both
routes use the same scene runtime, input and Surface renderer as editor Play.
Escape pauses and releases input; Enter resumes, R restarts from authored state,
and Q quits while paused. Losing window focus also pauses. No editor panes are
drawn.

For an intentional game-only capture:

```sh
env MallocNanoZone=0 ASAN_OPTIONS=detect_leaks=0 build/chibi --play \
  --project-root projects/arena2d --scene scenes/Sandbox.yaml \
  --frames 120 --fixed-dt 0.016666667 \
  --screenshot /tmp/arena-game.png
```

Normal play uses elapsed time. `--fixed-dt` supplies the given wall-clock
increment per display frame for repeatable verification. The shared engine
consumes it in 1/60-second simulation steps; `--frames` alone only limits
the run. Screenshots are opt-in and native-only. A web build supports
`chibi.html?play&project-root=projects/arena2d&scene=scenes/Sandbox.yaml` through
the same launch parser. Launching the editor with `--project-root PATH` opens
that project's start scene; `--scene PATH` selects another scene inside it
without starting Play. The Make targets expose the same selection: `make run
PROJECT=. SCENE=scenes/Arena3D.yaml` opens the 3D reference, while the defaults
open Arena2D.

Open `projects/arena2d/content/scenes/Sandbox.yaml` from Files after starting
the editor with `--project-root projects/arena2d`. Arena3D remains in the root
project at `content/scenes/Arena3D.yaml`. Play from the scene toolbar and focus
the scene to control the player. Move with WASD or arrows, aim with the mouse,
and hold the primary mouse button or Space to fire. Controller bindings use the
left stick for movement and right stick for aiming/firing. Escape releases scene
focus. Stop restores the authored scene; Restart starts a fresh encounter.

3D Geometry selects a box, sphere, capsule, cone, triangle prism, octahedron
tetrahedron, Chevron Pyramid, Box Frame, Triangle Outline or flat Triangle and its
width, depth, height and center elevation. The triangle prism has a triangular
footprint pointing local +X, extruded vertically; Orientation turns that tip
toward the actor's heading. Orientation uses `atan2(direction.y, direction.x)`
in gameplay coordinates; only 2D presentation flips its sign. This keeps the
player nose, projectile flight and picking aligned in both views. Gameplay
position `(x,y)` maps to 3D `(x,elevation,y)`;
the ground plane is at elevation zero. Appearance still provides the 2D
representation; Shader selects the Surface material and instance parameters. Physics
colliders are authored separately. Arena2D saves its `presentation` as `2d`;
Arena3D remains available as a visual and gameplay reference scene.

Arena2D's desktop renderer uses the physical HRC channels `scene`, `emission`,
`absorption` and `albedo`, plus SDF-derived planar normals. The Scene Inspector's
Radiance Cascades section edits Work scale, Trace levels and Feedback. The
Performance pane reports frame history and p95 timing. The prototype Inspector's
`GameHRC2D` fields tune transport and composition. HRC transport
uses a fixed work grid, so its dispatch cost does not grow with the number of
entities. The 2D path feeds material emission directly into HRC.
The GLES3 path shows an explicit unavailable diagnostic when desktop compute is
not available.

The current increment includes geometric authoring, controls, Surface graph
binding, shared fixed-step Box2D simulation, 2D SDF solids and desktop radiance
cascades. Three-dimensional indirect lighting and the complete upgrade/run
structure remain open; Arena3D continues to provide the comparison path.
The [roadmap](arena-roadmap.md) tracks those requirements without treating the
first playable slice as the completed game.

The initial behaviors share `MovementActions` and `WeaponActions` library
functions with the existing game. Weapon timing belongs to its behavior, while
the shared spawn function receives its projectile prototype explicitly. Latent
operations are supported in executable library functions; pure functions retain
their restrictions. Review each new mechanic for readable event flow and useful
functions, and extend engine capabilities when graphs expose a real missing
operation.

Local input owns entities with `GameInput`; adding `GameAim` alone does not make
an AI-controlled actor listen to the mouse. `GameWeapon` exposes damage and
cooldown. The unused projectile-count field was removed, and the twin-stick
weapon root shows its projectile prototype choice.

`Has Entities With Component` exposes a component dropdown and a boolean result.
The encounter uses Input membership to check whether its player remains alive.
The node reads the existing ECS component index and excludes entities queued for
destruction; it requires no position, collider or arbitrary search radius.

Validation entry points after `make build`:

```sh
env MallocNanoZone=0 build/test_suite --arena-play
```

Set `CHIBI_ARENA_CAPTURE=/tmp/arena3d-native.png` on that run to capture the
played scene for visual review. Ordinary development does not produce
recordings. These focused checks supplement `make test`; hardware controller
feel, sustained performance and visual-quality comparisons require separate
play evidence.

## Current visual contract

The game mixes 3D actors with 2D graphic effects, with Arcane as a visual reference.
The player is the center of attention: enemy arrivals must announce their actual
positions before becoming dangerous, and effects must clearly follow their source.
There is no gameplay HUD. The fixed elongated hexagon follows complete floor-grid
edges. A permanent outer lattice surrounds the animated inner triangle outlines.

The player is a short forward-facing pyramid with filled chevron faces and a rear
notch. Enemies are smaller hollow cube frames with independent tumble and solid
contacts. Their 11-unit bodies stay smaller even while tumbling than the player's
20-unit ship; colliders, warning footprints and death fragments follow that scale.
Projectiles are spinning cubes; their tails and the small hollow XP pickups are flat triangles. Particle
emitters share their source's interpolated position. The ship nozzle follows its
full visual pose; projectile nozzles use heading so cube tumble cannot swing the
trail through the body. Death pieces inherit the source frame's pose and material.

## First increment, September 8, 2026

The native GPU checks verify perspective mapping, depth independent of draw
order, lighting under nonuniform scale, picking all five solid types and batching
256 repeated boxes or prisms into one draw. Prism checks compare rendered
silhouettes and picking at four headings, including empty corners inside its
rectangular authoring bounds. Shader math checks compare CPU and GPU results,
including zero vectors and negative or extreme noise coordinates.

The native editor check exercises selection, dragging and Undo, resizing,
scene save/reload, independent prototype previews, current-frame mouse aim,
firing and Stop. Input checks cover cooldown tapping, focus and pause release,
restart and AI isolation. The component dropdown has a real selection,
Undo/Redo and save/reload check that verifies the stored component key.
Asset diagnostics now identify a rejected file and
its actual parse, link or shader error while keeping valid assets available.

The WebGL run verifies the saved arena, visible enemy spawning, restart and Stop.
It exposed a shader mount skipped on GLES; the host now mounts preloaded shader
files on that backend as well. This does not add the native 2D radiance pipeline
to the browser.

Physical-controller testing remains open. Camera composition, actor silhouettes,
lighting and combat feedback remain development art; material response alone
does not satisfy the visual-showcase milestone.

## Spatial authoring and Surface materials

Fit and marquee use rotated 3D bounds, including height and elevation. Prototype
placement projects the pointer onto the ground and preserves authored elevation.
Spatial-only prototypes have an Inspector still preview.

Materials now use a Surface output with Base Color, Emission, Roughness,
Metallic, Normal, Opacity and Unlit. Material previews show a sphere through the
same 3D renderer. Parameters expose numeric vectors or a floating point color
picker; selecting the current type preserves values. The supplied floor, rim,
player, enemy and projectile assets use these graphs. See
[shader graphs](shader-graphs.md) for input conventions and renderer limits.

The integrated native checks passed for actual color/vector edits and history,
Surface CPU/GPU inputs, Boolean parameters, instance overrides and batching,
transparency, matching preview/scene output, and retained rendering with
diagnostics during invalid edits. `make test`
and the arena interaction checks pass. WebGL renders the arena materials and
sphere preview; browser playback verifies enemy spawning, restart and Stop with
no shader warnings or errors.

The independent behavior review also moved movement delivery to the simulation
frame boundary. It coalesces the latest sample, checks current Input membership,
and uses that frame's delta before Tick. Scene and prototype input share this
path. Tests exercise unequal frame durations, removing Input before delivery,
release and focus loss, and clearing pending movement on Stop or restart. The
shared projectile reader now exposes only fields consumed by its function.

## Overhead composition and designer effects

The camera now looks down from 10 degrees off vertical with a 35-degree vertical
field of view. The native standalone capture uses the authored scene at 100
frames and a fixed 1/60-second step. Lower bodies, thin layered rails and a dark
diamond floor follow the [reference review](research/arena-visual-direction.md).
Triangle Prism is a general selectable solid whose local +X tip shows the
player's aim. Its visible faces and exact picking agree, including the empty
corners of its rectangular bounds. Composed bodies, contact depth, trails and
impact effects remain visual requirements. The later HDR pass is recorded below.

Mapping, Mapping3D, Gradient, Fractal Noise, Voronoi, Remap, Fresnel and Color
Ramp now provide direct material controls. Select a ramp to add/remove stops,
drag their positions, edit floating RGBA colors and choose interpolation. Stops
and mode choices are authored settings without sockets. Nested reflected values
save and reload through the ordinary graph format, including clipboard copies.
The supplied arena materials use these nodes. Image Texture is covered below.

Native validation passes `make build`, `make test`,
`--arena-ui` and `--shader-ui`. Checks include camera proportions at three aspect
ratios, prism silhouette/picking and 512 boxes/prisms in two draws; CPU/GPU effect
parity; actual ramp editing, rendered changes, Undo/Redo and persistence; and
standalone aim, pause, input release, restart and quit. These checks establish the
increment's behavior; the roadmap's full visual and material gates remain open.

The browser shell fills its window for both editor and standalone Play. WebGL
checks at 1280×720 and after resizing to 960×720 confirm the arena framing,
player material, pause/restart, and no shader warnings. Invalid scene paths show
the rejected path directly. Mixed box/prism GPU tests also verify distinct
Surface materials and instance overrides; custom meshes use Cute's second
attribute stack for the shared material record.

An independent review caught slider rounding that could make adjacent ramp
stops equal. The position control now preserves strict ordering at both ends,
with a native drag regression. Incorrect authored advanced-input flags were
migrated directly, and drag-to-search excludes literal settings.

## Image materials and coordinate clarity

Image Texture now samples PNG/JPG/JPEG assets with separate RGB and Alpha
outputs, linear/sRGB intent, nearest/linear filtering and repeat/clamp wrapping.
The searchable Inspector selector accepts a project image or a path. Files can
drop an image onto that selector or create a node on the shader canvas. Reload
replaces resident pixels without changing graph JSON; decode or size failures
retain the previous image and display the error. Resources are mounted before
production graph registration, including standalone Play and development reload.

The renderer shares decoded straight-alpha pixels and GPU variants. Programs
have an eight-variant image budget, while a scene can use more images through
ordered batches. Each smooth-union metaball group is one composite operation
with the same budget. Images are currently RGBA8, limited to 2048 pixels per side,
and sampled without mipmaps. Explicit Reload is supported; automatic background
file watching and higher-resolution image streaming remain future work.

Unconnected UV/position inputs display Surface UV or Object Position instead of
misleading zeroes. The Inspector can switch to an explicit Value and restore
automatic coordinates. Compiler and editor use one shared definition. Rendered
checks confirm these changes, Undo/Redo and the same node footprint when an
already-visible input changes between automatic, constant and connected values.

EnergyCells is an eleven-node material with a small tiling line image, Mapping,
Time, Color Ramp and emission. Intensity, Scroll Speed and Tiling are named
instance controls. ShaderImageDemo shows it on a box, prism and sphere using the
shared overhead camera. Run `make play PROJECT=. SCENE=scenes/ShaderImageDemo.yaml`.
This is a material-authoring example; the arena's lighting and combat-effect
acceptance remains open.

Verification passes `make build`, `make test` and
`--shader-ui`. The native image tests compare CPU/GPU sampling, nine image
materials on 54 box/prism actors in 18 draws, independent instance overrides,
upright faces, alpha ordering, corrupt-image recovery, 2D compute/fragment
rendering, shape picking and complete smooth unions across batch boundaries.
UI checks exercise actual Files drops, filtering a refreshed catalog, path entry,
sampling choices, Reload, persistence and coordinate overrides. Browser checks
confirm standalone EnergyCells rendering and assigning it to an existing 2D
smooth-union actor through the Inspector, with no shader warnings or errors.

The review found and fixed WebGL minification filtering, GPU texture retirement
after replacement, generated compute binding syntax, and production image-mount
ordering. Failed backend edits keep their last valid program and diagnostics.
Failed shader source can be captured explicitly with `CHIBI_SHADER_DUMP`;
normal builds and tests produce no shader dumps or screenshots.

## HDR emission and Environment authoring

The 3D path now renders linear HDR color before transparent blending, bloom,
exposure and final display conversion. Select Environment in Arena3D's Scene
list to adjust Exposure, Bloom Strength, Bloom Threshold and Bloom Radius.
Strength zero disables bloom. Radius is a percentage of viewport height, so
resizing does not turn a soft halo into a fixed-pixel outline. This is a reflected
component with normal per-instance overrides, Undo/Redo and persistence. It has
no transform or collider and does not affect camera framing or gameplay queries.

The supplied material changes lift the player's complete triangular footprint,
keep rose enemy tops visible, and reserve the strongest sustained emission for
small projectiles. The dark floor stays below the default bloom threshold.
Reference-level motion, contact shadows, composed enemy bodies, trails, impacts
and crowded-combat finish remain open in M4.

The behavior cleanup also restricts the local Fire action to entities with
GameInput in both scene and prototype Play. Directed AI events remain available.
The older unknown-action input fallback still broadcasts a zero-movement Input
event; replacing that fallback with an explicit action contract belongs to M3.

Native verification passes the GPU render checks and both editor interaction
suites. Tests cover HDR values above one, transparent blending before display
conversion, exact sRGB output, radius continuity and resize scaling, disabled
bloom, non-finite values, edge clamping, and retained output during invalid edits.
Actual Environment slider drags preserve their geometry when Revert appears;
Undo/Redo, save/reopen, camera stability and visible diagnostic recovery pass.
Custom material textures also retain authored filter/wrap settings independently
of Cute's draw filter; fragment tests compare rendered samples at texel and wrap
seams with the CPU evaluator.

WebGL validation also exposed a same-frame streaming-buffer fence deadlock in
Cute: the fourth reuse could wait forever after its fence had already completed.
The fence patch releases the completed slot. Buffer streaming also now appends
into unused byte ranges and replaces storage before reusing a full allocation,
using Cute's existing offsets and three-buffer ring. All eight dependency patches
apply and reverse cleanly on the pinned source. Real browser Play now advances normally
with HDR, image materials and no GL errors, including resize from 1280×720 to
960×720. Native quiet captures with default bloom and strength zero preserve
the same silhouettes and dark floor; action effects and dense-combat comparisons
remain outstanding.

At 960×720, short local browser samples of ShaderImageDemo improved from 47
frames in 2.0021 seconds (23.5 FPS) to 241 in 2.0025 seconds (120 FPS) after
the buffer change. A separate three-second trace observed 3,240 blit uploads,
20 storage generations, no overlapping ranges within a generation, no alignment
violations and no timed-out fence polls. This isolates the streaming regression;
it is not a crowded-combat performance result. Browser firing also confirms the
corrected player nose and projectile direction agree.

## Fixed simulation and collider authoring

Scene Play and prototype preview now share one 60 Hz engine. Body selects Static,
Dynamic or Kinematic; Velocity uses project units per second. There are 32 project
units per meter. Collider selects Circle/Box independently of visual geometry,
with category/mask filters, Sensor and a typed runtime owner exclusion. World is
the boundary collision category. Continuous sensors use relative shape sweeps;
authored Contact Begin handlers decide damage, effects and destruction.

Movement graphs set Velocity. Shared Fire Projectile receives an explicit Shooter
handle and starts at its origin, so a thin wall cannot fall behind a muzzle offset.
The old per-tick position integration, projectile direction fields and bounds clamp
have been removed. Input routing is a private engine module; physics and gameplay
remain separate responsibilities. The focused regression entry is
`build/test_suite --simulation`.

The engine reports simulation steps, dropped wall time and any event backlog.
Pause clears accumulated time; Step advances one fixed tick. Debugger resume
finishes the existing tick before another begins. Rendering interpolates solved
positions while ECS keeps the authoritative pose. The [physics notes](research/arena-fixed-step-physics.md)
record validation and the remaining M3 gates.

## Reactive visual iteration, September 9, 2026

The active scope is one fixed arena and the existing weapon. ArenaPlayer no
longer attaches or advertises dash. The broader run structure and SDF/GI
experiments are deferred in the roadmap and GitHub #28/#32.

ArenaFloor now authors an equilateral triangular lattice and inset contours in
its local Floor Grid function. Tile Size, Line Width and Grid Glow are instance
controls. Its dark metallic Surface responds to local lights; narrow blue rails
leave the cyan player and rose enemies in the foreground.

Point Light is a reflected component with Color, Intensity, Radius and Elevation.
It uses the entity's Transform, follows interpolated movement and illuminates
ordinary lit Surface materials. Color channels share the existing packed linear
material convention. Radius and elevation use project units; intensity controls
peak scene-linear radiance. Unlit materials ignore point lights. Each view keeps
at most 32 sources ranked by estimated camera influence, and reports entering or
leaving that budget. Lights do not cast shadows.

The panel response is separate from physical lighting. Surface Influence carries
Color, Strength, Radius and Linger on player/projectile/enemy prototypes.
ArenaFloor's Triangle Center helper computes one world-space sample position per
triangle. Surface Influence is sampled at that center and drives the panel's own
emission, with stronger contours and a restrained fill. Nearby panels can have
different activation values. Dim Point Lights remain a secondary projectile cue.
The renderer retains at most 64 planar samples, spaced by a quarter of each
source radius, and keeps the strongest remaining colored signals when full.
Overlaps combine by channel maximum. Linger uses simulation seconds, so pause
freezes decay; restart and scene changes clear history. The node returns black
without a scene signal, including standalone material previews. The current
floor uses identity mapping so cell centers remain in world coordinates.

ArenaImpactFeedback reacts to accepted Damaged and Died events by spawning
ArenaHitPulse or ArenaDeathPulse. SurfacePulseBehavior reads the initial influence
strength, fades it with a squared envelope and destroys its entity after 0.28
seconds. A death pulse survives retirement of the enemy that spawned it. These
are ordinary prototypes and behavior graphs. Tune their values in the Inspector
and graph editor; transient source-only prototypes currently need a receiving
scene material to preview the visible response.

A later holographic floor or force-field mesh can consume the activity signal
for event-driven deformation. Geometry displacement is deferred; emissive panel
activation is the current behavior.

Octahedron is a selectable spatial solid with eight flat faces and exact picking,
including the empty regions inside its rectangular bounds. ArenaEnemy uses it
with a rose Surface material. This adds a faceted silhouette; multipart shells,
hollow forms and contact shadows remain separate art and authoring work.
The 3D particle path now honors authored end color, alpha and size exponent,
matching the existing particle controls.

The first launch check also exposed in-progress asset errors: malformed event
payload IDs, array defaults, exposed component field IDs and stale edge
conversions. They were migrated to the current graph contract. The temporary gameplay HUD
from that increment has since been removed, including its source and build
registrations.

### Panel review evidence

Native GPU checks verify constant panel emission, moving-source decay, pause,
reset, independence from Point Light, and the bounded history. The final scene
was played through ordinary movement/aim/fire input in the standalone host:
40 enemies cleared, 4,752 units travelled, with peaks of 9 enemies, 7 projectiles,
116 particles and 4 draws. Actual 1920×1080 captures show cyan shot/player panels
and rose impact panels with sharp triangular boundaries.

On the Apple M4 Pro, the 30-second simulation review used 180 warm-up frames and
1,403 active host wall-frame samples: p50 8.325 ms, p95 9.077 ms, p99 9.324 ms.
Screenshot readback/writes were excluded. A paused browser arena was open; no
compiler was running. This is a short native sample, not GPU pass timing or the
sustained two-minute crowd acceptance. The 64-sample activity budget was reached
once, so denser play still needs a deliberate quality/budget review.

The capture confirms the floor mechanism and readable play. Round particles
still read as beads, enemy geometry remains a single faceted solid, and the rim
is subdued. Composed enemy bodies, fragment art, contact depth and the broader
visual showcase remain open in #32. Holographic deformation remains a later
experiment.

Material inspection also found authored parameters sharing GPU slots, which
silently mixed Base Color and Hit Flash. The affected Arena materials were
migrated, and distinct parameters sharing a slot now produce a compiler diagnostic.
Web packaging tracks content and shader file changes, so asset-only edits trigger
a fresh preload bundle when running `make web`.

Validation: `make build`, `make test`, `make web`,
`--arena-combat`, `--arena-encounter` and the complete `--arena-ui` route pass. The actual Surface Influence
Inspector check edits Color, Radius, Strength and Linger, exercises Undo/Redo,
and saves/reopens the scene. Existing health-driven fixtures were migrated to the
current event catalog; their behavioral assertions remain in place.

## Outline floor, ship motion and experience polish

The current reference is [the supplied Sektori gameplay clip](https://www.youtube.com/watch?v=RV8g5RcSeMo).
Floor emission is confined to hollow triangle contours. The previous filled-panel
increment above is superseded by this outline treatment. `ArenaFloor` exposes
Tile Size, Idle Size, Active Size, Line Width, Idle Glow, Reaction Gain and Beat
Amount as material parameters. Surface Influence still provides the spatial
activity signal; Inner Radius permits an annular pulse. `ArenaWaveBehavior`
spawns a red expanding ring for each group of eight enemy arrivals.
`ArenaSpawnMarker` displays the actual formation positions for one second before
`SpawnTelegraphBehavior` resolves each `GameSpawnRequest`. The encounter alternates
edge rows, arcs and pincers; pending markers count toward encounter completion.
Delay and the incoming prototype are ordinary editable fields.

`GameRhythm` on the environment supplies a shared 120 BPM clock with a 0.18-second
pulse window. Scene Rhythm exposes phase, pulse, period and duration to behavior
graphs; Beat Phase and Beat Pulse expose the same simulation clock to Surface
materials. Pause freezes both. This is a configurable tempo, not music analysis
or an audio transport; connecting actual music remains later work.

`GameMeshParts` composes ordinary solid primitives with local size, position,
rotation and optional material overrides. The component Inspector edits these
as rows with Add/remove controls. `GameVisualTransform` adds visual pitch, yaw,
roll and scale; movement, aim and colliders stay independently authored.
`ArenaShipMotionBehavior` rolls gradually between the pyramid's faces while
strafing relative to aim. Release settles onto the nearest face; reversing lifts
the nose slightly. Rotation and angular velocity remain continuous.
`GameParticleEmitter` follows the interpolated visual pose at its local nozzle
offset, including heading and roll. Emitted particles retain their own trail.
Rate, size, lifetime, velocity inheritance, color and shape are editable fields.
Heading Only keeps a projectile nozzle behind its flight direction while the
visible cube tumbles. Emission and particle aging follow presentation time,
including frames between fixed simulation ticks; pause freezes them.
Attached emitters currently render through the Arena3D presentation. The shared
particle lifecycle still supports behavior-triggered bursts.
Cube projectiles use a second authored emitter with shrinking flat triangles.

XP uses `GamePickup`, `GameExperience`, `GameVisualTransform` and `GameTrail`.
`ArenaPickupBehavior` owns the twelve-second wait, final two-second expiration,
beat spin and the collection trajectory. Each pickup has a random initial rotation. One eased revolution spans the whole
beat while scale moves gently from 1 to 0.8 and back. The pickup is a small, flat
triangle outline with restrained emission. Collection first moves outward, then follows a
shrinking spiral toward the moving player over 0.6 seconds. A connected
camera-facing ribbon follows a smooth curve through the sampled path and fades
with simulation time. Experience is credited on arrival; repeated collection requests cannot credit twice.

`ArenaDropBehavior` separates the twelve beams of the enemy's hollow cube frame.
Both the living mesh and the authored `Primitive Part` node read the same
canonical geometry. Each fragment inherits the source dimensions, full visual
pose, scale, material and material parameters. The new pickup temporarily retains that
source snapshot while the authored spawn sequence runs. Shards then separate,
spin and fade. Encounter completion waits for final XP arrival; this keeps the
last reward visible. The first iteration does not add upgrade choices.

SDF rendering remains a bounded comparison in issue #34. The chevron pyramid
and exact frame breakup share explicit polygon geometry. A future SDF trial
should prove a useful blended or deforming silhouette with correct depth,
normals, editing and measured cost before replacing these assets.

For an intentional ten-second gameplay recording, create an empty frame directory
and run `--arena-play-review` with `CHIBI_ARENA_VIDEO_FRAMES` pointing to it.
The check captures 300 PNGs at 30 FPS over simulation seconds 5–15; file capture
is excluded from reported frame samples. Encode those frames with ffmpeg for
review. Captures are opt-in and are never generated by normal development.

### Earlier verification before the mixed 2D/3D revision

The authored 1920×1080 run cleared all 40 enemies over 1,800 fixed simulation
frames and travelled 4,863 units. After 180 warm-up frames, 1,520 active host
wall-frame samples measured p50 10.777 ms, p95 13.757 ms and p99 18.805 ms.
Screenshot readback/writes were excluded. Peaks were nine enemies, seven
projectiles, 105 particles and five draws. A paused browser preview was open;
no compiler ran during the recording. This short native sample is not GPU pass
timing or the longer crowded-scene performance gate. The 64-sample floor
influence budget was reached and retained the strongest signals.

The recorded ten seconds include small idle outlines, local cyan/amber/rose
activation, red expanding wave rings, ship turns, enemy pieces and XP. The
same assets power editor and standalone Play. Native regressions exercise
mesh-part editing and persistence, actual SDL mouse-button hold/release, pause
and focus transitions, source-string ownership after enemy destruction, and
last-valid render retention after malformed mesh input.

Final checks passed: `make build`, `make test`, `make web`,
`--arena-ui`, `--arena-encounter` and `--arena-play-review`. The final browser
build was reloaded, restarted through its menu and paused at full health with
clean diagnostics. A quiet bloom-disabled capture confirms that the ship and
floor remain readable without the halo.

## Mixed 2D/3D revision verification

The current scene uses a filled chevron pyramid, 11-unit hollow enemies with
10-unit collision radii, complete outer floor triangles, restrained inner line
reactions, and no HUD. Spawn markers announce separated formations for one second
before those positions become dangerous. Frame breakup inherits the source
geometry, visual pose and material. XP is small, hollow and flat, with a random
starting rotation, gentle beat motion, expiry and a faint spiral collection trace.

The editor mouse fix excludes ImGui's window-movement active ID from keyboard
control capture. Native SDL tests now hold mouse fire together with WASD and aim
drag. Attached emitters use interpolated presentation time, with equal-duration
60/120 Hz trajectory, pause and reset checks. Heading Only keeps cube-shot tails
behind the firing direction while ship exhaust follows the full visual pose.
Ready spawn initializer chains finish inside the same fixed tick, preserving
movement while formations and death pieces are created.

A CPU sample also found every prototype being serialized into both spawn hosts
on every display frame. Registration now runs when assets load or change. Native
editor regressions verify that edits, Undo/Redo and behavior attachment changes
reach the next scene and preview spawn, while earlier instances retain their data.

`make build`, `make test`, `make web`, native render checks and the full arena UI
checks pass. The rebuilt browser preview was reloaded, restarted and paused with
clean warning/error diagnostics. Normal and bloom-disabled captures retain clear
ship faces, cube cutouts and outer grid lines.

The final authored play review completed all 40 enemies across 1,800 fixed 1/60
frames, travelling 4,116.6 units. The scripted pilot uses real SDL movement,
mouse aim and held fire; it avoids warnings and aims at live enemies. The video
contains simulation seconds 5–15 at 30 FPS. Gameplay values are unchanged by the
review controller.

At 1920×1080, after 180 warm-up frames, 1,195 active host wall-frame samples measured
p50 20.248 ms, p95 26.513 ms and p99 33.934 ms. Peaks were 18 enemies, seven shots,
156 particles, 36 beam shards and 51 draw commands. Immediate presentation was
accepted. Screenshot readback/writes are outside samples, but samples include OS
presentation waits and are not GPU pass timings. No compiler ran during capture;
the browser preview was paused. The 64-sample floor influence budget was reached.

The 16.7 ms p95 / 20 ms p99 target is **not met** by this run. Issue #32 retains the
performance gate. An earlier CPU sample spent 54% of its main-thread samples
waiting for a Metal drawable; future measurements should separate simulation,
render submission, GPU passes and presentation, and inspect crowd draw growth.
The earlier and final pilots produced different workloads, so their total timings
do not establish a before/after speedup from the registration fix. Visual quality
review and sustained performance remain open acceptance gates in the roadmap.

The separate compiled-System integer arithmetic defect is tracked in #36. The
arena's generic behavior arithmetic path is covered by the passing tests.

## Motion, graphic VFX and editor profiling

The ship rocks gradually onto its next 90-degree face during sustained strafing.
Reversal preserves angular velocity; release settles to the nearest face without
resetting to zero. Moving backward lifts the nose by three degrees.
In the editor, select ArenaPlayer, open Behaviors, then ArenaShipMotionBehavior.
Under Variables → Motion, tune Roll Speed (36 degrees/second), Face Rocking
(0.5), Roll Acceleration (3/second), Strafe Dead Zone (0.12), Settle Response
(1 Hz), Settle Damping (1) and Reverse Nose Lift (3 degrees). Save and restart
Play to use changed defaults.

Enemy rotation integrates in the native `GameVisualMotion` ECS system. The
component Inspector exposes Speed (1.6 by default) and Reaction Duration (0.32
seconds). ArenaEnemyVisualBehavior chooses random initial angles and rates and
triggers a finite contact/damage reaction; its Bump Lift remains authored. The
native component owns resolved visual rotation and elevation, preserves scale,
and leaves gameplay heading and collision geometry independent. Its reaction
uses a half-sine envelope with exact zero at each endpoint. Invalid values or
missing Visual Transform/3D Geometry fail the simulation with a diagnostic.
Adding Visual Motion preserves the current visual rotation and elevation, starts
at speed 1, and leaves its reaction inactive. Scene Undo/Redo retains those
captured defaults. Begin and contact changes resolve before presentation in the
same simulation step without advancing motion a second time.

The frame bars are thicker. The living Box Frame mesh and the Primitive Part
behavior node consume one canonical twelve-part definition. The death behavior
copies the complete source pose, scale, dimensions and material parameters. CPU
checks compare every fragment at birth under a non-unit source scale; a rendered
check compares the assembled fragments against the living frame. This is exact
decomposition of the authored frame, not arbitrary imported-mesh fracture.

Flat XP triangle vertices are centered on their geometric centroid, including
matching bounds and picking. Collection trails interpolate retained positions
with smooth curves and use a connected camera-facing ribbon with shared joins,
antialiased edges and continuous fade. Sources share one geometry upload and
keep independent depth-sorted draw ranges. Ordinary particles use camera-facing
quads with flat plasma regions, streaks and rings. Impact bursts originate at
the projectile's actual elevation. The engine plume and projectile triangle
tails remain attached to their interpolated source pose. Enemy fragments remain
solid 3D geometry.

View → Performance opens a dockable panel with 240 frames of stacked CPU timing,
a 16.67 ms reference line, frame hover, averages, p95 and Freeze capture. The
breakdown separates window input, document/input routing, simulation, scene
submission, editor UI and presentation. The active Scene or Prototype Preview
runtime reports behavior/event time, physics, other ECS systems, fixed steps,
queued events and dropped simulation time. Present includes GPU and display
waits; these are CPU scopes, not GPU pass timestamps.

Expand Simulation to see behaviors ranked by CPU time, then expand a behavior
to see its events. The rows show mean milliseconds per captured frame and total
starts/resumes across the captured history. Nested functions belong to their
root event; nested root events are counted separately without double counting.
Dispatch/setup overhead remains visible separately. Freeze retains labels and
measurements even if the scene or behavior is subsequently replaced. Collection
is bounded at 256 behavior/event pairs with an explicit overflow total.

The current backend label is **Interpreted behavior execution**. Validation by
`behavior_asset_compile` does not generate native code. Actual behavior JIT and
WASM bytecode integration remain unfinished acceptance in #8; see
[behavior execution](research/behavior-execution.md). The existing compiled
System arithmetic tests are not proof of compiled Scene behaviors.

Use `make run` for the optimized editor or `make play` for the optimized
standalone game. `make debug` runs the plain Debug editor, and `make debug-san`
runs the sanitizer-enabled Debug editor. The latter defaults to ASan and UBSan;
pass `SANITIZERS=thread` to use TSan. TSan and ASan cannot share one binary, so
the target switches the sanitizer mode in the shared Debug build directory.
`make benchmark` uses a separate RelWithDebInfo profile so sampling keeps
symbols without changing the interactive release build. Test drivers keep
assertions enabled in optimized builds because their fixture setup and checks
require them; runtime libraries retain normal optimized flags.

The native CI matrix now exercises sanitized Debug and optimized
RelWithDebInfo with SLJIT enabled in the optimized job. Optimized validation
exposed stale cache reuse across graph lifetimes and graph/query mutations
hidden inside assertions. Runtime stores now distinguish graph lifetimes, and
those mutations execute independently of assertions. Test-driver assertions
remain enabled so release validation does not silently skip its fixtures.

`build/test_suite --arena-crowd-review` reproduces accumulation without shooting
through the actual editor loop. It runs 1800 fixed steps, discards 180 warm-up
frames, reports costs by enemy-count bucket and ranks behavior/event costs at
40 enemies. Run the identical selector from `build-release` for a matched
optimized comparison. Do not run a compiler, sampler or another GPU workload
during a clean timing capture. The 1920×1080 app window contains a docked Scene
viewport; these measurements are not full-screen GPU render timings.

`make play` uses the same executable and shared content-loading/startup objects.
Its per-frame path calls the engine directly and skips editor tick, authoring
updates, previews, graph drawing and editor panels. Pause and restart still use
the shared scene/runtime commands. It is not yet a separately packaged runtime
binary.

An actual editor Play CPU sample showed repeated node/connection scans and
unchanged graph serialization dominating the initial active workload. Runtime
states and DATA/EXEC connections now have indexed lookup. The debugger caches
compatibility by working revision and applied definition, while pending edits
still force an immediate check. Trace recording uses a bounded ring with owned
event names; its chronological view rebuilds only when inspected. Tests exercise
paste, remapping, edge edits, concurrent immutable reads, trace wraparound,
hot apply, and pooled instance reuse. Gameplay remains authored in graphs; this
pass removes avoidable runtime overhead rather than moving game decisions into
C systems.

### Earlier shooting workload sample

The final actual editor Play review used Debug with ASan/UBSan on Apple M4 Pro,
a 1920×1080 app window and immediate presentation. The editor Scene viewport
remains docked beside the graph and Inspector. It cleared all 40 enemies, travelled
5,389.5 units, and produced 1,165 active samples after 180 warm-up frames in a
1,800-frame fixed-step run. Host wall-frame time measured p50 **11.604 ms**,
p95 **15.025 ms**, p99 **20.732 ms**. Peaks were 19 enemies, 10 projectiles,
207 particles, 36 exact frame pieces and 30 draw submissions. The source-level
trail submissions preserve ordinary transparent depth ordering.

A matched intermediate run had the same encounter counts and travel, with
p50 14.614 ms, p95 20.271 ms, p99 25.313 ms and peak 85 draws. That intermediate
already contained the first runtime lookup and debugger compatibility fixes.
The final run also includes bounded trace history, EXEC indexing and ribbon
geometry. The original unoptimized pilot failed to hold viewport focus and did
not shoot; its aggregate timings cannot establish a before/after speedup.

Final mean editor CPU scopes were simulation 8.772 ms, scene submission 1.403 ms,
editor UI 0.984 ms and Present 0.358 ms. Simulation included behavior/events
8.627 ms, physics 0.123 ms and other ECS systems 0.016 ms. No compiler, screenshot
writer or CPU sampler ran during this measurement. The short native p95 is
below the 16.7 ms target; p99 remains above the 20 ms target. Sustained crowd
and browser performance acceptance remain open in #32.

The final standalone review cleared 40 enemies, travelled 4,116.6 units and
retained 1,195 active samples: p50 10.508 ms, p95 14.505 ms, p99 20.168 ms,
peak 30 draws. It uses different viewport/input geometry from the editor review,
so the two routes are reported independently.

Verification passed: `make build`, `make test`, `make web`, the full
`--arena-ui` route, and both editor/standalone played
encounter reviews. The final ribbon checks include pause/reset, source colors,
opaque occlusion and a real transparent particle crossing. The rebuilt browser
renders, restarts and pauses without shader, WebGL or runtime failures. Crowded
idle playback emitted the existing bounded floor-influence and point-light
budget notices. It is left paused on a fresh run.

### Crowd accumulation and optimized validation

The subsequent matched crowd review leaves all 40 enemies alive and stops
shooting. Apple M4 Pro, 1920×1080 app window with a docked Scene viewport,
1800 fixed steps, 180 warm-up steps, 1620 active samples and 834 samples in the
40-enemy bucket. Before/after runs both report zero shots after warm-up, zero
kills and 5228.5 units of player travel. No compiler, sampler or capture writer
ran during timing; the browser preview was paused.

| Native configuration | Before simulation at 40 enemies | After | Reduction |
| --- | ---: | ---: | ---: |
| Debug with ASan/UBSan | 14.567 ms | 7.117 ms | 51% |
| RelWithDebInfo, sanitizers off | 2.164 ms | 0.718 ms | 67% |

The optimized 40-enemy bucket spends 0.631 ms in behavior/event dispatch,
0.080 ms in physics and about 0.007 ms in other native systems. Wave Tick
accounts for 0.236 ms, Enemy Tick 0.150 ms, Ship Motion Tick 0.040 ms and
host dispatch/setup 0.164 ms. Scene behaviors still execute in the interpreter.

Debug wall-frame p50/p95/p99 changed from 15.042/17.839/18.288 ms to
8.903/10.239/11.288 ms. Optimized wall frames remain paced by presentation:
8.169/9.151/9.293 ms after, compared with 8.300/9.054/9.221 ms before.
Optimized Present averages 7.208 ms. These single native captures do not
establish browser or sustained full-screen GPU acceptance.

The runtime now uses indexed component lookup, epoch-based memo invalidation,
bounded traversal history and indexed debugger watches. Loop continuation
watches preserve the original input snapshot and update outputs, avoiding
repeated full-array copies. Published ForEach values remain owned across
completion and suspension. Visual Motion removes continuous enemy rotation
from authored Tick events; idle combat feedback avoids inactive effect work.

Both complete native test suites pass. Optimized validation also found and
fixed a stale graph-lifetime cache, plus graph extraction, Behavior Expression and
query-construction operations previously hidden inside assertions. The CI
matrix now checks Debug and optimized native configurations. Native behavior
JIT is still unfinished in reopened #8; compiled-System arithmetic is fixed
locally and covered in #36. A separate shooting review still clears all 40
enemies, including exact frame breakup and XP collection.

The rebuilt Web game also renders, accepts held primary fire, restarts and
pauses. Its only observed console diagnostics are the existing bounded floor
influence and light-budget notices. Browser frame cost remains a separate
acceptance gate. The browser is left paused on a fresh scene.

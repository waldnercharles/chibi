# Graph-authored projectile loop

Status: projectile lifecycle, contact, damage, and death are implemented and
verified in both runtime hosts, September 5, 2026. Enemy pursuit and automatic weapons are also implemented and verified in both hosts. The sections below retain the
design rationale and ordering. Broader Neon-Swarm gameplay and final editor
polish remain unfinished.

## Implemented evidence

- `game/entities.[ch]` owns captured prototype requests, deferred allocation,
  initialization, Begin ordering, destruction, and reset cleanup. Each flush
  handles at most 64 requests; unchanged prototype registrations reuse snapshots.
- `core/flow.[ch]` distinguishes host waits from timers. Tokens reject stale
  completion and release finished observers without waiting for an activation
  to end. System-only compiler rule CMPL-21 rejects latent structural operations.
- Reflected GetEntity/SetEntity/HasEntity nodes take explicit entity handles.
  Structural execution and non-owner access run serially in the behavior pump.
- Player's WeaponBehavior handles Fire and spawns the authored Projectile.
  ProjectileBehavior owns movement, age, and expiration. Native firing and native
  movement/expiry were removed. ProjectileHitBehavior and HealthBehavior now
  author hit response, bounded damage, health changes, and death.
- Engine and isolated preview dispatch Tick to runtime attachments, including
  spawned entities. Rendering queries the active world, so spawned projectiles
  appear without entries in the authored actor list.
- `make test` covers initialize-before-Begin, cancellation before/after allocation,
  debugger pause/resume/cancel, repeated spawning, reset, dead handles, bounded
  batches, changed defaults, and the authored lifecycle in both hosts. Native
  editor tests play the scene, fire a projectile, and open WeaponBehavior.
- Repeated spawning exposed borrowed ECS descriptor strings; registration and
  migration now own descriptor names/keys. Compiler diagnostics now retain the
  actual root graph ID (zero), preserving source-node lookup.
- `Collider` defines radius, category, and mask. Native contact detection
  snapshots the world, filters pairs symmetrically, and queues Contact Begin once
  per overlap. It no longer changes health or destroys projectiles.
- Queued entity events copy scalar payloads and metadata. Delivery is bounded to
  64 events per boundary, skips dead or retiring targets, and defers recursively
  submitted events. Gameplay events pause with simulation; structural cleanup is
  separate. Runs own dispatch names and input type names across debugger holds.
- `Send Damage` only sends an event. Its recipient graph decides how to change
  health. Exact integer comparison avoids converting health values to floats.
- Combat tests cover masks, overlap history, separation, entity replacement,
  reset, pause, damage surviving projectile destruction, and simultaneous lethal
  hits in scene and isolated preview. Event tests cover ownership, cancellation,
  bounded batches, recursion, and debugger lifetime.
- Reflection now prioritizes declared names over layout and line heuristics.
  Adding Collider preserves every existing component ID and key. `make test`
  includes insertion, shared-layout, and rename regression cases.

## Verified starting point (before this slice)

- `content/behaviors/InputBehavior.chibi` is the live movement path. It is attached
  to the Player prototype and receives the typed `Input` event dispatched by
  `app/gameplay_input.c`; the graph writes the owning entity's transform.
- `content/systems/MovementSystem.chibi` is a standalone system-authoring fixture
  used by the System document editor and benchmark. It is not attached to the
  shipped Scene and is not run by the main gameplay host.
- `content/gameplay/*.json` files are reflected authoring-data documents. The
  editor loads them for inspection, numeric editing, Save/Reload, and Undo/Redo;
  they are not currently applied to ECS state or used to configure simulation.
- Projectile creation, movement, lifetime, contact, damage, and death are now
  authored in behavior assets. `game/gameplay.c` remains only the shared native
  contact detector used by the behavior hosts.
- `game/prototype.c:prototype_spawn` applies ECS commands internally and
  attaches behaviors after writing prototype defaults. It cannot run inside a
  system query. ECS recording APIs also reject active iteration.
- `reflect/reflect.c:emit_component_nodes` generates component access against the
  behavior owner. Initializing another entity requires entity-targeted access.
- `game/behavior.c:behavior_attach` dispatches Begin immediately. Its pump
  can execute attachments in parallel without structural-effect gating.
- Core flow supports time-based suspension. Host-operation completion needs an
  explicit suspension mechanism; a fabricated timer is not that mechanism.
- Neon-Swarm's `src/projectile.c:projectile_spawn` clones authored defaults then
  initializes runtime state. `src/weapon.c` composes that state from weapons and
  supports. Those rules must eventually remain visible in our graphs.

## Chosen design

Implement **latent Spawn Prototype in behavior graphs** first. It yields to the
host, creates a real entity outside iteration, then resumes the graph with its
entity handle. This uses the existing ECS storage and avoids unpublished entity
storage, reservation semantics, and changes to component lookup.

The authoring sequence is Fire → Spawn Prototype → Set Transform → Set Projectile.
Spawn completes automatically at a safe boundary. No separate Commit Spawn node
is required. Pure/dataflow systems cannot use this latent node; report a compiler
diagnostic there. A later system-to-behavior event operation can request spawns.

Use a small `game/entities.[ch]` host owned separately by each scene/isolated live
world. Bind it as `game.entities` to behavior instances. Its responsibilities are
prototype lookup, queued spawn/destruction requests, and boundary processing.
Register owned prototype snapshots by stable identity; requests capture their
snapshot so a subsequent editor change cannot alter an in-flight request.

Host API shape (see `game/entities.h` for current signatures):

```c
entities *entities_new(behavior_world *behaviors);
void entities_free(entities *entities);
int entities_register(entities *entities, const prototype *prototype);
int entities_request_spawn(entities *entities, flow_context *ctx,
                                const char *prototype_identity);
int entities_request_destroy(entities *entities, ecs_entity entity);
int entities_flush(entities *entities);
```

Keep request and continuation details private. The spawn request records a stable
activation identity and output location, not an unowned `flow_run *`. Add the
small core host-wait/completion API needed to resolve an output and make that
activation runnable. It must distinguish host waiting from time waiting and
reject completion after cancellation or pooled activation reuse.

## Ordering contract

1. Spawn captures its request and suspends the activation. It publishes no entity
   output yet. Structural nodes execute serially; they cannot race the queue.
2. At a boundary after behavior pumping and all active queries, the host creates
   the entity and applies its prototype components **without attaching behaviors**.
   Extract this component-initialization operation from `prototype_spawn`;
   preserve the existing synchronous editor wrapper.
3. Resolve Spawn's entity output and resume its continuation serially. The handle
   now names a live entity, so targeted component nodes access ordinary ECS data.
   This initialization continuation runs before ordinary systems see the entity.
4. Attach the new entity's behaviors after the continuation completes or reaches
   a gameplay yield (Delay or another host operation). Begin therefore reads all
   synchronous initialization writes. Writes after a gameplay yield occur after
   Begin. A debugger pause does not finish initialization: defer Begin and ordinary
   simulation until the user resumes or cancels the initialization activation.
5. On initialization failure or cancellation, destroy the uninitialized entity.
   Cancellation before allocation drops the request. Freeing/restarting the world
   cancels its outstanding requests. Never leave an entity awaiting Begin forever.
6. Destroy Entity queues destruction; it never destroys its executing instance
   inside the node callback. At the boundary, detach behaviors before destroying
   ECS state. A spawn destroyed during initialization gets no Begin.

Process a bounded request batch per boundary. Requests issued by initialization
or Begin wait for a subsequent batch; recursive spawning must not hang a frame.
Ensure spawned entities receive later Tick events: the scene currently has an
authored actor list, which must not become the runtime attachment scheduler.

## Implementation ownership and completion criteria

| Work | Files | Done when |
| --- | --- | --- |
| Host suspension | `core/flow.[ch]`, `core/instance.[ch]` | Completion resumes exactly once; canceled/reused activations reject stale completion. |
| Spawn lifecycle | New `game/entities.[ch]`, `game/prototype.[ch]`, `game/behavior.[ch]` | Allocation, initialization, Begin, cancellation, and destruction obey the ordering above. |
| Graph nodes | Game node registration; `reflect/reflect.c` | Spawn/Destroy are discoverable; targeted component Get/Set accepts `ecs.entity`; existing owner nodes remain compatible. |
| Scheduling | `engine/engine.c`, `game/prototype_preview.c`, `app/scene_authoring.c` | Scene and isolated preview bind equivalent hosts, register prototype snapshots, and flush outside queries; structural behaviors run serially. |
| Authored content | `content/behaviors/`, Player/Projectile prototypes | Fire, spawn, initialization, movement, and expiration execute through graphs. |

Mark structural nodes with structural effects, and targeted writes as
non-owner-scoped writes. Start by running the behavior pump serially when any
attached graph requires structural execution. Preserve parallel execution for
eligible graphs later only when its scheduling is proven safe.

Forward a typed Fire event from input to attached behaviors. Author WeaponBehavior
to read the owner transform/weapon, spawn Projectile, and initialize its state.
Author ProjectileBehavior to update transform and age on Tick and destroy itself
at its lifetime. Remove native fire/movement/expiration for these authored entities
so two implementations cannot both run. Collision/damage may temporarily remain
native but must be separated from movement/expiration; combat is the next slice.

## Acceptance

Run each scenario in both the scene and isolated prototype preview:

- Fire produces one Projectile from the selected authored prototype. Its Begin
  observes the graph's overridden position, direction, and damage exactly once.
- Editing speed or lifetime changes the next shot without recompiling C. A graph
  edit to movement changes execution through the existing live preview path.
- Projectile Tick moves it, increments age, and expires it through Destroy Entity.
  No native fire/move/expire call executes for it; no behavior attachment survives
  its destruction.
- Cancel before allocation, cancel after allocation, failed initialization,
  destruction during initialization, and restart during a pending request leave
  no leaked entity, attachment, or stale completion.
- A breakpoint between Spawn and Set holds Begin until initialization resumes.
  Delay after Set allows Begin and documents the yield boundary explicitly.
- An active query cannot trigger immediate ECS mutation. Unsupported latent
  system graphs receive a source diagnostic. Multiple requests have deterministic
  order, and recursive Begin spawning is bounded per frame.
- Native editor interaction opens and edits the authored Weapon/Projectile graphs,
  plays both targets, and observes these outcomes. `make test` includes the runtime
  ordering checks and the native authoring scenario.

This work proves projectile lifecycle and basic combat authoring. It does not
establish Neon-Swarm coverage: targeting, enemy attacks, waves, upgrades, piercing,
chaining, weapon composition, and effects remain explicit follow-up work.

## Contact, damage, and death design

The previous collision loop excluded only the projectile itself, then mutated
health and destroyed the projectile. Neon-Swarm filters contacts with
categories and masks in `src/projectile.c` and consumes begin contacts in
`src/combat.c`. Its `src/enemy.c` guards dead targets and owns death responses.

Introduce a world-owned contact host with authored radius, category, and mask
fields. Track overlapping pairs so a persistent overlap generates one Contact
Begin event. Collect pairs during queries, then dispatch after queries end in a
deterministic order, revalidating entity handles. Reset clears contact history.
Native detection must not subtract health or decide projectile lifetime.

Use the existing typed behavior event inputs for `Contact Begin(Other, Point)`
where `Point` is a `CF_V2` world-space position, and
queued entity-targeted delivery for `Damage(Amount, Source)`. Projectile graphs
send damage and destroy themselves; Health graphs guard living state, subtract
and clamp health, then perform the death response. An Apply Damage node that
mutates health internally would conceal the rules this slice must expose.

Verify enemy-only filtering, persistent overlaps, separate projectiles, lethal
damage, duplicate contacts with dead entities, debugger holds, and reset in both
runtime targets. Include a visible target and an authored edit changing the hit
or death response without changing C. Later piercing, chaining, and death effects
must be able to replace these graph decisions.

## Contact performance

Contact performance is measured by `contact_benchmark`, also included in
`make benchmark`. It creates 1,000 and 4,000 circle colliders in sparse and
overlapping grids, measures initial contact creation, then averages 20 detection
passes with established contacts. Gameplay execution and rendering add work.
The measured local Debug build has sanitizers enabled. Steady detection cost:

| Bodies | Grid spacing | All pairs | Axis sweep |
| --- | --- | --- | --- |
| 1,000 | 40 | 19.221 ms | 0.895 ms |
| 4,000 | 40 | 301.030 ms | 4.369 ms |
| 1,000 | 12 | 19.348 ms | 1.829 ms |
| 4,000 | 12 | 307.298 ms | 12.612 ms |

The detector sorts bounds along the widest axis and rejects separated bounds
before testing circles. Bounds are computed once per body, with conservative
rounding margins for extreme coordinates. Canonical entity-pair sorting retains
the existing event order and overlap history. A brute-force reference in
`test/combat.c` checks varied masks, radii, negative and extreme coordinates,
vertical layouts, invalid values, event order, and persistent overlaps. The full
`make test` suite passes after this change.

To reproduce the Debug measurement after `make build`, run
`cmake --build build --target contact_benchmark`, then `build/contact_benchmark`.

## Authored enemy pursuit

`EnemyBehavior` handles Tick with a 10-node main graph and an 8-node pure
Move Toward function. It finds the nearest player
category within 2,000 world units, branches on Found, normalizes target minus
self, and clamps speed times delta to the remaining distance. Enemy's authored
Movement speed is 90. No target leaves its position unchanged.

`Find Nearest Entity` is a generic contextual read with position, radius,
category bits, and an excluded handle as inputs. It returns Found, Entity,
Position, and Distance. The radius is inclusive; equal distances use full entity
handle order. Pending destruction and nonfinite positions cannot win selection.
Its world-read effect keeps behavior execution serial while reading mutable ECS
state. It currently scans matching components, so many querying enemies still
need a separate performance measurement and possibly a shared spatial index.

`Normalize Vector 2` returns zero for zero or nonfinite vectors. Double-precision
length calculation preserves directions for extreme finite float magnitudes.
The contextual-read callback contract now permits multiple typed outputs; the
older no-input read callback retains its single-output contract.

Reviewing cached spatial results exposed a shared allocator defect. Arena growth
used to move earlier allocations, leaving cached struct values with stale
pointers. Arenas now retain aligned blocks, keep allocation addresses stable,
and reuse those blocks after reset. Regressions preserve and reread earlier
values after growth, including a cached query position.

New tests cover query filtering and result fields, vector extremes, and pursuit
in scene and isolated preview, including absent targets, diagonal speed, range,
coincident targets, target replacement, and arrival without overshooting. The
native editor scenario also opens EnemyBehavior and checks its arranged camera
and absence of asset-preview errors. The full `make test` suite passes with
sanitizers, including the native editor and resize scenario. The inspected
initial capture opened at 47% zoom. The revised default workspace places Graph
above Scene and initially improved pursuit to 76%. Its new local function and
shorter call label bring the main graph to 119%, with the function at 102%.
Double-click and Back navigate between these graphs without losing the caller
view. Larger graphs still need organizational tools beyond whole-graph fitting.

The pursuit tests also exposed two preexisting defects. Vector multiplication
now accepts a zero scalar without treating it as a missing struct pointer. JSON
numeric readers accept integer spelling for float fields and layout coordinates;
previously a radius written as `2000` silently loaded as zero. Direct/pull vector
checks and numeric serialization regressions cover these fixes.

## Automatic weapons

Player now attaches AutoWeaponBehavior alongside its manual Fire behavior. The
28-node automatic graph finds the nearest enemy within 600 units, accumulates
elapsed time only while a target exists, and consumes complete cooldown
intervals while retaining the remainder. The graph clamps cooldown to at least
0.01 seconds. Pending spawn batches remain bounded by the entity host.
The full `make test` suite passes after these changes, including native graph
opening, complete pin visibility, playback controls, and resize verification.

The graph captures origin and normalized aim before Spawn Prototype. Those
Set Variable output values belong to the activation; the continuation does not
reread a target or shared aim variable after yielding. Projectile initialization
sets position, direction, and weapon damage, preserving its other prototype
fields. Damage and cooldown still come from Weapon.

`test/auto_weapon.c` exercises both hosts: no-target time, fractional cooldown
remainder, right/left/diagonal/zero aim, multiple intervals, zero and negative
cooldown, changed projectile defaults, and target deletion plus owner movement
while initialization is paused. Breakpoints are configured on the authored
fixture before publication, then enabled on the test instance only when needed.

A later generic prototype-default read should derive exact range from projectile
speed and lifetime instead of repeating range in the weapon graph. Wave spawning,
enemy attacks, upgrades, piercing, chaining, weapon composition, and effects
remain unfinished. These authored graphs are getting too large for comfortable
editing at overview zoom. Direct function navigation and extraction of selected
pure calculations are now implemented. Execution graph extraction and further
authored grouping remain needed.

## Function extraction

Selected pure calculations can be extracted into a local function through the
Graph or node context menu. Extraction works on a replacement asset, so failed
validation leaves the document untouched. Typed boundary ports preserve incoming
conversions and outgoing connections, including shared inputs and arrays. The
editor records the replacement as one Undo step. Existing function navigation
opens the result with automatic arrangement and returns to the caller's view.

This first version excludes execution nodes, state reads, calls, and split pins.
Configure Shot and Check Firing Interval are candidates for simplifying the
automatic weapon graph. They have not yet been extracted in that authored asset.

Single-field component conversions now remove the Movement Break node from
EnemyBehavior. Its main graph has nine nodes; movement speed reaches the local
function through an implicit conversion. Tests verify float and vector fields,
arithmetic binding, nominal/container rejection, compilation, and serialization.
Both gameplay hosts and native editor extraction/navigation pass `make test`.


## Direct component fields

HealthBehavior and AutoWeaponBehavior now wire fields directly from their source
component outputs. Whole-component connections remain available on the same
nodes. Set outputs retain their captured value even after a later write to the
component. Collapsing field rows keeps every connected pin and wire.

Projection metadata is serialized by aggregate pin ID and reflected field name.
The interpreter reads the cached aggregate; compiled systems lower projections
through their existing field-extraction instructions. Both execution and compact
dataflow systems are covered, with backend availability checked explicitly.

Verification passes in the normal sanitizer-enabled `make test` run and in an
isolated SLJIT-enabled build using the pinned backend revision. Coverage includes
copy/paste remapping, preserved whole-value connections, saved graph roundtrips,
Set snapshots after later writes, compiled system execution, and native
expand/collapse/re-expand with non-overlapping node bounds. The normal build's
backend setting remains unchanged.


## Weapon graph organization

AutoWeaponBehavior now has 24 top-level nodes and three pure helpers: Direction
To, Check Firing Interval, and Offset Position. The offset is a call input with
default 32. Existing spawn and component-write node IDs remain unchanged.
Gameplay verification additionally checks elapsed remainders directly and changes
shared Shot Damage while initialization is paused; the projectile still receives
its captured damage. `make test` passes after grouping.

The inspected overview remains small at 31%, now including the complete curved
loop wire. A useful next boundary is Initialize Projectile, followed by Fire
Projectile once executable calls can suspend. Executable functions and macros now suspend with their active child and resume
parent execution after Return. Parameters and return values are copied.
Spawn initialization waits for the complete caller continuation before Begin;
initializer-only ticking advances Delay while ordinary events remain blocked.
Scene pause and debugger pause still hold initialization. Pure functions reject
latent operations.

Function signature field projections also need a separate audit so
Direction parameters can expose their fields without becoming extra arguments.


## Resumable call verification

`make test` passes after the resumable-call changes, including native editor
interaction and headless pane resizing. New coverage exercises delayed functions,
nested macros, repeated inputs and breakpoints, child errors, missing Return,
cancellation, host token completion, and struct variables surviving Stop and
activation teardown. Both scene and prototype hosts check Spawn → Delay → Return
→ parent setup → Begin, including debugger holds and cancellation.

Instance variables own their values in per-variable arenas. Assignment copies
before releasing the old value; graph Get reads copy into execution storage.
Completed child calls can therefore be reclaimed without invalidating variables
or captured return values. Parent failures take precedence over child status.
Delay stepping preserves the existing rule: execute one continuation activation
at the deadline, then pause.

AutoWeapon now uses that structure: eight event nodes, 12 burst-function nodes,
and nine projectile-function nodes. Only Elapsed remains persistent. Origin,
Direction, and Damage are captured once by Fire Ready Projectiles and passed to
Fire Projectile on each loop iteration. Both-host tests change actual weapon
and owner state while initialization is held, check every shot in that burst,
and verify the next burst recaptures the changes.

The main graph opens at 120% zoom, up from 31%; the burst function opens at 77%.
Native checks cover visible pins and complete wire bounds in all three graphs.
Fire Projectile now exposes Direction fields on Function Entry and has eight
nodes. The entry retains its original parameter pins and x/y projection IDs.
Pure and executable calls, including delayed results, support field outputs;
projected rows are excluded from parameter binding and call signature creation.
CMPL-23 reports stale call signatures. Native expansion chooses an output when
several are eligible and preserves connected fields. The full `make test` run
passes, including both-host gameplay and native interaction checks.

Function navigation is now visible in node headers, caller breadcrumbs, and a
local graph picker. Entity values use host-resolved actor/prototype names after
checking runtime world identity. Native tests cover nested navigation, restoring
the root camera/selection, and picking both a function and Events; `make test`
passes. A shared host flags catalog now names Player, Enemy, and Projectile for query
inputs and Collider properties. Unknown bits survive edits, and native Undo
coverage passes. Project-defined categories remain future work. The left navigation is now consolidated into one Files/Outline navigator with
scene/document access. Native selection, visibility, focus, and full-height
layout checks pass. Empty Inspector space and loop routing remain gaps against
the requested polished editor.


## Player contact damage

Player now attaches PlayerContactBehavior. It has 11 event nodes, a 13-node Check
Enemy Contact function, and a five-node Apply Contact Damage function. Remaining
is a persistent float initialized to zero. Tick subtracts Delta Time and clamps
to zero. Contact Begin checks cooldown, valid Collider and Health components,
the Enemy category bit, and positive health. It sets Remaining to 0.5 before
sending 10 Damage to Self. HealthBehavior remains generic, so other Damage events
bypass the body-contact cooldown. Has Any Flags is a generic pure unsigned mask
predicate used by the graph; combat rules remain authored.

This follows Neon-Swarm src/combat.c's begin-contact processing and
src/physics.c's existing-contact exclusion, plus damage 10 from
data/configs/combat.json and cooldown 0.5 from data/player/player.json. Staying in
contact never repeats damage; an early re-entry is discarded. Both-host tests
cover simultaneous contacts, continuous overlap, early and late re-entry,
dead/friendly filtering, generic damage bypass, lethal cleanup, and fresh player
state. Engine pause is covered separately; prototype reset uses stop/relaunch.
`make test` passes with the attached behavior registered in Player fixtures.

## Current scope

The user explicitly excludes shields. Prioritize movement, visual presentation,
projectiles, enemies, particles, and editor usability. Reimplementing every
Neon-Swarm feature is not a completion requirement; waves, upgrades, and weapon
variants are optional extensions rather than prerequisites. Existing source
behavior is reference material, not a mandate to reproduce every subsystem.

The editor should feel like a professional IDE: familiar navigation and readable
graphs, with Apple's restraint, consistent spacing, and responsive feedback.
Judge new features by how much they improve creating and playing a small game.


## Particle feedback

ProjectileHitBehavior now reads Damage directly from its component output and
emits an impact burst before destroying the projectile. Emit Particles accepts
Position, Color, Count, Speed, and Lifetime; its storage belongs to the running
world. Effects survive destruction of the emitting entity, freeze with gameplay,
and clear on scene restart or prototype reset. The renderer fades and shrinks
them over their lifetime. Capacity is bounded to 2048 particles per world.

This is a small authored effects primitive, not a requirement to reproduce all
Neon-Swarm effects. Color is stored as packed RGBA and edited using a swatch, picker, or Hex input.

Validation: `make test` passes, including native graph navigation and resize,
both-host impact emission, paused effects, expiry, and reset. The compact command
bar and authored impact graph screenshots were inspected.

### Next playable slice

The default scene currently seeds one actor per prototype, which is useful as a
showcase but does not form a repeatable encounter. The next core gameplay task is
an authored, nonvisual Encounter prototype with WaveBehavior. Existing health
cleanup, pursuit, automatic weapon, projectile lifetime and impact particles are
already executable. The wave can use existing generic nodes and local functions:
accumulate Tick delta, find the player, spawn eight enemies at 1.5-second intervals
on alternating sides, and stop spawning when no player remains. Initialize each
spawned Transform in a local function and return promptly. Restart should reset
the controller's variables. Keep other prototypes available in Files instead of
instantiating each as a default actor. Preserve an explicit editor test fixture
for existing multi-actor interaction coverage when changing the default scene.

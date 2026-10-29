# Author a game with Chibi

The game's `project.kdl` selects its `content` directory and starting scene.
`chibi-compile` and `chibi_ctl` both read assets from that directory.
Project format 10 uses `.chibi` executable source for behaviors, callable
libraries, and systems. Scenes, prototypes, shaders, models, and other declarative
assets use KDL. Studio, text editors, and AI agents edit these same asset files.
The copied player deployment lives in the game's ignored `.chibi/runtime` directory.
The compiler, Studio, and controller are separate authoring tools; they do not
ship inside the game runtime.

Each game lives in its own repository. Studio owns its starter and shell assets
in `chibi-studio`; [examples](examples.md) lists the checked-in projects to learn
from. Pass the project root to an installed authoring tool rather than mounting a
game under the engine checkout.

## Start a project

Scaffold files with the controller instead of copying another project. `new_project`
writes `project.kdl` and an empty `content/scenes/Start.kdl`; `new_asset` writes an
empty asset of one kind in its content folder, the same form Studio's New menu
starts from, and never overwrites a file. A taken name fails with `asset_conflict`,
a bad name or path with `invalid_request`, and a disk that refuses the write with
`io_error`:

```sh
printf '%s\n' '{"contract":1,"operation":"new_project","path":"tetris","name":"Tetris"}' | chibi_ctl
printf '%s\n' '{"contract":1,"operation":"new_asset","kind":"behavior","name":"Board"}' | \
  chibi_ctl --project-root tetris
```

[`test/fixtures/terrain-lab`](../test/fixtures/terrain-lab/README.md) is a small
playable game made this way.

Kinds are `scene`, `prototype`, `behavior`, `event`, `system`, `library`,
`material`, `shape`, `field`, `shader-library` and `model`. Before running content,
`validate_project` checks the manifest, its start scene, its application title
and icon, and every asset. Markup in `content/ui` must be a regular file:
`chibi_ctl` never follows a link, so a linked document is refused. The reply's
`problems` list only the assets with errors or warnings, each diagnostic naming
what is missing (`Unknown prototype 'Block'`) and, when known, its line. While a
model and a shape share a name there is no catalog, so it lists no asset
problems until one is renamed. Once nothing else is wrong, it prepares the
project as `chibi-compile` does and lists what preparation refuses, such as
resources that together pass the player's 4,096 records or 64 MiB, so a valid
project compiles. It never changes files: a project saved by an earlier build
fails until `chibi_ctl --project-root <root> --migrate` upgrades it.
Version 30 rewrites each `on` handler as `on X :: (Field: Type)`, listing the
payload fields it reads; the older `on X(a)`, `on X as e` and `on X` forms no
longer parse. The migration refuses a handler that it cannot rewrite
exactly, such as one that reads the whole `event` or whose alias an action
reads; rewrite that handler by hand and migrate again. It does not see events
that a game's own native table declares, so write those handlers in the named
form first. Version 31 removes the step globals `dt`, `delta` and
`delta_time()`: a read in an `on Tick` handler or a system becomes a listed
`dt`, and the migration refuses one anywhere else, where the step must be
passed in.
Version 38 gives systems an implicit, read-only body-local `dt` and flattens
their query headers. It migrates live component bindings to `^T`, merges
`join` entries into the binding list, and replaces `without` and query `where`
clauses with `if` entries. Behavior handlers still list their payload fields.
The migration refuses ambiguous optional whole-component reads and body
`query expression;` declarations; rewrite those sources before retrying.
A prototype is the `name` it declares, whatever its file is called: scenes and
other prototypes' `(array)includes` find it by that name in the player and in
`chibi_ctl` alike, and two prototypes that declare one name, or a name over
1024 bytes, are refused.
`new_asset` names a new prototype's file after it. `--format` checks a
prototype with includes but keeps its text as written instead of folding the
included components in.

## Tilemap layers

Put a `Tilemap` on one transformed entity to draw an atlas-backed grid.
`image` names the atlas, `columns` and `rows` give the grid dimensions, and
`frame_width` and `frame_height` set both cell size in project units and atlas
frame size in pixels. `cells` contains whitespace-separated row-major frame
indices; `-1` leaves a cell empty. The entity origin is the top-left corner and
rows increase along local +Y. Orientation rotates the whole layer.

Tilemaps render in scene previews without spawning cell entities. Their `layer`
orders them with sprites. A behavior can query an occupied frame with
`tilemap_frame_at(entity, point)` or count occupied cells overlapping a world
rectangle with `tilemap_occupied_in_box(entity, minimum, maximum)`. Decorative
layers need no behavior or collision setup. See the rotated `TileMosaic`
prototype in `test/fixtures/sprites`.

## Light 2D scenes

A 2D scene draws its sprites, shapes and tilemaps unlit, in the colors they are
drawn with, until an entity holds `HRC2D`. Holographic radiance
cascades then light it: emissive materials light their surroundings, absorbing
materials cast shadows, and light bounces off lit surfaces. Only the first such
entity counts. `ambient` is the share of the unlit colors that stays: 1 keeps
them as drawn and adds light to them, and lower values darken what no light
reaches. `intensity` scales how bright emissive surfaces look, and `resolution`
sets the detail of the light grid. `sky` lights a side view from above: open
ground receives its color, and absorbing materials block it like other light,
so the ground under an overhang and the inside of a cave stay dark. Studio adds the component with `ambient` and
`intensity` at 1 and `resolution` at 0.5, but KDL initializes an omitted field to
zero before range validation (`resolution` becomes 0.1), so write every setting
a scene needs. See the `Lighting` prototype in `test/fixtures/sprites`.

## Declare components

A game declares its own component types under `content/components`, one `.chibi`
file per component. Fields are laid out in order with natural alignment and start
zeroed, then clamped into any declared range. A field's type is a script scalar
(`bool`, `int`, `number`, `f64`, `u8`, `string`, `entity`, ...), an engine
struct such as `vec2`, or a
measure: a `number` with an optional range and an optional unit. The `//`
comments directly above the component and each field describe them.

```code
// Stored energy that refills over time.
Charge :: component {
    level: 0..100 charge
    // Charge gained per second.
    rate: charge/s
    delay: s
    full: bool
    drift: vec2
}
```

A range such as `0..1` or `-1.5..2` is an inclusive stored-value contract. Scripts,
asset loads, aggregate constructors, prepared players and native engine writes
clamp values outside it to the nearest endpoint. Bounded `number` fields keep
fractions: assigning `2.75` into `0..10` stores `2.75`. The conversion `int(2.75)`
truncates toward zero. Each assignment clamps independently; two saturating
updates cannot be reassociated. Floating arithmetic may overflow to infinity
before the store; positive/negative infinity clamp to the high/low endpoint.
NaN becomes the field's validated default, or zero clamped into the interval
when no default is declared, with a development diagnostic and no exception.
KDL text still rejects nonfinite literals before loading; typed asset values
and prepared streams use the same validation as runtime writes.

Native `NODE_RANGE` declares the same contract. `NODE_RANGE_DEFAULT` supplies a
finite NaN fallback. `NODE_RANGE_HINT` is only a slider suggestion. The existing
friction sliders, 3D restitution, appearance antialias width, optical absorption,
material edge light and HDR emission use hints because their native consumers
permit values above the slider's maximum. Other existing native range
annotations are contracts.

Native writes through mutable C row pointers and the execution view bypass
validation until `ecs_component_modified` commits the row. Reflected write APIs
and component insertion validate immediately; constructors and scene preparation
validate newly published values. Copying an aggregate preserves its value.
Range metadata alone is not proof about a borrowed C pointer. Compiler range
facts may originate only from a validated value; unchecked pointer loads do
not authorize check elimination or arithmetic reassociation. Component range
changes invalidate the linked storage contract, and prepared players retain
their ranges. Reloaded scripts and whole-component snapshots use the world's
registered component schema while a replacement catalog is being prepared.
Generic local values and nested layouts use the current nominal type catalog.
Prototype operations instead resolve nested layouts through their registry for
the duration of each transaction; the world retains copied values, not that
resolver. Replacing physical layouts while generic values are live remains
outside the existing runtime contract.

A unit such as `u/s`, `rad`, `u/s^2` (shown as u/s²) or `banana/basket` labels the
value. Unit names mean units only in a field's type, so `s` stays a free name in scripts.
A unit standing alone must be a common one (`u`, `px`, `m`, `s`, `ms`, `rad`,
`deg`, `Hz`, `fps`, `bpm`, `EV`, `dB`, `kg`, `N`), so a misspelled type name
is reported instead of becoming a unit; after a range or with a `/`, any name
works.

Units are nominal dimensions, erased before the game runs. `*` and `/` combine
them, so `charge.rate * dt` is in `charge` because
`dt` and `delta` are in seconds. `+`, `-`, `%`, comparisons, assignment and `bind` need the
same unit on both sides. A plain number literal takes the unit it meets in a
sum or comparison, and as a factor it may carry any unit, so `3 * dt` is not
checked. A value nothing measures, such as a local set to `0` or an unannotated
function result, has no dimensional proof. Measured initializers propagate their units
through locals, constants and state; every possible `fallback` value is checked.
Function parameters and results can declare erased units, for example
`Distance :: (speed: u/s, elapsed: s) -> u`. Calls, defaults, returns and writes
through pointer parameters check these contracts, including across libraries.
Unannotated signatures remain dimensionally unknown. The engine's own fields carry
units too: a `Movement.speed` is in `u/s` and a collider's `radius` is in `u`.
Proven mismatches and malformed referenced unit metadata are compile errors;
`validate_project` reports their source locations and prevents execution:

```code
Recharge :: system(charge: Charge) {
    charge.level += charge.rate * dt // charge
    charge.level += charge.rate      // Units differ: charge += charge/s
}
```

Valid custom names such as `charge` and `banana/basket` remain supported. Units
with different names are distinct; there are no implicit conversions between
`m` and `u`, or aliases from `Hz` to `1/s`. Fix a mismatch by correcting the
declared units or the calculation, such as multiplying a rate by `dt`. Plain
numbers still infer `number`, a 32-bit float; use `int` explicitly when integer
semantics are intended. Neither absent unit information nor an editor hint
authorizes arithmetic optimizations.

Prototypes, scenes, behaviors and systems then use `Charge` like an engine
component. A name may not reuse an engine type. The compiler stores the
declarations in the prepared artifact, so the player needs no source.
`chibi_ctl` formats, validates, describes and runs them like engine components.

## Authored prototypes and relationships

Files under `content/prototypes` can declare prototypes in `.chibi`. Existing KDL
prototypes remain readable. Component entries supply sparse defaults, and ordered
bases compose before the declaration's own overrides:

The following declarations belong in separate files:

```chibi
// content/prototypes/Enemy.chibi
Enemy :: prototype {
    Health { current: 100, maximum: 150 }
    behaviors: [EnemyAI]
    children: [{name: "enemy", prototype: Child}]
    links: {Targets: child.enemy}
}
// content/prototypes/Elite.chibi
Elite :: prototype(Enemy, Armored) {
    Health { current: 200 }
}
```

Each file declares one prototype. Referenced prototypes, behaviors and components
must exist. Defaults support literals, static names, arrays and objects; executable
calls are rejected. Overrides replace authored fields while preserving unspecified
inherited defaults. Component/field spellings must resolve uniquely.

Relationship declarations live under `content/relationships`:

```chibi
Targets :: relationship {
    exclusive: true
    acyclic: true
    traversable: false
    on_delete_target: Remove
}
```

Boolean traits default to false. `traversable` requires `exclusive` and implies
acyclic traversal. Target deletion defaults to `Remove`; `Delete` and `Panic`
select the existing ECS policies. Relationships currently have no payload data.

Initial prototype links address instance-local children, such as `child.enemy`.
A nonexclusive relationship may name several targets in an array. An explicitly
named relationship overrides inherited links for that relationship. Arbitrary
scene-global initial targets and queryable embedded components are not supported.

During execution, `add_pair(source, Targets, target)` and
`remove_pair(source, Targets, target)` stage mutations through ECS commands.
`has_pair(source, Targets, target)` reads committed state. The relationship is a
typed symbol reference; a string or numeric ID cannot substitute for it. Mutations
become visible at the normal structural command boundary, including query refresh
and target-deletion handling. Prepared projects retain descriptors and initial
pairs for compiler-free loading.

Relationship iteration also reads committed edges. Removing an existing edge and
immediately adding it back in the same handler is rejected as a duplicate: the
removal is still queued. Retain the target and re-add it in a later handler after
the removal commits.

## System queries

A system uses the same typed description as an explicit query iterator.
Its flat header binds root components, relationship generators, components
from other sources, pure intermediate values and `if` filters. Reads and
writes are inferred from the evaluator and body.

```code
Home :: system(
    position: ^Transform,
    velocity: Velocity,
    aim: ^Transform? from Targets,
    if (Player || Ally) && !self.Frozen && !Targets.Frozen,
) {
    if aim {
        position.x = aim.x
        position.y = aim.y
    }
}
```

`Targets` is an authored or engine relationship exposed in the script catalog.
A scalar `from Targets` binding requires an exclusive relationship and follows
its one target. Adding `?` retains roots without that target or component.
A value binding copies the component for the row and is read-only. A pointer
binding accesses the stored component. Guards must prove optional presence
before fields are read. Several roots may share a target, so sourced writes
are not assumed independent.

`Frozen` and `self.Frozen` test root membership; `Targets.Frozen` tests the
exclusive target. A missing target makes the latter false. `has(Targets)`
tests relationship existence, including relationships with multiple targets,
without generating extra rows. `has(Component)` tests component membership.
A catalog component takes precedence if a relationship has the same name.

Ancestor sources are explicit:

```code
Inherit :: system(
    position: ^Transform,
    team: Team from ancestor(ChildOf),
) {
    position.x = team.spawn_x
}
```

`ancestor(ChildOf)` selects the nearest ancestor carrying the requested
component. `self_or_ancestor(ChildOf)` checks the root first. Traversal requires
an exclusive, traversable relationship; `ChildOf` is built in. Optional sources
use `?`. Membership guards use the same notation, such as
`if ancestor(ChildOf).Team`.

A header generator gives each relationship target its own system row:

```code
DamageTargets :: system(
    damage: Damage,
    target: entity in self.Targets,
    health: ^Health from target,
    if health.value > 0 && changed(damage),
    amount := damage.amount,
) {
    health.value -= amount
}
```

A generator can use an earlier variable, for example `ally in target.Allies`.
Independent generators form all matching combinations. Eight and four targets
produce up to 32 rows per root. There are at most eight named entity variables.
Forward references and duplicate names are errors. An optional component does
not make its generator optional: a root with no targets produces no rows.
`has(target.Health)` tests membership on that entity without generating rows.

`in` generates identities; `from` selects a component on an existing identity.
The type on a generator may be omitted and is inferred as `entity`.
Intermediate bindings use `name := expression` or `name: Type = expression`.
Entries evaluate in source order; a false `if` skips the remaining entries.
Every system has an implicit read-only local `dt` in seconds in its body.

An explicit query uses that same list:

```code
matches := query(
    damage: Damage,
    target in self.Targets,
    health: ^Health from target,
    if health.value > 0,
)
for row in matches {
    row.health.value -= row.damage.amount
}
```

The description captures outer values at construction. Each loop owns its
iteration state; nested and repeated loops use independent cursors. Cleanup
also occurs on early exits. Query rows expose their named bindings as fields.
Managed component references resolve current storage while their original
component incarnation exists. Component removal, entity destruction or world
destruction permanently invalidates the target; re-addition has a distinct
identity. Invalid access stops the activation with a runtime diagnostic, which
the embedding caller can inspect. Authored code has no catch or checked recovery
operation yet. Keeping a reference does not keep the component attached to its entity.
A scalar `from` binding keeps the selected component through suspension.
Relationship edits affect later admissions; an existing pointer keeps its
component identity.

A body loop over `self.Targets` remains available when one system invocation
should cover all targets. That streaming relationship loop cannot suspend or
change entity structure while open. Reactive `bind` destinations remain on
the root; target or ancestor writes belong in the system body.

Each complete root/target tuple owns independent invocation locals and suspended
work. Ordinary locals survive suspension without `retain`; they end when the
invocation ends. Explicit `retain` belongs to the system registration and is
shared across all its tuples. Use components for durable entity state.
Entity generations participate in identity: a tuple absent at the next membership
refresh discards its locals and cancels its work; a later entity reusing a slot
cannot inherit them. Removing and restoring the same relationship entirely
between refreshes preserves the tuple. Reordering relationships preserves
surviving continuations. The cost grows with retained matches and live
continuations. The query cache's byte cap does not cap this separate storage.
Use a body loop for one invocation covering all of a root's targets.
Proven stateless, synchronous generator systems can stream through one
temporary row instead of retaining an instance for every tuple. Each admitted
body uses an activation. Serial synchronous invocations can reuse one idle
activation per program, retaining at most 64 KiB of fixed frame storage. Payloads,
host banks and nested calls still release their owned storage between invocations.
Suspended, inspected, cancelled and failed work does not return to this cache.
Prepared system batches still need separate activations for simultaneous rows.
`execution_program_memory_usage` reports idle `activation_cache_bytes` separately
from live activation headers.
Before bodies run, streaming systems stage admitted rows and their binding
values so body writes cannot change later rows' admission or snapshots within
the same update. This storage grows with the admitted rows and their values.

### Value and change filters

`if` entries accept Boolean value expressions using component bindings,
constants, supported globals and read-only helper functions. Preserve ordinary
short-circuit order: `if aim && aim.x > 0` guards an optional binding; `if
aim || aim.x > 0` does not. Header predicates cannot write, suspend, perform host
side effects, consume randomness, or read system state or event payloads. These
restrictions also apply through helpers. Put conditions on persistent system
state inside the body. Registration-owned retained state exists independently of
query admission, including when no rows match.
Pass query bindings and captured outer values to header helpers as explicit
arguments; a helper cannot implicitly read the surrounding query or invocation.

`changed(damage)` observes published writes to the whole bound component, not
only the fields used in the body. A same-value write counts. A new matching row
is eligible once. Each system row has its own observation history, so another
system does not consume its notification. Direct native pointer writes become
observable when their owner calls `ecs_component_modified`.

The ordered dynamic `if` entries gate new work in source order. Required
header bindings and generators define structural membership; membership tests
inside a mixed value/change expression belong to that gate. A false gate does not destroy
existing invocation locals or cancel an already suspended body. Structural tuple loss
does. Changes are consumed only when new work is admitted, so a busy row does not
lose a write while waiting. Writing a component watched by the same system can
make that row eligible again on a later update.

Change tracking stores an eight-byte revision per world slot for each tracked
component, shared across systems, plus source/revision history per retained row
and `changed` term. `execution_program_memory_usage` reports logical instance,
activation, state-bank, query-history and index bytes separately. Allocator
overhead and nested buffers are excluded from those header counts; shared ECS
tracking bytes should not be summed across programs.

Component values and predicate results are not part of the structural query
cache. They are checked against current execution data. Cascade ordering and
arbitrary fixed-entity sources remain native-query features.

Native queries use `ecs_query_plan_new`, `ecs_query_begin_plan` and
`ecs_query_plan_free` to reuse a world-bound logical plan. The world must outlive
the plan, and only one iterator may use a plan at a time. Between iterations,
component registration, entity growth, pair edits and component membership
changes are supported. Authored systems retain this plan across updates.
Completed iterations cache root membership and provider/variable bindings, never
component values. Relevant component additions/removals, `Disabled` membership,
relationship edits and relationship ordering invalidate that cache. Unrelated
component changes and ordinary field writes retain it. Plans over all entities
also track entity creation/destruction. A rebuild refreshes storage references,
candidate selection and ancestor caches; a hit reads current component values
through current handles. Ending an iteration early never publishes a partial
result as a complete cache.

Simple single-component queries use their direct index unless `Disabled`
filtering requires additional work. Cached row payload is limited to 1 MiB per
plan; oversized joins keep streaming. A multi-target query whose cardinality is
not known in advance can require a counting pass before reserving enough rows.
This bounds retained row storage; it does not impose a fixed runtime bound on
an arbitrarily large query.

Candidate selection estimates both matching rows and sparse bitset scanning.
Pure predicates move before later variable generators as soon as their required
variables are bound; generator order stays intact. `ecs_query_plan_stats`
reports cache hits/builds/overflows, candidates, predicate tests, source lookups,
binding steps and retained row bytes so callers can inspect actual work.
The native API also exposes pair variables, Boolean
scopes, `ECS_UP`, `ECS_SELF_UP` and explicit cascade ordering.

## Scene entities

A scene entity starts from its prototype's components and behaviors. Its
`components` override the prototype's values. A `behaviors` list replaces the
prototype's behaviors, and an empty `(array)behaviors` removes them all; leave
the list out to keep the prototype's behaviors.

```kdl
(array)entities {
    - name="Player" prototype="Player"
    - name="Decoy" prototype="Player" {
        (array)behaviors
    }
}
```

An entity's `children` are entities placed under it: each is `ChildOf` its
parent, and its `Transform` is relative to the parent's placement. A name must
be unique among its siblings and cannot contain `.`; a path such as
`Ship.Turret` names a nested entity, for example as a scenario target.

```kdl
(array)entities {
    - {
        name "Ship"
        prototype "Ship"
        (array)children {
            - {
                name "Turret"
                prototype "Turret"
                components {
                    Transform x=0.0 y=-12.0
                }
            }
        }
    }
}
```

A prototype lists `children` in the same shape, and each instance of it,
whether placed in a scene or spawned, gets the whole tree. A prototype that
includes another gets the included prototype's children before its own. A
child cannot use the prototype it belongs to, directly or through its
children. Studio cannot load a prototype with children yet, as with
includes, and snapshots name only the scene's own entities.

```kdl
name "Ship"
components {
    Transform x=0.0 y=0.0
}
(array)children {
    - {
        name "Turret"
        prototype "Turret"
        components {
            Transform x=0.0 y=-12.0
        }
    }
}
```

## Edit and verify

1. Read the existing asset and its dependencies. Use the shared SDK's reflected
   components, functions, and types to choose available capabilities.
2. Edit the saved `.chibi` or KDL source. Keep game rules, scene composition,
   visual choices, and tuning in the game project.
3. Format the project and validate changed assets with the native controller.
   Resolve loader/compiler diagnostics before running the content.
4. Inspect saved content or run a bounded deterministic scenario. For visual
   changes, use the hidden-window capture route in [agent execution](agent-execution.md).

Use `chibi-compile` from a pinned installed SDK and `chibi-studio` from the
corresponding Studio distribution. The native controller is a separate authoring
tool; when it is supplied on `PATH`, formatting and inspection use `chibi_ctl`.
It is not part of the deployed player package.

From the game root:

```sh
chibi_ctl --project-root "$PWD" --format
chibi_ctl --project-root "$PWD" --format --check
chibi-compile --project-root "$PWD" --output game.chibip
./.chibi/runtime/bin/chibi-player game.chibip --resource-root "$PWD"
```

Keep `game.chibip.resources.json` and all declared resources with the prepared
artifact. Compilation resolves authored source and dependencies before deployment;
the player loads the prepared artifact and its declared resource closure. The
installed compiler and deployed player must satisfy the same format and native
capability requirements. Package integrity and installed-consumer linking alone
do not establish rendered player, Studio interaction or browser acceptance;
validate each affected consumer and report the workload actually exercised.

The controller accepts one plain JSON request per stdin line and produces one
JSON response per line. Requests use `contract: 1`, an `operation`, and the
operation's fields at the top level. For example, validate a saved behavior:

```sh
printf '%s\n' '{"contract":1,"operation":"validate_asset","path":"content/behaviors/Projectile.chibi"}' | \
  chibi_ctl --project-root "$PWD"
```

Asset paths in requests and replies start with `content/`, which stands for the
folder `project.kdl` selects: `content/behaviors/Projectile.chibi` is
`behaviors/Projectile.chibi` there.

Change the path to an asset in your project. A request failure returns a nonzero
exit status. Inspect the response's diagnostics as well as its status. Retained
operations include `read_asset`, `validate_asset`, `validate_project`,
`new_project`, `new_asset`, `catalog`, `inspect_graph`,
`inspect_system`, `inspect_ui`, `find_references`, `call_hierarchy`,
`run_scenario`, and `runtime_snapshot`. The native contract definitions in
`src/automation/controller.c` describe each operation's fields. The controller
and shared SDK supply inspection, validation, and execution; asset changes are
file edits. There is no MCP server or agent graph-mutation transport.

A short deterministic scene run uses saved content:

```sh
printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"scene","name":"Main","frames":1}' | \
  chibi_ctl --project-root "$PWD"
```

Inspect `behavior_failures` and the deterministic state evidence in the response.
Choose affected-contract checks using the [testing instructions](testing.md).
A short sample run does not establish full runtime or rendering coverage.

## Switch scenes

Scripts start another scene with `load_scene(Scene: "Level2")`, restart the
running one with `reload_scene()`, and read its name with `current_scene()`. A
scene's name is its file name in `content/scenes` without `.kdl`. A request
takes effect after the frame: the host destroys every entity, discards
uncommitted save changes, keeps committed save data, and starts the scene, whose
Begin runs before its first frame. Time, held input and audio carry on.
`chibi-compile` prepares every scene in `content/scenes`, so the player can load
any of them, and refuses a scene that binds one `GlobalBinding` name to two
entities. Scene scenarios (`run_scenario`, `runtime_snapshot`,
`profile_scenario` and recordings) follow requests the same way. Studio play
mode cannot load scenes yet, so a request there fails with "This host cannot
load scenes".

## Play sounds and music

Put mono or stereo WAV and Ogg Vorbis files sampled at 44.1 kHz in
`content/sounds`. A WAV holds plain PCM (8 to 32-bit) or float samples;
WAVE_FORMAT_EXTENSIBLE, surround, Opus, other sample rates and cut-short files
fail preparation, since the mixer plays files at 44.1 kHz without resampling.
Preparation and scenarios also decode every Ogg file with the players' Vorbis
decoder and refuse one that yields no audio. `validate_project` lists the first
sound they would refuse as a project problem. A sound's name is its file name
without the extension, so `content/sounds/jump.ogg` is `"jump"`. Scripts play
one with `play_sound(Sound: "jump")`, and set its Volume and Pan from 0 to 1.
`play_music(Music: "theme", Fade: 1)` loops a track and fades it in. Asking for
the track that already plays changes nothing, so scenes that share a track keep
it running. `stop_music(Fade: 1)` fades it out. `set_master_volume`,
`set_music_volume` and `set_sound_volume` scale each bus from 0 to 1, and the
master bus scales the others. `play_tone` still synthesizes sweeps and noise
without a file. `chibi-compile` embeds every sound in the prepared artifact.
Scenarios hash every audio request, and `runtime_snapshot` counts the sounds and
tones played in `sound_cues`. Studio play mode cannot play sound files yet, so
`play_sound` and `play_music` fail there with "No sound named".

`get_audio_analysis()` reads the playback host's measured PCM. `Level`, `Bass`,
`Mid`, `Treble` and `Onset` are numbers from 0 to 1. Level is absolute RMS;
the approximate bands split at 250 Hz and 2500 Hz. Onset measures a rise in
energy, with no tempo or beat prediction. Levels have a 20 ms attack and 120 ms
release. Apply visual sensitivity in authored code. Stereo channels retain
their own energy, including when their waveforms have opposite polarity.

`set_audio_source(0)` analyzes the game output after its music, effects and
volume controls. `set_audio_source(1)` requests external audio. The browser
asks for a tab, window or screen on the current or next user gesture; select
one with audio sharing enabled. Support depends on the browser and selected
source. Native players select an installed loopback input such as Background
Music or BlackHole. Route the music player's output to that virtual device.
`CHIBI_AUDIO_INPUT` selects an exact recording-device name instead. The player
never selects the default microphone implicitly. PulseAudio monitors require
`SDL_AUDIO_INCLUDE_MONITORS=1` before launch. Captured audio stays in memory
for analysis and is neither saved nor played back.

`Source` reports 0 or 1. `Status` reports idle (0), waiting (1), ready (2),
unavailable (3), denied or cancelled (4), or ended (5). `Active` means recent
PCM arrived while the playback host was running; an active source can be
silent. Pausing, changing the source or resetting the session clears the
measurements. Headless scenarios have no playback samples and return zeros.
Keep capture state and visual responses separate from deterministic game rules.

## External URLs and pointer picking

`open_url("https://example.org/help")` requests an external browser page.
`request := await open_url(Url: "https://example.org/help")` also returns
`request.Accepted`. This means the enabled host queued its own copy of the URL,
not that a browser opened or loaded it. URLs must start with lowercase
`http://` or `https://`, have a nonempty host, and contain no credentials,
whitespace, control characters or backslashes. Other protocols are rejected.
Each request replaces the preceding request, including an invalid request.

The interactive player opts in. Headless tooling, hidden players and players
with `--frames` reject requests. Native players ask the OS to open the URL and
print an error if it rejects the handoff. Browser players consume the recent
trusted input's transient activation, even when a quick click or keyup finished
before the authored frame. If the request precedes activation, it waits only
for that same input's release.
Popups use `noopener,noreferrer`. Browser blocking is not reported as success.
The request expires after five seconds and cancels on a new keydown or
pointerdown, pause, scene change, page hide or shutdown. An expired request
never borrows a later gesture. Call `cancel_open_url()` when changing modes or
abandoning a provider choice.
Opening a provider neither captures nor authorizes access to its audio.

`get_pointer_ray()` returns `Active`, `Origin` and `Direction` in world space.
The player samples the ray through the displayed 3D image before the fixed
step. It is inactive outside that image, when input is unfocused or captured by
UI, and in hosts without a 3D pointer. Authored hit testing can intersect the
ray with a plane or pass it to `raycast_3d`. Input recordings retain the ray;
scenario keys can store `pointer { origin x=0 y=0 z=10; direction x=0 y=0 z=-1; }`.
Controller `input_timeline` entries accept the same `pointer` object with
`origin` and `direction` objects containing `x`, `y`, `z`. Omitting `pointer`
holds the previous ray; setting it to JSON `null` clears it.

## GPU particle fields

Put `ParticleField3D`, `Geometry3D` and `Shader` on one transformed entity in a
3D scene. Geometry must be a tetrahedron (`primitive=6`) or octahedron
(`primitive=5`) with positive dimensions. `Shader.material` names a pure
shader-library function as `Library.Function`. Its first arguments are
`Index:number`, `Count:number`, `Seed:number`, followed by named number or
`ShaderVec2/3/4` uniforms, with at most 32 arguments total. Its results are
exactly `Position:ShaderVec3`, `Size:number`, `Color:ShaderVec4`, in that order.
Position is local to the entity, Size scales the geometry, and Color is HDR RGBA.
Uniform defaults and `Shader.parameters` use the existing material parameter
path, including `set_material_parameter()`.

Set `ParticleField3D.count`, `seed`, and `time` explicitly. Shader Time reads
this component clock; it advances only when an authored behavior changes it.
Time must be finite and nonnegative. Numeric math, vectors, noise, mapping and
time/beat reads are available. Textures, ramps, scene influences, surface
coordinates and fragment derivatives are rejected. There are at most 128 field
sources and 1,048,576 particles in one scene. A zero count hides a field.

With `additive=#true`, particles blend additively and test opaque scene depth
without writing depth or sorting individual drops. Fields emit their authored
color and do not cast shadows or contribute to HRC. The vertex shader computes
positions and colors; each particle has no CPU simulation state or entity.
Per-frame CPU updates scale with the number of fields. Each instance retains
four bytes, uploaded when its count changes, rather than a position/color upload
every frame. `test/fixtures/feature-coverage` contains the `NativeRainField`
scene: 128 falling lattice drops at a fixed one-second clock, with an opaque
box for inspecting depth occlusion.

## Simulate 3D bodies

An entity with `Transform3D` and `Collider3D` collides in the
scene's Box3D world. A collider is a box (`size` is its full extents), a sphere
(`radius`) or a capsule (`radius`, and `height` along local Y), scaled by the
entity's world scale. A `shape="Hull"` collider wraps the vertices of `mesh`, a
`content/... .mesh` file, in at most 64 of them, and a `shape="Mesh"` collider
is that mesh's triangles. A mesh collider is static and a root, and it touches
only the other shapes, never another mesh. Hulls and meshes need a scale of at
least .01 on each axis, and a negative scale mirrors them. Lengths are units,
32 to the meter. Without
`Body3D` the collider is static; with one it is kinematic, moving at its
velocities, or dynamic, and physics writes a dynamic body's position, rotation
and velocities back each step. Dynamic bodies must be roots. Any other body
under a ChildOf parent with a Transform3D follows it and ignores its own
velocities. When the top of its hierarchy has a body, the child's collider
becomes part of that body: it moves exactly with it, adds its mass (a sensor
adds none), and its hits push the body while contact events name the child.
Otherwise the child teleports when a script moves its parents. Moving a body's
Transform3D from a script teleports it. Gravity is 9.81 meters per second
squared down Y unless one entity's `Gravity3D` sets it. 2D bodies
fall only when one entity's `Gravity` sets an `acceleration` in units
per second squared, which also moves material liquids and gas. Behaviors
handle `on ContactBegin3D :: (Other: entity, Point: vec3)`
and `on ContactEnd3D :: (Other: entity)`, as 2D colliders report
`on ContactBegin` and `on ContactEnd`. A sensor collider reports both without pushing. The 3D
renderer draws a body's `Geometry3D` at its Transform3D pose (see
`docs/rendering-3d.md`).

`Character3D` turns a kinematic body with a solid capsule collider
into a character that walks and slides. Each step physics moves it by its
`linear_velocity` until it meets something solid, slides along what it meets,
and drops the part of the velocity that pushes into it, so a script can add
gravity every frame without it building up while the character stands.
Physics writes `on_floor` and `floor_normal`, a surface within 45 degrees of
up, against gravity. While grounded, it rides its supporting collider's body,
including translation and rotation from physics or authored transforms. Carry
is swept against other solids and stays separate from `linear_velocity`.
Jumping away from the floor or walking off releases support without inheriting
platform momentum. Removing or replacing the supporting collider also releases
it. A child collider uses its actual ancestor body's motion.

The character stops at dynamic bodies and other characters as at walls and
needs a radius of at least 2 cm. Colliders under it must be sensors, because
only its capsule moves and slides. It turns by its `Transform3D`; platform
rotation carries its position without turning it. Physics keeps its body's
`angular_velocity` at zero. `MovingPlatforms3D` in the feature-coverage fixture
shows translating and rotating platforms with capsule riders.

`Joint3D` joins the body at or above its entity to the body at or
above `other`, or to the world when `other` is zero, at the entity's position.
Put it on the body itself, or on a child entity with a Transform3D to place and
turn the joint. `kind` is `Hinge`, turning about the entity's local Y axis;
`Slider`, moving along it; `Ball`, turning freely; `Weld`, holding fast; or
`Distance`, keeping the entity at its first distance from `other`'s origin.
`limit` keeps a hinge's angle in radians, a slider's offset or a distance in
units between `lower` and `upper`, and a ball within `upper` radians (at most a
quarter turn) of its axis. `motor` drives a hinge, slider or distance at `speed`
with at most `strength`: a hinge's torque in kilogram square units per second
squared, or a force in kilogram units per second squared. `spring` pulls a hinge, slider, ball or weld back toward where it was
made at `hertz` with `damping`, and makes a distance elastic: a distance's
limit and motor act only with its spring, so a spring of zero hertz and a limit
make a slack rope. `collide` lets the two bodies touch. A joint holds the bodies
as they were when it was made and is made again when its fields change or its
entity moves on its body; a joint whose `other` is destroyed holds nothing.
A scene names `other` by its path, such as `Joint3D other="Frame.Anchor"`,
and a prototype names one of its children the same way, in its own components
or a child's. A scene's paths reach only its own entities, not its instances'
children. Any entity handle at the top of a component takes a path; loading
the scene or spawning the prototype writes the entity there, and a path that
names no entity fails validation and loading. Scripts may still set `other`.

`Joint` joins 2D bodies the same way at the entity's `Transform`, with
`Hinge`, `Slider`, `Weld` and `Distance` and no ball: a hinge turns about the
entity, a slider moves along its local Y axis, and a hinge's limits stay within
0.99 pi radians either way. A dynamic body turns only with `allow_rotation`, so
a hinged door needs it and a `Orientation` to show its angle.

Scripts query colliders where their components place them now, with or without
a step: `raycast_3d(start, end, categories, exclude, sensors)` and
`shape_cast_3d(entity, translation, categories, sensors)` return `.Found`,
`.Entity`, `.Point`, `.Normal` and `.Distance` for the nearest hit, and
`contact_entities_3d(entity, categories)` lists what touches the entity's
collider as the simulation would let it. None of them meets colliders on the
caller's own body. A shape cast that starts touching something, such as the
ground a body rests on, hits it at distance zero, so leave the ground's
category out to sweep along it. A mesh collider can't be swept.

`apply_impulse_3d(entity, impulse)` changes a dynamic body's velocity by the
impulse over its mass at its next physics step, and
`apply_force_3d(entity, force)` accelerates it by the force over its mass
through that step. 2D bodies take `apply_impulse` and `apply_force` with 2D
vectors. A 3D body weighs 1000 kg per cubic meter, the density of water, and a
2D body 1 kg per square meter, so an impulse is in kilogram units per second
and a force in kilogram units per second squared. Pushes act through the
center of mass and add up, and they fail on an entity without a dynamic body.
After each step physics writes a dynamic body's mass, with the solid colliders
it carries, to `mass` on its `Body3D` or `Body`, so
`self.Body3D.mass` times a change of speed is the impulse that makes it. The
mass reads zero until the body's first step.

```kdl
name "Crate"
components {
    Body3D type="Dynamic"
    Collider3D category=1 mask=1 friction=0.6 {
        size x=32.0 y=32.0 z=32.0
    }
    Transform3D {
        position y=64.0
        scale x=1.0 y=1.0 z=1.0
    }
}
```

## Saved progress

`read_saved_number(Key, Default)` returns `.Found` and `.Number`. Missing keys
and text that is not a finite JSON decimal, or that a `number` cannot hold,
return `Found=false` and the default. `await set_saved_number(Key, Number)`
stages a finite number using decimal text that preserves its exact value. It shares the store with `read_saved_text` and
`set_saved_text`.

Both calls carry a `number`, so the store keeps 32-bit values. A behavior can
still declare `f64` variables, such as a long-running accumulator, but saving
one narrows it with an explicit `number(value)`, and reading it back gives only
that precision. Save a value that needs every `f64` digit as text.

Rejecting a nonfinite number is the save store's data validation:
`set_saved_number` refuses NaN and infinities, and `read_saved_number` reports a
missing value for them. It is separate from numeric conversions, where
`number(value)` turns an `f64` past the `number` range into a signed infinity
and keeps NaN. Reads include staged changes. `await commit_save()` writes
them to storage; its `Failed` outcome keeps the previous committed save intact.
`await discard_save_changes()` restores that commit.

Exported players open their save before the first `Begin`. The manifest's
project `name` identifies the store independently of the window title and
export directory. Native players use the OS user-data directory under
`Chibi/Saves`; browser players use localStorage on the page's origin. Renaming
the project selects a different save. `chibi-player --save PATH` overrides the
default file or browser storage key. Preview and scenario worlds retain their
isolated in-memory stores unless their host explicitly opens storage.

## Runtime identity and capability requests

`.chibi-engine.json` records the player package build ID, source SDK build ID,
engine build ID, package version, and exact format/platform/header requirements.
`share/chibi/package.json` inventories every deployed file with SHA256 and size;
its build ID covers the complete contract and source SDK pin. The installed SDK
manifest records compiled source, object, link, and archive evidence separately.

Runtime delivery verifies the package before copying it into the game. It can
retire an older combined runtime only after verifying its complete known file
inventory. Modified or unmanaged directories are preserved. New launches use the
new package; restart an existing game after delivery. Delivery leaves game source
and Codex configuration unchanged. Inspect the local pin and copied manifest to
identify the deployed runtime; the authoring controller's engine identity belongs
to its own distribution.

When the SDK cannot express a behavior, prepare a capability request containing
the build ID, a minimal scene or scenario, expected and actual behavior, and
validation/capture evidence. Keep the reproduction in the game project.
Engine implementation belongs in the engine repository.

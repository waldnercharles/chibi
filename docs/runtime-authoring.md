# Author a game with Chibi

The game's `project.kdl` selects its `content` directory and starting scene.
`chibi-compile` and `chibi_ctl` both read assets from that directory.
Project format 10 uses `.chibi` executable source for behaviors, callable
libraries, and systems. Scenes, prototypes, shader metadata, models, and other
declarative assets use KDL. Particle shaders may use GLSL source; graph-based
materials and shader libraries remain available. Studio, text editors, and AI
agents edit these same asset files.
The copied player deployment lives in the game's ignored `.chibi/runtime` directory.
The compiler, Studio, and controller are separate authoring tools; they do not
ship inside the game runtime.

Games use separate repositories such as `chibi-arena3d`, `chibi-space-cadet` and
`chibi-voidweaver`; labs and sampled models live in `chibi-examples`. Studio owns
its starter and shell assets in `chibi-studio`. Pass the project root to an
installed authoring tool rather than mounting a game under the engine checkout.

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

Kinds are `scene`, `prototype`, `behavior`, `event`, `system`, `library`,
`material`, `shape`, `field`, `shader-library`, `shader-source` and `model`.
`shader-source` creates a KDL descriptor and a matching `.glsl` entry file in
`content/shaders`. Before running content,
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

## Declare components

A game declares its own component types under `content/components`, one `.chibi`
file per component. Fields are laid out in order with natural alignment and start
zeroed. A field's type is a script scalar (`bool`, `int`, `float`, `double`,
`number`, `string`, `entity`, ...), an engine struct such as `CF_V2`, or a
measure: a `number` with an optional range and an optional unit. The `//`
comments directly above the component and each field describe them.

```code
// Stored energy that refills over time.
component Charge {
    level: 0..100 charge
    // Charge gained per second.
    rate: charge/s
    delay: s
    full: bool
    drift: CF_V2
}
```

A range such as `0..1` or `-1.5..2` sets the field's editing range in Studio. It
is a hint, never a clamp: scripts and assets may store any value. A unit such as
`u/s`, `rad`, `u/s^2` (shown as u/s²) or `banana/basket` labels the value. Unit
names mean units only in a field's type, so `s` stays a free name in scripts.
A unit standing alone must be a common one (`u`, `px`, `m`, `s`, `ms`, `rad`,
`deg`, `Hz`, `fps`, `bpm`, `EV`, `dB`, `kg`, `N`), so a misspelled type name
is reported instead of becoming a unit; after a range or with a `/`, any name
works.

Scripts check units the way F# does, and units are erased before the game
runs. `*` and `/` combine them, so `charge.rate * dt` is in `charge` because
`dt` and `delta` are in seconds. `+`, `-`, `%`, comparisons, assignment and `bind` need the
same unit on both sides. A plain number literal takes the unit it meets in a
sum or comparison, and as a factor it may carry any unit, so `3 * dt` is not
checked. A value nothing measures, such as a local set to `0` or a function's
result, is never checked either. The engine's own fields carry units too: a `Movement.speed` is in
`u/s` and a collider's `radius` is in `u`. A mismatch is a warning for now.
`validate_project` lists it with its line, and the program still runs:

```code
system Recharge(charge: Charge) {
    charge.level += charge.rate * dt // charge
    charge.level += charge.rate      // Units differ: charge += charge/s
}
```

Prototypes, scenes, behaviors and systems then use `Charge` like an engine
component. A name may not reuse an engine type. The compiler stores the
declarations in the prepared artifact, so the player needs no source.
`chibi_ctl` formats, validates, describes and runs them like engine components.
Projects saved before version 20 declared components in KDL; `--migrate`
rewrites each into `.chibi`.

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
capability requirements. Final product package acceptance is still in progress.
Package integrity and installed-consumer linking do not establish rendered
player, Studio interaction or browser acceptance.

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
Test invocations remain subject to the [60-second policy](testing.md#temporary-60-second-policy).
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
(`primitive=5`) with positive dimensions. `Shader.material` names either a GLSL
particle descriptor or a pure shader-library function. Both use the same
`Shader.parameters` bindings, including `set_material_parameter()`.

### GLSL particle source

Put the descriptor directly in `content/shaders`, alongside its source:

```kdl
version 1
name "Rain.Fall"
kind "particle"
source "Rain.glsl"
entry "rain"
(array)includes "RainCommon.glsl"
parameters {
    Spacing {
        type "float"
        default 7.0
    }
    Wind {
        type "vec3"
        (array)default 2.0 0.0 0.5
    }
}
```

Set `Shader.material="Rain.Fall"`. The name is the descriptor's public binding;
it does not have to match the file or entry function. Parameter names are GLSL
identifiers and preserve case. There are at most 29 user parameters, each with
type `float`, `vec2`, `vec3` or `vec4` and a finite matching default. Vector
defaults use an explicit `(array)default` with one value per component. The engine
generates the `ChibiParameters` struct from this metadata:

```glsl
#include "RainCommon.glsl"

ChibiParticle rain(ChibiParticleInput p, ChibiParameters u)
{
    ChibiParticle drop = chibi_particle();
    drop.position = vec3(p.index * u.Spacing, 0.0, 0.0) + u.Wind * p.time;
    return drop;
}
```

`ChibiParticleInput` contains float `index`, `count`, `seed` and `time` fields.
The clock is `ParticleField3D.time`. `ChibiParticle` contains `position:vec3`,
`size:float`, `color:vec4`, `scale:vec3`, `rotation:vec3` and `lighting:float`.
`chibi_particle()` initializes them to zero position, size one, white RGBA,
unit scale, zero rotation and unlit lighting. Assign every field when constructing
the struct without that helper. Output units and lighting match the graph path
below.

Write ordinary GLSL scalar/vector math, helper functions, conditionals and
bounded loops. Define helpers before their callers. The current compiler accepts
the GLSL 450 subset supported by Cute's shader compiler; this is a particle vertex
function, not a complete vertex/fragment program. The engine supplies `#version`,
the entry adapter and resource bindings, so do not add `main`, another version
directive or resource declarations. Textures and fragment operations are not
part of this particle interface. Text authoring currently accepts GLSL for
particles only; materials continue to use their graph assets. HLSL and WGSL are
not accepted source formats.

List each helper file in the descriptor's optional `includes` array and use that
exact relative name in `#include`. Source and include paths resolve relative to
the descriptor and must stay inside project content. Includes are bundled during
preparation; the player does not search the filesystem. Up to 32 includes are
allowed, with a 512 KiB limit per source file. The GLSL compiler consumes this
source directly, bypassing shader graph IR and its node budget. `validate_asset`
and project preparation compile the actual particle adapter and report source
diagnostics; `--format` formats the KDL descriptor and leaves GLSL text intact.

### Shader-library particle graphs

A graph binding uses `Library.Function`. Its first arguments are
`Index:float`, `Count:float`, `Seed:float`, followed by named float or
`ShaderVec2/3/4` uniforms, with at most 32 arguments total. Its required results are
`Position:ShaderVec3`, `Size:float`, `Color:ShaderVec4`, in that order.
Position is local to the entity, Size scales the geometry, and Color is HDR RGBA.
Uniform defaults and `Shader.parameters` use the existing material parameter
path, including `set_material_parameter()`.

Particle functions may append `Scale:ShaderVec3`, `Rotation:ShaderVec3`, and
`Lighting:float`, in that order. Omitted outputs use identity scale, zero XYZ
rotation (radians), and unlit color. Scale multiplies the field's geometry
size per instance. Lighting mixes from emissive color at zero to vertex-lit
Lambert shading at one, using the scene's hemisphere, directional, point and
spot lights. It does not sample shadows or screen-space occlusion. Normals
follow both per-particle nonuniform scale and the entity transform. All motion
remains an authored GPU evaluation; this component holds no persistent physics
state or collision solver.

Graph functions support numeric math, vectors, noise, mapping and time/beat reads.
Textures, ramps, scene influences, surface coordinates and fragment derivatives
are rejected.

### Field lifetime and rendering

Set `ParticleField3D.count`, `seed`, and `time` explicitly. Shader Time and GLSL
`p.time` read this component clock; it advances only when an authored behavior
changes it. Time must be finite and nonnegative. There are at most 128 field
sources and 1,048,576 particles in one scene. A zero count hides a field.

With `additive=#true`, particles blend additively and test opaque scene depth
without writing depth or sorting individual drops. Fields emit their authored
color and do not cast shadows or contribute to HRC. The vertex shader computes
positions and colors; each particle has no CPU simulation state or entity.
Per-frame CPU updates scale with the number of fields. Each instance retains
four bytes, uploaded when its count changes, rather than a position/color upload
every frame. `test/fixtures/feature-coverage` contains the `NativeRainField`
scene: 128 falling lattice drops at a fixed one-second clock, with an opaque
box for inspecting depth occlusion. `LitRainField` omits all opaque geometry and
checks the same lit particles in a particle-only scene. `GlslRainField` uses a
GLSL descriptor with typed spacing, size and wind parameters, an included helper,
a three-iteration loop and all six particle outputs. Its 128 lit grains reuse
the rain camera, environment and lamp without game rules or particle CPU state.

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
handle `on ContactBegin3D` with `event.Other` and `event.Point`, and
`on ContactEnd3D` with `event.Other`, as 2D colliders report `on ContactBegin`
and `on ContactEnd`. A sensor collider reports both without pushing. The 3D
renderer draws a body's `Geometry3D` at its Transform3D pose (see
`docs/rendering-3d.md`).

`Character3D` turns a kinematic body with a solid capsule collider
into a character that walks and slides. Each step physics moves it by its
`linear_velocity` until it meets something solid, slides along what it meets,
and drops the part of the velocity that pushes into it, so a script can add
gravity every frame without it building up while the character stands.
Physics writes `on_floor` and `floor_normal`, a surface within 45 degrees of
up, against gravity. The character stops at dynamic bodies and other
characters as at walls, does not ride moving platforms, and needs a radius of
at least 2 cm. Colliders under it must be sensors, because only its capsule
moves and slides. It turns by its `Transform3D`; physics keeps its body's
`angular_velocity` at zero.

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
and text that is not a finite JSON decimal return `Found=false` and the default.
`await set_saved_number(Key, Number)` stages a finite double using decimal text
that preserves its exact value. It shares the store with `read_saved_text` and
`set_saved_text`. Reads include staged changes. `await commit_save()` writes
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

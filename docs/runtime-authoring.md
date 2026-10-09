# Author a game with Chibi

The game's `project.kdl` selects its `content` directory and starting scene.
`chibi-compile` and `chibi_ctl` both read assets from that directory.
Project format 10 uses `.chibi` executable source for behaviors, callable
libraries, and systems. Scenes, prototypes, shaders, models, and other declarative
assets use KDL. Studio, text editors, and AI agents edit these same asset files.
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
`material`, `shape`, `field`, `shader-library` and `model`. Before running content,
`validate_project` checks the manifest, its start scene, its application title
and icon, and every asset. Markup in `content/ui` must be a regular file:
`chibi_ctl` never follows a link, so a linked document is refused. The reply's
`problems` list only the assets with errors or warnings, each diagnostic naming
what is missing (`Unknown prototype 'Block'`) and, when known, its line. While a
model and a shape share a name there is no catalog, so it lists no asset
problems until one is renamed. It never changes files: a project saved by an
earlier build fails until `chibi_ctl --project-root <root> --migrate` upgrades it.
A prototype is the `name` it declares, whatever its file is called: scenes and
other prototypes' `(array)includes` find it by that name in the player and in
`chibi_ctl` alike, and two prototypes that declare one name, or a name over
1024 bytes, are refused.
`new_asset` names a new prototype's file after it. `--format` checks a
prototype with includes but keeps its text as written instead of folding the
included components in.

## Declare components

A game declares its own component types under `content/components`, one KDL file
per component. Fields are laid out in order with natural alignment and start
zeroed. A field type is a script scalar (`bool`, `int`, `float`, `double`,
`string`, `entity`, ...) or an engine struct such as `CF_V2`:

```kdl
name "Charge"
description "Stored energy that refills over time."
(array)fields {
    - name="level" type="float"
    - name="capacity" type="float"
    - name="full" type="bool"
    - name="drift" type="CF_V2"
}
```

Prototypes, scenes, behaviors and systems then use `Charge` like an engine
component. A name may not reuse an engine type. The compiler stores the
declarations in the prepared artifact, so the player needs no source.
`chibi_ctl` formats, validates, describes and runs them like engine components.
Studio does not read declarations yet.

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
any of them, and refuses a scene that binds one `GameGlobalBinding` name to two
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

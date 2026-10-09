# Author a game with Chibi

The game's `project.kdl` selects its `content` directory and starting scene.
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
`validate_project` checks the manifest, its start scene and every asset. Its
`problems` list only the assets with errors or warnings, each diagnostic naming
what is missing (`Unknown prototype 'Block'`) and, when known, its line. While a
model and a shape share a name there is no catalog, so it lists no asset
problems until one is renamed. It never changes files: a project saved by an
earlier build fails until `chibi_ctl --project-root <root> --migrate` upgrades it.
A prototype's `(array)includes` names other prototypes, which `chibi_ctl` finds
by file name, so name each prototype's file after it. `--format` checks such a
prototype but keeps its text as written instead of folding the included
components in.

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
printf '%s\n' '{"contract":1,"operation":"validate_asset","path":"behaviors/Projectile.chibi"}' | \
  chibi_ctl --project-root "$PWD"
```

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

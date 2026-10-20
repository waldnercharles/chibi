# Design notes

These are changes worth making while stage one re-implements what stage zero
and the fixtures do today, instead of porting them as they are. Each entry
says what we do now, how other engines handle the same need, and what to
change. `docs/language-friction.md` covers the language itself. Nothing here
is decided or implemented. Line references are to `main` after #420
(2026-10-19), whose language the re-implementation targets.

## Should Terrain Lab be the compiler's checked game?

Terrain Lab has three roles today:
- it is the playable example in `docs/examples.md`;
- it is the terrain save/load fixture for the `terrain-playable` selectors;
- it is the game that M5 step 4 rebuilds through stage one.

It came from the environment-authoring work (E1) as a playable terrain
example, and moved here when the example repositories were retired.

To compile it, stage one needs nearly everything at once:
- floats and vectors;
- behaviors with state, handlers and components;
- `globals` and `input`;
- named native arguments and effectful natives;
- a field library.

Other compilers gate on a corpus before any one program:
- Go's `toolstash -cmp` compares the object files of the whole standard
  library between two compilers.
- Rust's crater builds every published crate with both compilers.
- GCC compares its stage-two and stage-three object files.

Change:
- Gate stage one first on the feature corpus. `test/fixtures/feature-coverage`
  exercises each component, native and event, apart from those listed in its
  `coverage-gaps.txt`. Run each feature as it lands under the
  game-equivalence harness (`CHIBI_CANDIDATE_COMPILER`) with a short
  scenario.
- Keep Terrain Lab as the last, whole-game gate and as the playable example.
- Slim it with the engine changes below first. More than a third of
  `Explorer.chibi`'s 140 lines work around engine gaps rather than stating
  the game's rules, and stage one would have to compile those workarounds
  too.

## Input edges are tracked by hand

`Explorer.chibi` keeps `jumpHeld`, `digHeld`, `saveHeld`, `loadHeld` and
`respawnHeld` (lines 7-11). It compares each with the current `held` value
every Tick (lines 111-134).

- Godot has `Input.is_action_just_pressed`.
- Unity's Input System has `WasPressedThisFrame`.
- Unreal's Enhanced Input fires a `Started` trigger.
- Defold passes `action.pressed`.

Change: use `input.Jump.pressed`, which Chibi already has. It is true for one
fixed step (`docs/code-language.md` on main, line 902). That removes five
state fields and their bookkeeping.

## Storage completes by polling a request ID

`Save` and `Load` store a request ID. `PollStorage` asks
`terrain_storage_result` every Tick until the request leaves Pending
(lines 12, 38-90, 104), and `storageAction` remembers which operation was
pending.

- Godot `await`s a signal.
- Unity uses coroutines or `async` with `Awaitable`.
- Unreal Blueprints use latent nodes that resume when the work completes.
- Roblox calls yield.

Change: give `save_terrain` and `load_terrain` a `Completed` outcome, so that
`result := await save_terrain(terrain, identity)` reads as straight-line code.
`await` already follows a native's `Then` or `Completed` outcome
(`docs/code-language.md` on main, lines 340-383). The polling state, the
request IDs in the game, and the work of compiling that pattern would all go
away.

## Falling out of the world is checked in code

Every Tick, the behavior respawns the explorer when it passes fixed bounds:
`if self.Transform.y > 190 || abs(self.Transform.x) > 380`.

- Unreal has a world `KillZ`.
- Godot and Unity use trigger volumes: `Area2D.body_entered` and `isTrigger`
  colliders.

Change: author the bounds as a sensor body. Bodies already have `sensor`
(`src/runtime/components.h`). The explorer then handles its Contact event, so
the numbers live in the scene, not the code.

## The camera's follow and limits are a behavior

`QuarryCamera.chibi` clamps the target to ±144 and smooths with
`1 - pow(0.001, dt)`.

- Godot's `Camera2D` has `limit_*` and position smoothing.
- Unity's Cinemachine has damping and a confiner.
- Unreal's `SpringArm` has camera lag.

Chibi's `Camera` follows its entity with an offset but has neither limits nor
damping.

Change: add limits and damping to `Camera`, in line with turning camera
choreography into authored data. `QuarryCamera` then disappears, along with
what stage one would need to compile it.

## HUD text is set by CSS selector

The game writes `set_ui_text(Element: "#status", Text: ...)` throughout.

- Unity's UI Toolkit binds elements to data sources.
- RmlUi binds `{{ }}` views to a data model.

The C binding already wraps RmlUi data models (`src/ui/rml.h`).

Change: bind HUD text to behavior state, so the game writes
`status = "Terrain saved."` and the page shows `{{ status }}`. Lower priority
than the entries above.

## One way to read the step

`QuarryCamera` reads a bare `dt` in `on Tick {`, while the language guide
writes `on Tick(dt)`.

- Godot passes `delta` to `_process`.
- Unity reads a global, `Time.deltaTime`.
- Bevy injects `Res<Time>`.

Change: keep the handler parameter only, so stage one implements one form
rather than both.

## The field library reads `entry` beside its own parameters

`QuarryShapes.Quarry` declares `X` and `Y` but reads `entry.X` and `entry.Y`.

Change: read the parameters, and retire `entry` from libraries if it is left
over from graphs. Before the redesign touches it, that needs the owner's
answer on what `entry` still means. The terrain itself is one nested call
expression. SDF tools such as Houdini, Dreams and Inigo Quilez's examples
compose named operations instead, which `field_*` could offer as a short
vocabulary later.

## Single named results with defaults

`Grounded :: () -> (Result: bool = false)` names and defaults its only
result.

Change: stage one implements named results, Odin's model and needed for
natives. Terrain Lab writes `-> bool` where the name says nothing.

## The equivalence harness says that a frame differs, not what

`game-equivalence` hashes the ECS world, saves, materials, physics and UI per
fixed step.

- Lockstep games check the same kind of per-step checksums. Factorio's desync
  report also dumps both states at the first divergent tick.

Change: at the first differing frame, have the observer write that frame's
component values from both artifacts, as the controller's runtime snapshot
does. `Diff` then names the first entity and component that differ.

## Stage one should not inherit stage zero's split

Stage zero's `project_prepare.c` compiles modules and assembles the game
(scenes, events, catalogs) in one C pass. The tempting route for stage one is
to swap its module bytes in there.

- Go's `cmd/go` drives `cmd/compile` and `cmd/link`, all written in Go.

Change: build the stage-one route as a Chibi program. It would call the
compiler host per module and write the player artifact through a narrow
binding, as M6 asks of the build graph. That beats a C hook that would have
to be removed again.

## Stage one parses with Tree-sitter

`compiler_syntax_parse` is the C Tree-sitter parser. #420's grammar renames
the node kinds that `Tree.chibi`, `Compile.chibi`, `Body.chibi` and
`Steps.chibi` read by name.

- Odin's parser (`core:odin/parser`), Go's `go/parser` and Zig's `std.zig.Ast`
  are written in their own languages. Editors keep a separate grammar.

Change: rather than re-bind stage one to the new Tree-sitter node kinds, write
the parser in Chibi on the Chibi tokenizer that the odin-tokenizer branch
began (`tools/compiler/source_tokenizer`, not yet merged). Keep Tree-sitter
for Studio and highlighting.

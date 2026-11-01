# Design notes

These are changes worth making while stage one re-implements what stage zero
and the fixtures do today, instead of porting them as they are. Each entry
says what we do now, how other engines handle the same need, and what to
change. `docs/language-friction.md` covers the language itself. Line references
are to `main` after #420 (2026-10-19), whose language the re-implementation
targets.

## Decisions (owner, 2026-10-20)

- **Examples.** Playable examples such as Terrain Lab live outside the engine
  repository. The compiler's corpus is intentional, purpose-made example
  files, one per feature, browsable like Storybook. Stage one gates on those
  before any whole game.
- **Stage one's artifact route.** It is written in Chibi, not as a C hook:
  "The user should need to download nothing else to start programming."
- **Chibi parser.** A Chibi parser on the Chibi tokenizer is wanted, but does
  not hold up M5. Only the tokenizer exists so far, on the odin-tokenizer
  branch.
- **`entry`.** Removed from the language. Phase 1 (project version 28):
  `entry.X` and a bare `entry` became the parameter's own name. Phase 2a
  (project version 29): a function has no `after entry`; its statements
  moved into the function's body. Still under discussion: macro entry and
  exit paths becoming ordinary functions returning a union, and whether
  `after` goes entirely.
- **Named results.** A named, defaulted single result whose name adds nothing
  is dropped. Named results remain where natives need them.
- **Events.** Decided: a handler is written `on Tick :: (dt: s) { ... }`. It
  names the payload fields it reads, in any order and any subset, each with
  its type, and `on Begin :: () { }` takes none. Systems declare the step the
  same way, and `event` goes. Tick's payload is `dt` in seconds (#551).
  Project version 30 migrates every handler to the new form; a later change
  removes the old forms from the language. Declarations name-first, `Tick :: event (dt: s)`, may follow.
  See "One way to read the step".

## Decisions (owner, 2026-10-21)

- **Strings carry their length.** A Chibi `string` becomes a pointer and a
  byte length, as in Odin, Go, Rust and Zig, so indexing, length and slicing
  cost O(1) and a slice may share its parent's bytes. Today `ByteAt`, `Slice`
  and `ByteLength` measure the text with `strlen` on every call, and the
  compiler's `Text(forest, text, node)` walks each source up to every node it
  slices. See "Strings carry their length" below.
- **One string type.** Chibi code sees a single `string`, with no `cstring`.
  Native C components keep their NUL-terminated `const char *` fields, and the
  runtime converts at that boundary.
- **Arrays change through methods.** `code.append(x)` replaces
  `code = array_add(code, x)`, and every array function is a method on its
  first argument. `append` adds an item and `extend` adds an array's items;
  project version 32 removed `array_add` and the concatenating `append`.

## Decisions (owner, 2026-10-22)

- **Flat system query bindings.** Use one flat binding list for root
  components, relationship generators and components from related entities.
  The implementation uses the shared description for systems and query iterators:

  ```chibi
  DamageTargets :: system(
      damage: Damage,
      target: entity in self.Targets,
      health: ^Health from target,
      if health.value > 0,
  ) {
      health.value -= damage.amount
  }
  ```

  `in` generates matching rows; `from` binds a component on the named source.
  Generator types may be written or inferred with `target in self.Targets`.
  Component values are read-only snapshots; `^T` bindings access the component.
  Pure `:=` intermediates and `if` guards evaluate from left to right.
  `query(...)` is an iterable description; each loop owns its cursor.
  Systems have implicit body-local `dt` and retain per-tuple continuations.
  This supersedes system Tick payload headers and the separate `join`,
  `without` and runtime `where` query clauses. Behavior handlers still name
  their event payloads. Grouped source bindings remain deferred.
- **Grouped bindings are a future exploration.** Consider grouping several
  component bindings under their source entity when repetition becomes a
  demonstrated problem, for example:

  ```chibi
  target: entity in self.Targets {
      health: Health,
      armor: Armor?,
  }
  ```

  This sketch is deferred. Its binding scope, interaction with generators,
  and distinction from an executable block need a separate design decision.

The remaining entries are proposals, except "Strings carry their length",
which records how the decision above is carried out.

## Strings carry their length

Now: a string value is a `const char *` in `pin_value.str`, in VM slots, in
data cells and in record fields. Every byte operation measures it, and every
slice copies its bytes.

Elsewhere: Odin's `string` is `struct { data: [^]u8, len: int }`, beside a
NUL-terminated `cstring` for C. Go's string header and Rust's `&str` are the
same pair. Zig's `[]const u8` is a slice, and string literals are also
NUL-terminated for C. Each shares bytes when it slices.

Representation: slots, data-cell payloads and record fields hold one pointer,
and SLJIT writes only that payload. So the length cannot live beside the
pointer in `pin_value`: a JIT write would leave it stale. The pointer instead
addresses an immutable descriptor, `{const char *bytes; uint32_t length}`,
whose bytes lie inside a NUL-terminated buffer:

- Values, slots and record fields stay 8 bytes; only what the pointer means
  changes.
- `Slice` allocates a descriptor into its parent's bytes and copies none.
- C needs a NUL-terminated string when it writes a native component field or
  calls a host API. It gets the descriptor's bytes as they are when
  `bytes[length]` is the buffer's terminator, which is always in bounds, and a
  copy otherwise.
- Reading a native component's `const char *` field measures it once and
  makes a descriptor.
- Interned strings, prepared string constants and the wire and document
  formats carry the length.

Phases, each its own PR:

1. Route every C reader of `pin_value.str` (about 270 sites) and every
   producer through accessors, `pin_text(value)` for bytes and length and
   `pin_value_text(...)` to build one. The representation does not change, so
   the accessor still measures.
2. Switch the accessors and producers to descriptors, convert at the native
   component boundary, and bump the prepared format and rebuild the bootstrap
   artifact.
3. Make `Slice` share bytes. Make `ByteAt` and `ByteLength` read the length.
   Add `Bytes`-free compiler paths where they help.

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

Resolved for handlers (2026-10-20, #565 and the PR that removed the old
forms): a Tick handler lists the step's field, `on Tick :: (dt: float s)`. The
positional list, the `as` alias and `event.dt` no longer parse or analyze.
Project version 31 removed the globals `dt`, `delta` and `delta_time()`
(owner decision, 2026-10-20): a handler or system lists `dt`, and a function
that needs the step takes it as a parameter.

The system part is superseded by the 2026-10-22 flat-query decision above:
systems receive a read-only body-local `dt` implicitly. Behavior handlers
continue to list payload fields, and helpers still take the step explicitly.

- Godot passes `delta` to `_process`.
- Unity reads a global, `Time.deltaTime`.
- Bevy injects `Res<Time>`.

Change (decided): one form. Events are declared name-first with a typed
payload, `Tick :: event (dt: s)`, as other declarations are. A handler is
written `on Tick :: (dt: s) { ... }`; its parameters are checked against the
declaration, and the other three forms go. Godot's `signal tick(dt: float)` and Pony's
`be tick(dt: F64)` declare events with typed payloads the same way.

## Single named results with defaults

`Grounded :: () -> (Result: bool = false)` names and defaults its only
result.

Change: stage one implements named results, Odin's model and needed for
natives. Terrain Lab writes `-> bool` where the name says nothing.

## The equivalence harness says that a frame differs, not what

`game-equivalence`, archived with game projects, hashed the ECS world, saves,
materials, physics and UI per fixed step.

- Lockstep games check the same kind of per-step checksums. Factorio's desync
  report saves both the client's and the server's game state, a few ticks
  after their checksums diverge, and the cause is found by comparing them.

Change: at the first differing frame, have the observer write every
component's values for that frame from both artifacts. `Diff` then names
the first entity and component that differ. The
harness replays deterministically, so unlike Factorio it can capture the
exact frame.

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

The retired `compiler_syntax_parse` was the C Tree-sitter parser; `tools/compiler/source_parser` replaced it. #420's grammar renames
the node kinds that `Tree.chibi`, `Compile.chibi`, `Body.chibi` and
`Steps.chibi` read by name.

- Odin's parser (`core:odin/parser`), Go's `go/parser` and Zig's `std.zig.Ast`
  are written in their own languages. Editors keep a separate grammar.

Change: rather than re-bind stage one to the new Tree-sitter node kinds, write
the parser in Chibi on the Chibi tokenizer that the odin-tokenizer branch
began (`tools/compiler/source_tokenizer`, not yet merged). Keep Tree-sitter
for Studio and highlighting.

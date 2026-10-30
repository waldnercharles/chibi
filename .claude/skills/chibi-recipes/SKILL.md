---
name: chibi-recipes
description: Step-by-step recipes for the Chibi engine repo. Use when adding a reflected component, a native script function, or a project format change.
---

Each recipe ends with the checklist in `AGENTS.md` ("Before every commit").
Copy the nearest existing example rather than inventing a new shape; the
smallest ones are named below.

## Add a component

`components.h` holds only engine capabilities that several games reuse.
Game rules, tuning and scene composition stay in authored content. For a
reusable capability:

1. Declare the struct in `src/runtime/components.h` with
   `NODE_COMPONENT_TYPE_DENSE(Name)` (see `GlobalBinding`). Use plain
   C fields; enums become named choices, `NODE_HANDLE(ecs.entity)` marks an
   entity reference. Comment what it means and its units, and mark a numeric
   field's unit and stored-value contract with `NODE_UNIT("u/s")` and
   `NODE_RANGE(0, 1)`. Owning writes clamp the range; use `NODE_RANGE_HINT`
   only for a slider whose endpoints are not semantic limits. Native writes
   through row pointers must call `ecs_component_modified` before publication.
2. Build (`chibi build`). The reflect step regenerates the native type tables
   and adds the component's row to `src/runtime/component_ids.tsv`. Commit
   that row with the header change; never edit the ID by hand. Resolve a
   conflict in that file by rebuilding, not by merging rows by hand.
3. Read the component where it acts: a system in `src/runtime`, or script
   through the generated get/set. Keep it optional, so scenes without it
   still work.
4. Cover its behavior with a pico_unit case.

## Add a native script function

The smallest complete example is Play Sound:
`src/runtime/audio_definition.h`, `audio_definition.c` and `audio.c`.

1. In `<area>_definition.h`, name the function (`#define ..._NAME`), list its
   inputs and outputs as X-macros, and write its presentation: category,
   keywords and a one-sentence description.
2. In `<area>_definition.c`, add the definition (with its script
   `source_name`) and its binding to the area's `native_definition_pack`.
3. Implement the call in `<area>.c` as a `node_host_call` plus its
   `node_access` effect flags. Pure functions declare no effects.
4. A new pack also needs its library in `sdk/native.cmake` and an
   `apush(selected, ...)` in `src/host/native_default.c`.
5. Test it from source in a tool selector (for example
   `test/runtime/source_features.c`, run with
   `chibi build test source-native-features`), on both the VM
   and SLJIT backends.

## Change saved projects

A build opens only projects at `CHIBI_PROJECT_VERSION`; there are no migration
steps and no compatibility aliases (see the first rule in `AGENTS.md`).

Bump the version only when you can name a project in this repository or
`chibi-studio` that would otherwise load wrong or fail. Skip the bump for
additive changes, a contract no project uses yet, a change to what the engine
does with a value (a physics or rendering fix), or old data that loading
already handles. The PR then says how to fix a hand-made project.

When you do bump it:

1. Bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h`.
2. Edit Studio's projects (`projects/starter`, `projects/studio`,
   `tests/acceptance/project`) the same way in a Studio PR that pins the
   engine PR in `.github/engine-ref`, and merge the two together.
3. Game projects are archived, so nothing compiles them yet.

Prepared artifact versions (`PLAYER_PREPARED_VERSION` and the other
`*_PREPARED_VERSION`s in `sdk/artifact_formats.cmake`) change in the PR that
changes the format, never in a follow-up. Artifacts are rebuilt from source,
so they need no migration. If `main` took the same number first, take the
next one when you merge `main`; git applies two identical bumps silently.

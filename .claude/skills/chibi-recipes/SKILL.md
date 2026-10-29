---
name: chibi-recipes
description: Step-by-step recipes for the Chibi engine repo. Use when adding a reflected component, a native script function, a test scene or scenario, or a project format change.
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
   Give it a Properties tab or icon
   in `src/authoring/engine_components.kdl`.
2. Build (`make build`). The reflect step regenerates the native type tables
   and adds the component's row to `src/runtime/component_ids.tsv`. Commit
   that row with the header change; never edit the ID by hand. Resolve a
   conflict in that file by rebuilding, not by merging rows by hand.
3. Read the component where it acts: a system in `src/runtime`, or script
   through the generated get/set. Keep it optional, so scenes without it
   still work.
4. Use it in a `test/fixtures/feature-coverage` scene or prototype (KDL key
   `<Name>`) and cover its behavior with a pico_unit case.
   `make test-projects` fails until the component is used there or listed in
   that fixture's `coverage-gaps.txt` with a reason.
5. If saved projects change meaning, also follow "Change saved projects".

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
   `python3 tools/test_policy.py tool source-native-features`), on both the VM
   and SLJIT backends, and call it from a behavior in the
   `test/fixtures/feature-coverage` scene for its area.

## Add a test scene or scenario

1. Pick the fixture project under `test/fixtures` that matches the area, or
   add one with `project.kdl` (`version`, `name`, `content`, `start_scene`)
   and a `content/` tree. Don't add game rules to engine fixtures; keep
   content minimal and independent of any one game. A scene that exercises
   an engine feature area belongs in `test/fixtures/feature-coverage`, where
   `make test-projects` runs every scene for four frames.
2. Add `content/scenes/<Name>.kdl` (`version 1`, `name`, `(array)entities`
   of `- name=... prototype=...`) and the prototypes it uses. A scenario in
   `content/scenarios/<Name>.kdl` names the scene, a frame count and input keys.
3. Format and validate: `build/bin/chibi_ctl --project-root <root> --format`,
   then a `validate_asset` request per changed asset.
4. Run it bounded. A scene:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"scene","name":"<Scene>","frames":4}' | build/bin/chibi_ctl --project-root <root>`
   A scenario, which supplies its own scene, frames and input:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","scenario":"<Name>"}' | build/bin/chibi_ctl --project-root <root>`
   Check `behavior_failures` and the state evidence. Choose enough frames to
   exercise the scenario.

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
2. Edit every project in this repository (every `project.kdl` under
   `test/fixtures`) to the new format and version directly, with a one-off
   script if it helps; don't commit the script.
3. Edit Studio's projects (`projects/starter`, `projects/studio`,
   `tests/acceptance/project`) the same way in a Studio PR that pins the
   engine PR in `.github/engine-ref`, and merge the two together.
4. Run `chibi_ctl --format` on each edited project and compile it with
   `chibi-compile` to prove it loads.

Prepared artifact versions (`PLAYER_PREPARED_VERSION` and the other
`*_PREPARED_VERSION`s in `sdk/artifact_formats.cmake`) change in the PR that
changes the format, never in a follow-up. Artifacts are rebuilt from source,
so they need no migration. If `main` took the same number first, take the
next one when you merge `main`; git applies two identical bumps silently.

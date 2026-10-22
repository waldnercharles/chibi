---
name: chibi-recipes
description: Step-by-step recipes for the Chibi engine repo. Use when adding a reflected component, a native script function, a test scene or scenario, or a project migration.
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
   in `src/authoring/engine_components.chibi`.
2. Build (`make build`). The reflect step regenerates the native type tables
   and adds the component's row to `src/runtime/component_ids.tsv`. Commit
   that row with the header change; never edit the ID by hand. Resolve a
   conflict in that file by rebuilding, not by merging rows by hand.
3. Read the component where it acts: a system in `src/runtime`, or script
   through the generated get/set. Keep it optional, so scenes without it
   still work.
4. Use it in a `test/fixtures/feature-coverage` scene or prototype (component object key
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
   add one with `project.chibi` (`version`, `name`, `content`, `start_scene`)
   and a `content/` tree. Don't add game rules to engine fixtures; keep
   content minimal and independent of any one game. A scene that exercises
   an engine feature area belongs in `test/fixtures/feature-coverage`, where
   `make test-projects` runs every scene for four frames.
2. Add `content/scenes/<Name>.chibi` as an ordinary module importing
   `game:assets` and exporting `Build() -> byte[]`. Return `data.Encode(...)`
   with the scene's version, name and entities; `data.Scene` and
   `data.EntityDefinition.Named` provide typed family constructors. Add the
   prototypes it names. A module in `content/scenarios/<Name>.chibi` uses
   `data.Scenario` to name the scene, frame count and input keys. The complete
   examples are in `docs/examples.md` and `docs/runtime-authoring.md`.
3. Format and validate: `build/bin/chibi_ctl --project-root <root> --format`,
   then a `validate_asset` request per changed asset.
4. Run it bounded. A scene:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"scene","name":"<Scene>","frames":4}' | build/bin/chibi_ctl --project-root <root>`
   A scenario, which supplies its own scene, frames and input:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","scenario":"<Name>"}' | build/bin/chibi_ctl --project-root <root>`
   Check `behavior_failures` and the state evidence. Keep frame counts
   small; the 60-second policy applies.

## Change saved project contracts

Current source projects use manifest version 33. They compile ordinary Chibi
asset modules; earlier document parsers and automatic source migrations are
retired. `--migrate` validates the current baseline rather than recovering an
older source format.

1. Identify an existing project in this repository or `chibi-studio` that the
   contract change would make load wrong or fail. Skip a version bump for an
   additive feature or an unused contract; explain any manual fix in the PR.
2. Implement the change in authored Chibi when it is compiler/schema/authoring
   policy. Keep native code limited to runtime facilities and integration.
3. When a bump is required, change `CHIBI_PROJECT_VERSION` and convert affected
   source modules, nested configuration values and references together. Preserve
   ordered values, integer widths, scalar types and authored computations. Any
   temporary conversion tool is removed from the final change.
4. Cover the changed contract through the production compiler/value path using a
   bounded admitted fixture. Format and compile each affected project under the
   validation policy in `docs/testing.md`.
5. Coordinate Studio/game conversions before raising the oldest supported source
   version. Do not create a separate PR that only changes another repository's
   version number.

Prepared artifact versions (`PLAYER_PREPARED_VERSION` and the other
`*_PREPARED_VERSION`s in `sdk/artifact_formats.cmake`) change in the PR that
changes the format, never in a follow-up. Artifacts are rebuilt from source,
so they need no migration. If `main` took the same number first, take the
next one when you merge `main`; git applies two identical bumps silently.

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
   `NODE_COMPONENT_TYPE_DENSE(Name)` (see `GameGlobalBinding`). Use plain
   C fields; enums become named choices, `NODE_HANDLE(ecs.entity)` marks an
   entity reference. Comment what it means and its units.
2. Build (`make build`). The reflect step regenerates the native type tables
   and adds the component's row to `src/runtime/component_ids.tsv`. Commit
   that row with the header change; never edit the ID by hand. Resolve a
   conflict in that file by rebuilding, not by merging rows by hand.
3. Read the component where it acts: a system in `src/runtime`, or script
   through the generated get/set. Keep it optional, so scenes without it
   still work.
4. Use it in a `test/fixtures/feature-coverage` scene or prototype (KDL key
   `GAME.<Name>`) and cover its behavior with a pico_unit case.
   `make test-native` fails until the component is used there or listed in
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
   `make test-native` runs every scene for four frames.
2. Add `content/scenes/<Name>.kdl` (`version 1`, `name`, `(array)entities`
   of `- name=... prototype=...`) and the prototypes it uses. A scenario in
   `content/scenarios/<Name>.kdl` names the scene, a frame count and input keys.
3. Format and validate: `build/bin/chibi_ctl --project-root <root> --format`,
   then a `validate_asset` request per changed asset.
4. Run it bounded. A scene:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"scene","name":"<Scene>","frames":4}' | build/bin/chibi_ctl --project-root <root>`
   A scenario, which supplies its own scene, frames and input:
   `printf '%s\n' '{"contract":1,"operation":"run_scenario","scenario":"<Name>"}' | build/bin/chibi_ctl --project-root <root>`
   Check `behavior_failures` and the state evidence. Keep frame counts
   small; the 60-second policy applies.

## Change saved projects (migration)

A project saved at an older version loads only after `--migrate` upgrades
it; there are no compatibility aliases (see the first rule in `AGENTS.md`).

1. Bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h` and add the step
   that upgrades the previous version to the `switch` in
   `src/assets/project_migrate.c`. Steps are idempotent and stage every
   source before replacing any.
2. Cover the step with a pico_unit case that migrates a small old-format
   source and checks the result (see `program_version_11_references_migrate_to_values`
   in `test/language/program.c`).
3. Migrate every project the change reaches with
   `build/bin/chibi_ctl --project-root <root> --migrate`: every `project.kdl`
   under `test/fixtures`, and the game and Studio repositories
   (`chibi-voidweaver`, `chibi-celeste`, `chibi-studio/projects`), each in
   its own PR that names the engine PR it needs.
4. Run `chibi_ctl --format` on each migrated project and compile it with
   `chibi-compile` to prove it still loads.

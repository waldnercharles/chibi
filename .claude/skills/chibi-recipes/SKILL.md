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
3. Format and validate: `chibi fmt <root>`, then `chibi check <root>`.
4. Run it bounded with the project's `@test` functions (`chibi test <root>`) or
   a scenario through `make agent-test`. Choose enough frames to exercise the
   scenario.

## Change saved projects (migration)

Opening a project saved at an older version (`chibi_project_open`) upgrades
it in place, and `chibi check` refuses an older project until it has been opened. There are no compatibility aliases (see the first rule in `AGENTS.md`).

Bump the version only when you can name a project in this repository or
`chibi-studio` that would otherwise load wrong or fail, and the step would
rewrite it. Skip the steps below for additive changes, a
contract no project uses yet, a change to what the engine does with a value
(a physics or rendering fix), or old data that loading already handles (a step
that would only write what the runtime does anyway). The PR then says how to
fix a hand-made project.

1. Bump `CHIBI_PROJECT_VERSION` in `src/assets/project.h` and add the step
   that upgrades the previous version to the `switch` in
   `src/assets/project_migrate.c`. Steps are idempotent and stage every
   source before replacing any. After each step the manifest's `migrated`
   node records the version the sources reached, so a rerun after a later
   failure resumes there; only a step interrupted while replacing runs again.
2. Cover the step with a pico_unit case that migrates a small old-format
   source and checks the result (see `project_retirement_migrates_owned_values_and_preserves_shader_wires`
   in `test/authoring/project_document.c`).
3. Migrate every project in this repository by opening it (`chibi build <root>`):
   every `project.kdl` under `test/fixtures`.
4. Run `chibi fmt` on each migrated project and compile it with
   `chibi-compile` to prove it still loads.

Don't open version-bump PRs in other repositories (`chibi-studio`, games).
Their projects may lag `main`: Studio, `chibi`, `chibi-compile` and `chibi
export` upgrade an older project when they open it, and Studio CI opens
Studio's projects against engine `main` on every run. A lagging project takes
the new version in the next PR that changes it for another reason. Change
another repository first only when a step stops on something it can't rewrite
(a removed component a script names): make that edit work with the current
engine, merge it before the engine PR, and leave its version number alone.
Raising `CHIBI_PROJECT_OLDEST_VERSION` is the one case that needs those
projects migrated first; then delete the steps below it and their tests. A
project older than that version fails to open with an error naming the
version and an older SDK's `chibi_ctl --migrate`.

Prepared artifact versions (`PLAYER_PREPARED_VERSION` and the other
`*_PREPARED_VERSION`s in `sdk/artifact_formats.cmake`) change in the PR that
changes the format, never in a follow-up. Artifacts are rebuilt from source,
so they need no migration. If `main` took the same number first, take the
next one when you merge `main`; git applies two identical bumps silently.

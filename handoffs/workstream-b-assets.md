# Workstream B: project content and catalog in typed Chibi

Owner: the old-formats session. Coordinator: the parent session. Written 2026-10-29.

## Goal

Authored project content stops being KDL that C parses. It becomes typed Chibi
declarations that the self-hosted compiler checks and lowers, so the C asset
parsers and catalog builders in `src/assets` stop being built (delete what's dead;
`archive/<original path>` for whole features, per the owner).

## Order

1. Finish removing migrations (your `cwaldner/remove-project-migrations` branch).
2. `project.kdl` manifest -> a Chibi `Project` declaration.
3. Input maps (`src/assets/input_map.c`) and physics settings (`physics_settings.c`).
4. Prototypes (`prototype*.c`) and scenes (`scene_document.c`, `scenario_document.c`) as
   typed records and constructors.
5. (Materials moved to core-math, which is replacing graph-format assets: events, fields, materials, shaders, models.)
6. The game catalog: what C exports today becomes a Chibi package the compiler
   imports. Workstream A (the coordinator session) needs this; ask it how the
   compile path consumes the catalog.
7. `new_asset.c`, `asset_source.c`, `refactor.c` and `reference_adapters.c` go as
   their users go.

## Target

- Extend `packages/game/assets` (`Assets.chibi`, `Schemas.chibi`) as the typed schema.
- Projects declare content in `.chibi` files using records, unions, generics, maps
  and `@error` results where they help. Lean on Zig/Odin prior art: plain data
  declarations, no DSL.
- Convert this repo's `.kdl` files (236 at the time of writing) with a one-off
  conversion tool, deleted afterwards. Studio's projects get a paired Studio PR;
  Studio may break in between.

## Leave alone

- `ui_*` (lang-ux takes the prepared UI assets).
- `model_*` import/export (Box3D/glTF tooling, later).
- `src/graph` and the graph-format assets (events, fields, materials, shader libraries, shapes, models): core-math archives them all now (the owner accepts losing them temporarily), then reintroduces each kind as typed Chibi.

## Rules

Merge as soon as a step builds, without waiting for CI; fix main forward. Delete or
archive the C in the same PR that replaces it. Report C lines removed per PR. If a
needed language feature is missing, tell the parent session, which routes it to
generics or records.

## Status (old-formats, 2026-10-30)

Nothing in the toolchain reads KDL any more, and the C KDL stack is archived:

- #919: the compiler's bootstrap contract is `tools/compiler/bootstrap.txt`, read as plain lines.
- #921: `src/assets` project/prototype/scene/scenario/input-map/physics readers and the
  authoring services over them (native_prototype, scene_hierarchy, scene_snapshot,
  component_presentation) archived with their tests; `src/assets/project.h` keeps only
  `CHIBI_PROJECT_VERSION` (packaged-game manifests in `src/support/resources.c`).
- #922: `src/support/asset_text`, `third_party/ckdl`, and `parts_prepare`/`mesh_prepare` archived.

Left in `src/assets`: `ui_*` (#915 and lang-ux). The ~230 `.kdl` fixtures under `test/` are now
data no C reads; they go when their projects' tests are archived or return as typed Chibi.
Project content comes back as typed Chibi only when games return (not now, per focus.md).

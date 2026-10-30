# Archive

This tree is not built. It keeps code that is being rebuilt in Chibi so it can serve as a reference
while features are replaced one at a time with Zig- and Odin-style tooling. Paths below `archive/`
match their original paths in the repository root.

Nothing here is built, formatted, tested or listed in an inventory. Bring a file back with `git mv` to its original path and re-add it to the build lists.

## Studio editor services (authoring)

Editing, rename, completion/hover/highlighting and prototype-source editing for Studio. The compiler, formatter and reference index do not need them.

- `src/authoring/source_edit*`: refactoring and source edits (the text helpers `source_minimize` needs moved to `source_refactor_text`).
- `src/authoring/source_intelligence*`: highlighting, completion and hover.
- `src/authoring/source_rename*`: rename plans.
- `src/authoring/source_prototype_edit*`: prototype source edits.
- `test/authoring/source_edit.c`: their test.

## Source formatter and documentation site (authoring)

`chibi fmt` / `chibi-compile --format` and `chibi doc` / `chibi-compile --docs` ran on the C front end. They are archived while the compiler moves to Chibi.

- `src/authoring/project_format*`, `source_format*`, `source_minimize*`, `source_refactor_text*`: canonical formatting of project assets and package sources.
- `src/authoring/project_docs.c`: the site runner, cut from `project_compile.c`.
- `tools/docs/`: the Chibi site generator (Markdown, reference, grammar, site).
- `test/language/format.c`, `test/language/docs_markdown*`, `test/tools/published_docs.c`: their tests.

## Game-project compile path (authoring, host)

`chibi-compile --output/--check/--explain-costs/--modules/--resources-only/--stage-one-input` and the `chibi build/check/export/web` and run-a-game paths prepared game projects through the C front end. `chibi-compile` now only runs `--test`; the CLI reports "game projects are archived".

- `src/authoring/project_compile.c` (and its full header), `project_prepare*`, `component_declarations`, `prototype_prepare`, `scene_prepare`, `material_prepare`, `sound_prepare`, `ui_prepare*`, `source_costs`, `material_attach`, `prototype_attach`.
- `src/host/export.c` and `export.h`: exported folders, apps and web sites.
- `tools/toolchain/StageOne.chibi`: the stage-one project route.
- `test/host/export.c`, `test/tools/GameEquivalence.chibi`.

## Project script pipeline tests (test/language, test/runtime, test/tools)

These compiled project scripts through the stage-zero `authoring/source_prepare`/`source_collection`/`source_catalog` pipeline; they leave with it. Parked with the games, the runtime port and script behaviors (focus.md). Re-add as authored `@test` when script behaviors come back:

- **ECS sessions running script behaviors:** `headless`, `incremental`, `incremental_benchmark` (reload of changed sources, binding schedule), `modules` (imports between project scripts), `native_calls` (natives called from scripts on both backends), `navigation` (references/definitions over analyzed documents), `source_assets`, `scene_snapshot_read` (authored state from scenes).
- **Runtime behavior from source (test/runtime):** `owned_tick`, `parallel_tick` (tick ownership and parallel ticks), `whole_alias` (compiled aliases), `source_features` (native features, numeric queries, material features, input save), `source_reductions` (reductions and benchmarks).
- **Cases removed from kept files** (text in `archive/test/*/…_group1_cases.txt`): 12 `package_compiler_chibi.c` cases that compared the authored compiler with script libraries compiled through the project pipeline (executable libraries, library name resolution, scalar-runs-natively-like-the-VM, deferred char states, numeric conversion rejection, constant folding as stage zero does, invalid library closures), and one `build_process.c` case.

## Studio-only SDK authoring (dead-code sweep)

Only Studio called these; no engine product or test linked them. Studio, which is parked, loses its application catalog, UI loader and checker, palette ranking, call hierarchy, prototype instances, field parameter lists and model baking with them:

- `src/authoring/application_catalog.c`, `studio_catalog.c` (the `studio_catalog` SDK target): Studio's native catalog.
- `src/authoring/ui_loader.c`, `ui_check.c`: UI asset loading and RmlUi checking for the designer.
- `src/assets/call_hierarchy.c`, `prototype_instance.c`, `field_parameters.c`: asset authoring services.
- `src/support/text_rank.c`: command palette ranking.
- `src/runtime/models/model_bake.c` (the `model_bake` SDK target): model preview baking.

## KDL project content readers (src/assets, src/authoring)

Game projects are archived, so nothing in the toolchain reads project content any more. These C readers of the KDL project formats leave with their tests; project content returns as typed Chibi declarations:

- `src/assets/project.c` (the manifest reader; `src/assets/project.h` keeps only `CHIBI_PROJECT_VERSION`), `input_map.c`, `physics_settings.c`, `prototype.c`, `scene_document.c`, `scenario_document.c`.
- `src/authoring/native_prototype.c`, `scene_hierarchy.c`, `scene_snapshot.c`, `component_presentation.c` and `engine_components.kdl`, the Properties tabs of the engine's components.
- `docs/scene-authoring.md`; tests `test/authoring/{prototype_documents,scene_hierarchy,project_document,component_presentation}.c`, `test/language/prototype.c`, two scenario cases of `test/tools/native.c` and four scene and prototype reader cases of `test/language/asset_compiler_host.c`.

## Native player, renderer and UI host (src/render, src/ui, src/platform, src/host)

`chibi-player` and everything only it (and the render tests) linked leave the build. Games cannot be played, rendered, previewed or exported until a Chibi renderer replaces them; the compiler, build host, `chibi-compile` and the launcher's compile path stay.

- `src/render` (the renderer, shaders and `.shd`/`.frag` sources, audio output, scene3d, materials, HRC): `scene3d_parts`, `shader_catalog`, `shader_parameters`, `shader_values` and the headers they need stay for `chibi_visual_catalog` until project compilation drops its shader catalog.
- `src/platform` (Cute glue), `src/support/{resources,number_expr,ui_style}`, `src/ui/{rml*,controls/**,drag,list,popover,iframe,motion,focus_within,autofocus,json_items,iframe_profile,input_feel,text_motion}` (the RmlUi host and its controls).
- `src/host/{player_main,player_url,player_clock}`: the player entry point, its URL gestures and clock; `src/host/web/` (browser page and data loader).
- `src/authoring/studio_{schema,definition,native,project}`: the Studio type pack and schema, `chibi_studio_capabilities` and `chibi_project_preparation`.
- Tests: `test/render/**`, `test/draw_backend.cpp`, `test/authoring/render_resources.c`, `test/tools/{audio_output,device_controls,hrc_spot,particle_fields,scene3d_camera,scene3d_terrain,scene3d_volume,terrain_cut_benchmark,terrain_playable_render,player_url,player_clock,webgpu_staging,shader_webgpu}.c` and their selectors in `test_tools`, `tools/agent.py`, `tools/test_policy.py` and `test/execution_selectors.h`.
- Lost functionality: running a game (`chibi run`, `chibi PATH`, `make player-run`), `chibi export` (desktop and web) and `chibi web`, the web player, the GPU render and audio test selectors (`--radiance*`, `--scene3d*`, `--material-sampling`, `--scene2d`, `--interior-border`, ...), and the Studio type pack. The launcher app (`apps/chibi`), `tools/toolchain/{RuntimeSDK,Graph}.chibi`, `tools/graph/sdk/Evidence.chibi`, `tools/package_sdk.py` and the runtime SDK tests still name `chibi-player` as a product; they cannot build a real player and should be deleted with the runtime SDK.

## Game runtime and its tests (src/runtime, src/ecs, src/render shader catalog)

With games archived, the compiler path includes nothing of the game runtime. These leave with their tests; the standalone native catalog (`host_native_standalone_catalog_new`) is the only catalog the toolchain builds:

- `src/runtime`: audio (+analysis), physics, physics3d, materials (all `material_*`), particles, save, terrain storage, tilemap, ui (+`ui/`), url, visibility, visual parts/transform, transform propagation, input (engine, source, definition), player, engine, world, `camera`, `declared_components`, `math`, `type_definitions`; `behaviors/{entities*,entity_query,scene_flow*,tilemap_definition}`, `fields/*`, `geometry/*` (meshes, distance transform, terrain volume), `scenes/*` (physics, prototypes, scene prepared), `images/*` kept only as far as the shader catalog needs, `shaders/*`, `models/*` and `systems/access` where unreferenced.
- `host_native_default_catalog_new` and the game packs it assembled (`audio`, `entities`, `tilemap`, `fields`, `state`, `save`, `terrain_storage`, `scene`, `particles`, `ui`, `url`, `physics*`, `materials`, `visibility`, `input`), `authoring/renderer_catalog`, `authoring/image_prepare`.
- Tests: `test/runtime/{materials,material_*,particles}.c`, `test/tools/{physics,physics3d,native,native_catalog,transforms,tilemap*,terrain_*,material_surface_mesh,audio_analysis,shader_bake,shader_source_debug,scene_prepared}.c`, `test/language/{field_runtime,record_services}.c`, `test/authoring/sky_files.c`, and `test/tools/test_policy.c` (the policy runner's own test; `chibi build test` replaces it). The `materials` test group and its Make/CI entries go too.
- Lost functionality: simulation, rendering inputs, audio, saves, tilemaps, particles, terrain, game ECS scheduling and the native catalog for games. The VM keeps ECS queries (`src/ecs`), the scheduler and the source publication code it needs.
- Not touched: the CMake SDK build (`sdk/*.cmake`) still names these sources; its owner prunes or deletes it.

## Authored-compiler differentials archived without a port (test/language, test/tools)

Each called authored compiler packages through `compiler_fixture` (stage-zero prepared) and compared them with stage-zero or Tree-sitter results. Re-author the ones worth keeping as `@test` in `tools/compiler/<package>/tests`:

- `argument_effects_chibi.c` (argument-effect metadata and effect order; a `@test` closure importing `package_compiler` exceeds the 4e9-instruction compile budget), `bounded_compiler_chibi.c` (bounded Compile on the bounded corpus), `compiler_artifact_chibi.c` and `test/tools/compiler_artifact.c` (artifact write by `test_language` read back by `test_tools` with the compiler host).
- Kept: `package_compiler_chibi.c` (its bootstrap, native, scalar and types selectors drive seed refresh and the native lanes), `native_census.c`, `host_packages.c`.

## Third-party archive builder (third_party/CMakeLists.txt, .github/workflows/third-party.yml, test/tools/third_party_publish.c)

The prebuilt vendor archives in `third_party/archives.tsv` are published and pinned, as Odin ships `vendor/`. The CMake builder, its publishing workflow and the `third-party-publish` tool test (with `test/fixtures/third_party_publish`) are archived and not run. To rebuild an archive from upstream: restore `archive/third_party/CMakeLists.txt` to `third_party/`, configure and build it as `archive/.github/workflows/third-party.yml` does, upload the installed tree with `gh release upload`, and pin the SHA-256 in `archives.tsv`. It checks each library out at the commit named in the file and applies the patch directories left in `third_party/` in order. `compiler-runtime-seed.yml` reads the sljit and tree-sitter source pins from the archived file for the corresponding-source notices.

The third-party builder above is kept with `tools/publish_third_party.py`, which publishes what it builds.

## Deleted once replaced (2026-10-30)

The owner's rule: archived code stays only while a Chibi replacement is still to come. These groups
were deleted because Chibi already replaced them, or because the goal removes their infrastructure
outright (git history keeps them):
- the CMake build, the SDK's CMake package and the player and graph CMake files, replaced by `chibi build`;
- the Python build and test helpers, replaced by `chibi build` and `chibi test`;
- the compiler C differentials ported to `@test` (tokenizer, parser, package paths, routes, syntax,
  syntax diagnostics, package graph, unit algebra, type relations, asset packaging, shader source) and
  the fixture-run `@test` wrappers. Asset packaging lost its two-backend runs, the formatted-byte work bounds, the
  private `Join` probes and the asset generator's `Quote`/`Array`/`Join` cases: the generator
  (`tools/graph/asset_compiler`) uses build-host natives that `chibi test` does not provide. Shader source
  lost its two-backend runs, call counts, results outliving the fixture and its arenas, and copying lowered
  rows into the native `shader_graph` to evaluate them (its `@test` asserts the exact node rows instead);
- the stage-zero dialect and front-end tests (analyzer, records, language features, programs and
  queries, benchmark, wasm and layout, prepared round trip and C export, package execution, numeric and
  readers), which tested a retired dialect against a retired front end;
- unreferenced C from the dead-code sweep;
- the KDL value layer, ckdl, the unread KDL fixtures, the KDL game-equivalence stories and the player
  packaging license check.

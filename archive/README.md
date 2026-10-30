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

## Stage-zero script tests (test/language)

These tests compiled the stage-zero script dialect (`library` blocks, scriptless top level, paren-less `if`, `and`/`or`, handler/component/system script syntax) with `script_parse`/`script_analyze`/`script_lower`, so they leave with stage zero. Re-add the behavior as authored `@test` (or package-dialect fixtures) when needed:

- **Analyzer subject tests:** `semantic.c`, `record_semantic.c`, and the `*_chibi.c` differentials (named, lexical and nominal binding, unit and effect analysis, constant proof and scalar, type constructors, callable headers, union and record members). Diagnostics, ranges, recovery and C-vs-authored equivalence.
- **Language features through the VM:** borrow, iteration, maps, operations, generic, scalarize, effects, symbol references. Borrow flags and call-scoped views, map insertion order and nesting, iteration lifetimes, operator ABI generation, generic constructed types, scalar optimization patterns, effect publication.
- **Program and query runtime:** program (313 cases: bindings, handlers, scene bindings), query program/values/boundary, publication, retention of prepared results, reductions, native char results.
- **Records:** record publication, runtime and state.
- **Execution paths:** SLJIT and wasm execution of script modules (`wasm.c`), binding schedule, activation cache, component write, runtime attribution, packed and row array layouts, outliner projection, inferred string arrays, and the script/binding/activation/borrow benchmarks.
- **Not yet archived** (they cross the stage-zero producer and the `test_tools` consumer): the prepared round trip (`prepared.c`, `record_producer.c`, `record_prepared_runtime.c`, `test/tools/prepared.c`, `c_export.c`, `record_c_export.c`) and `compositional_records.c`, whose source generators `package_compiler_chibi.c` uses.

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

## Unreferenced C (dead-code sweep)

No linked product (`chibi`, `chibi-compile`, `chibi-player`, the build and compiler hosts, `test_tools`, `test_language`) or Studio reached these objects after the format, docs, game-compile and network archives:

- `src/execution/c.c`: C source emission for compiled behavior functions and sequences.
- `src/execution/inputs.c`: frame-argument reconstruction for debuggers.
- `src/authoring/visual_prepare.c`: scene visual preparation for the archived project pipeline.
- `src/support/code_lex.c`: the graph-expression lexer.

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

## KDL text and ckdl (src/support, third_party)

With the project content readers archived and the bootstrap contract read as plain lines, nothing linked the KDL value layer:

- `src/support/asset_text.c`: KDL documents to and from asset values; tests `test/support/asset_text.c`, `test/asset_fixture.h`.
- `third_party/ckdl`: the vendored KDL parser, its license notices in the SDK and compiler runtime seed.
- `src/authoring/parts_prepare.c`, `mesh_prepare.c` (the `shader_preparation` target): KDL mesh-part preparation; three render cases that prepared parts.

## Native player, renderer and UI host (src/render, src/ui, src/platform, src/host)

`chibi-player` and everything only it (and the render tests) linked leave the build. Games cannot be played, rendered, previewed or exported until a Chibi renderer replaces them; the compiler, build host, `chibi-compile` and the launcher's compile path stay.

- `src/render` (the renderer, shaders and `.shd`/`.frag` sources, audio output, scene3d, materials, HRC): `scene3d_parts`, `shader_catalog`, `shader_parameters`, `shader_values` and the headers they need stay for `chibi_visual_catalog` until project compilation drops its shader catalog.
- `src/platform` (Cute glue), `src/support/{resources,number_expr,ui_style}`, `src/ui/{rml*,controls/**,drag,list,popover,iframe,motion,focus_within,autofocus,json_items,iframe_profile,input_feel,text_motion}` (the RmlUi host and its controls).
- `src/host/{player_main,player_url,player_clock}`: the player entry point, its URL gestures and clock; `src/host/web/` (browser page and data loader).
- `src/authoring/studio_{schema,definition,native,project}`: the Studio type pack and schema, `chibi_studio_capabilities` and `chibi_project_preparation`.
- `sdk/desktop.cmake`, `sdk/preview.cmake`, `tools/package_player.py`: the player's CMake definition and packaging.
- Tests: `test/render/**`, `test/draw_backend.cpp`, `test/authoring/render_resources.c`, `test/tools/{audio_output,device_controls,hrc_spot,particle_fields,scene3d_camera,scene3d_terrain,scene3d_volume,terrain_cut_benchmark,terrain_playable_render,player_url,player_clock,webgpu_staging,shader_webgpu}.c` and their selectors in `test_tools`, `tools/agent.py`, `tools/test_policy.py` and `test/execution_selectors.h`.
- Lost functionality: running a game (`chibi run`, `chibi PATH`, `make player-run`), `chibi export` (desktop and web) and `chibi web`, the web player, the GPU render and audio test selectors (`--radiance*`, `--scene3d*`, `--material-sampling`, `--scene2d`, `--interior-border`, ...), and the Studio type pack. The launcher app (`apps/chibi`), `tools/toolchain/{RuntimeSDK,Graph}.chibi`, `tools/graph/sdk/Evidence.chibi`, `tools/package_sdk.py` and the runtime SDK tests still name `chibi-player` as a product; they cannot build a real player and should be deleted with the runtime SDK.

## Game runtime and its tests (src/runtime, src/ecs, src/render shader catalog)

With games archived, the compiler path includes nothing of the game runtime. These leave with their tests; the standalone native catalog (`host_native_standalone_catalog_new`) is the only catalog the toolchain builds:

- `src/runtime`: audio (+analysis), physics, physics3d, materials (all `material_*`), particles, save, terrain storage, tilemap, ui (+`ui/`), url, visibility, visual parts/transform, transform propagation, input (engine, source, definition), player, engine, world, `camera`, `declared_components`, `math`, `type_definitions`; `behaviors/{entities*,entity_query,scene_flow*,tilemap_definition}`, `fields/*`, `geometry/*` (meshes, distance transform, terrain volume), `scenes/*` (physics, prototypes, scene prepared), `images/*` kept only as far as the shader catalog needs, `shaders/*`, `models/*` and `systems/access` where unreferenced.
- `host_native_default_catalog_new` and the game packs it assembled (`audio`, `entities`, `tilemap`, `fields`, `state`, `save`, `terrain_storage`, `scene`, `particles`, `ui`, `url`, `physics*`, `materials`, `visibility`, `input`), `authoring/renderer_catalog`, `authoring/image_prepare`.
- Tests: `test/runtime/{materials,material_*,particles}.c`, `test/tools/{physics,physics3d,native,native_catalog,transforms,tilemap*,terrain_*,material_surface_mesh,audio_analysis,shader_bake,shader_source_debug,scene_prepared}.c`, `test/language/{field_runtime,record_services}.c`, `test/authoring/sky_files.c`, and `test/tools/test_policy.c` (the policy runner's own test; `chibi build test` replaces it). The `materials` test group and its Make/CI entries go too.
- Lost functionality: simulation, rendering inputs, audio, saves, tilemaps, particles, terrain, game ECS scheduling and the native catalog for games. The VM keeps ECS queries (`src/ecs`), the scheduler and the source publication code it needs.
- Not touched: the CMake SDK build (`sdk/*.cmake`) still names these sources; its owner prunes or deletes it.

## Stories (test/stories)

The game-equivalence stories: five small game projects, each played by a KDL scenario, that compared a reference and a candidate compiler's game artifacts. They left with game projects and the game-equivalence harness.

## Fixture-run @test wrappers (test/language)

`package_core.c`, `package_tests.c`, `package_portable.c`, `package_portable_compiler.c` and `integer_arithmetic.c` ran the @tests of core and portable packages through `compiler_fixture_tests*` on both backends, compiled by stage zero. `tools/test_policy.py` now runs `chibi test packages/core` and `chibi test test/integer_arithmetic` (`chibi-test-core`, `chibi-test-integer-arithmetic`) instead. Lost: both-backend and sanitizer runs of those suites, the package-discovery edge cases of `package_tests.c` (old `package_tests_find*`), and the portable consumer/compiler package fixtures (`test/packages/portable_consumer` has no runner now; re-add `chibi test` for it once its imports resolve).

## Compiler unit tests moved to @test (tools/compiler/<package>/tests)

Each differential that called an authored compiler package's exports from C is replaced by table-driven `@test` functions in `tools/compiler/<package>/tests/Tests.chibi` (it imports the package as `project:<package>`; run `chibi test tools/compiler`, which imports only directories that declare an `@test`; the folder is not a compiler source folder, so seed recovery is unaffected). The C differential is archived.

- `source_tokenizer_chibi.c` → `tools/compiler/source_tokenizer/tests`: the 12 hand-written expectation cases (range tokens, statement terminators, Odin error messages and offsets). Lost: the corpus and source differentials against the Tree-sitter token stream.
- `source_parser_chibi.c` and `SourceParserDiff.chibi` → `tools/compiler/source_parser/tests`: 18 short sources, one per construct family (attributes, `@error` unions, `@test`, behavior, component, library, prototype, relationship, system, return tuples, `when`, operators, collections, `for`) plus the four invalid corpus files with their diagnostic line and column. Lost: the differential over every `.chibi` file in the repository and the full-size `test/language/syntax_corpus` files against Tree-sitter. Note: a test source must stay small (a test has a 100M-instruction budget) and `length(string)` in a test hangs the compile.
- `package_paths_chibi.c` → `tools/compiler/package_paths/tests`: 22 @tests (21 of the 26 requests plus a missing-path request, each with the C resolver's result). Lost: the three long-path requests (segment over 255 bytes, absolute paths at 4095/4096 bytes, joined-limit-before-collapse) and the detached-results lifetime checks.
- `package_routes_chibi.c` → `tools/compiler/package_routes/tests`: 37 @tests (11 root-set rejections and accepted roots, 11 file routes, 17 import routes, with the results the C router gave). Lost: the raw UTF-8 and surrogate-pair import literals, the 134-call reuse of one preparation per backend, and the detached-results lifetime case.
- `package_syntax_chibi.c` → `tools/compiler/package_syntax/tests`: 5 @tests (header only, imports with and without alias with exact ranges, blank name, missing header, syntax error first). Lost: the 32 raw-parse transports of every child and incoming field name against Tree-sitter and the edited-snapshot case.
- `syntax_chibi.c` → `tools/compiler/syntax_diagnostics/tests`: 5 @tests (valid source, four rejected sources with their diagnostic line and column). Lost: the shared-corpus comparison of raw trees against Tree-sitter snapshots and the incremental UTF-8 and recovery edits.

## Authored-compiler differentials archived without a port (test/language, test/tools)

Each called authored compiler packages through `compiler_fixture` (stage-zero prepared) and compared them with stage-zero or Tree-sitter results. Re-author the ones worth keeping as `@test` in `tools/compiler/<package>/tests`:

- `argument_effects_chibi.c` (argument-effect metadata and effect order), `bounded_compiler_chibi.c` (bounded Compile on the bounded corpus), `compiler_artifact_chibi.c` and `test/tools/compiler_artifact.c` (artifact write by `test_language` read back by `test_tools` with the compiler host), `asset_packaging_chibi.c`, `shader_source_chibi.c` (shader source packaging).
- `type_relations_chibi.c`, `unit_algebra_chibi.c` (relation and unit tables against the authored packages), `package_snapshot_chibi.c` + `package_resolution.c` (snapshot Build, option validation, path-before-size and parse-before-route ordering, UTF-8 and budget validation), `package_graph_chibi.c` + `packages.c` (package graph).
- Kept: `package_compiler_chibi.c` (its bootstrap, native, scalar and types selectors drive seed refresh and the native lanes), `native_census.c`, `host_packages.c`.

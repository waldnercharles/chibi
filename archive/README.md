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

## Stage-zero oracle cases removed from package_compiler_chibi.c

`test/language/package_compiler_chibi.c` no longer includes `language/lower.h` or `language/semantic.h`. Removed (deleted, not archived; the authored compiler has no stage-zero to compare against): `oracle_packages` and the cases that compared authored compilation with stage-zero analysis or lowering: compound diagnostics vs stage zero, runnable package/typed/collection/importing sample tables (plain and `_compiled`), fallible source results, builtin math source contracts, invalid/deferred char conversion rejections, generic char native results, `native_overload_reference_contract` and `native_arguments_reference_contract`, and the `--package-compiler-chibi-native-arguments-oracle` selector. Authored `@test` suites and the remaining bootstrap/native lanes cover the authored side only. `compositional_records.c` lost an unused `language/lower.h` include.

## Stage-zero authored-program tests archived (test/language)

Each compiled authored sources with stage zero (`source_package_prepare`, `authoring_packages_prepare`, `compiler_fixture`) and ran them. `chibi test` and `host_packages_compiler.c` (authored compile path) cover the authored side; what is not re-covered:

- `package_imports.c`: transitive-call execution on each backend, alias scoping, record/union/aggregate-constant imports, input-order and root-relocation id stability, incremental reuse and cached closures, generic export and request scoping.
- `package_files.c`: snapshot loading from disk (canonical roots, NUL/oversize rejection, junction/UNC handling on Windows, relocation and reload invalidation).
- `package_math.c` (+ `--package-math-benchmark`): shipped `core:math` authored tests, IEEE/precision import checks, domain rejection, explicit f64 narrowing, catalog retirement of native math names, invocation benchmark.
- `integer_semantics.c` (+ `--integer-shift-counts-authored`): width-by-width integer pinning and shift counts through stage zero.
- `host_packages.c`: import aliases and shadowing, private-kernel precedence, member rejection, unavailable target, host:clock grant transport, sample retention, imported-wrapper effects, distinct domains, platform monotonic samples, `core:time` authored tests.
- `build_workflow.c` (+ its source header): configure/build order, failure propagation and missing-binding rejection for the authored build workflow service.

## Fixture-compiled sequence cases removed from execution.c

`test/language/execution.c` no longer includes `compiler_fixture.h` or `authoring/source_package_prepare.h`. Seven cases compiled `project:sequences` sources through stage zero and called them on each backend: sequence values copying at ownership boundaries, scoped lent locals, strict sequence ranges, pointer views following rebinding, sequence-place evaluation order, calls lending arguments (callees copying writes), and lent records copying nothing. Deleted; the hand-built-module cases in the file remain.

## native_census.c no longer uses compiler_fixture

The census loads the compiler with `compiler_bootstrap_load` (the sources on disk, recovered from the checked-in seed) and runs `CollectScalar` through `compiler_host_invoke` with the target taken from `compiler_target_json`, instead of preparing the compiler with stage zero. Report format and flattening are unchanged. Not run locally when it landed (all heavy slots were held); the first run of `--native-census-flattening` verifies it.

## Stage-zero bootstrap lane removed (package_compiler_chibi.c)

`package_compiler_compiles_itself` (`--package-compiler-chibi-bootstrap`, `make test-bootstrap`) had stage zero prepare stage one, compile the compiler sources to stage two, and compare stage one and stage two byte for byte on every sample. `chibi build bootstrap-artifact` now writes the seed from recovery's fixed point (seed -> stage one -> stage two, `--recover`), which checks the same fixed point without stage zero. Not re-covered: stage one against stage zero on the samples.

## package_compiler_chibi.c reduced to the native and scalar lanes

Deleted (stage zero prepared the compiler, then each lane compared the authored compiler with stage zero or ran its output): the plain, `_compiled`, `_types`, `_types_compiled`, `_collections`, `_collections_compiled`, `_imports`, `_imports_compiled`, `_fallible`, `_literals`, `_behaviors`, `_behaviors_rest`, `_actions`, `_queries`, `_libraries`, `_sharing`, `_debug_free` and `_math_native` selectors, and the `_records` lane with `compositional_records.c` (archived, with its source header and `Values.chibi`). The authored `@test` suites under `tools/compiler` and `chibi test` cover the compiler's own behavior; not re-covered: authored-compiler output executing on both backends against the same sample tables. Kept for now, still on `compiler_fixture`: `--package-compiler-chibi-scalar`, `--package-compiler-chibi-native` (+ `-native-call`) and `--package-compiler-chibi-native-arguments`.

## Emitter regressions now run under chibi-test-compiler

The authored emitter `@test`s moved from `test/native_aot/emitter_calls` and `test/native_aot/emitter` to `tools/compiler/llvm_native/tests` and `tools/compiler/native_abi/tests`, so `chibi test tools/compiler` (`chibi-test-compiler`) runs them; before, only the stage-zero `compiler_fixture_tests` ran them (and the orphaned `Tests() -> int` group ran nowhere), so they had drifted and were fixed (typed string constants became functions, `Emit`'s arity). The two C emitter cases `native_libraries_export_words` and `native_strings_emit` became `Libraries.chibi` and `Strings.chibi` there. The C lane `--package-compiler-chibi-native-arguments` was deleted: `test/compiler/samples/native_arguments` (`chibi-test-samples-native-arguments`) covers the authored side; the stage-zero reference comparison is gone.

## compiler_fixture and the stage-zero scalar lane

`test/language/compiler_fixture.[ch]` (stage-zero preparation of authored packages, `pin_value` calls, record shapes read from `authoring_packages` metadata, its ownership self-tests and `--compiler-fixture`) are archived. The kept lanes in `package_compiler_chibi.c` now go through `test/language/scalar_host.c`: the producer is the compiler `compiler_bootstrap_load` recovers (the same one the census uses), the consumer packages compile to one closure behind a small root package, and rows cross as JSON, read back as `scalar_facts.Row` records. Each lane runs on the host's one backend rather than on bytecode and SLJIT alternately. Changes in coverage: the two-libraries-in-one-source rejection (it expected stage zero's "Unexpected syntax"; the authored parser accepts it) is gone, and the "rows outlive the producer" assertions now only check that rows live in the caller's arena. `--package-compiler-chibi-scalar` passes (6 of 6) here; the Linux x86-64 native lane compiles but could only be syntax-checked on this host.

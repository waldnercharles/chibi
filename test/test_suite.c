#include "test/instance_bank_fixture.h"
#include "test/pending_fixture.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_multigate_fixture.h"
#include "test/whole_name_fixture.h"
#include "test/whole_owned_array_fixture.h"
#include "test/whole_particle_fixture.h"
#include "test/whole_random_fixture.h"
#include "test/whole_reaction_fixture.h"
#include "test/whole_reflected_fixture.h"
#include "test/whole_retrigger_fixture.h"
#include "test/whole_rhythm_fixture.h"
#include "test/whole_search_fixture.h"
#include "test/whole_state_fixture.h"
#include "test/whole_switch_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define PICO_UNIT_IMPLEMENTATION
#include "suite_internal.h"

void test_automation_graph_edit();
void test_compiled_behaviors();
void run_editor_command_checks();
void test_modeling();
void test_model_sampled();
void test_shader_detail();
int test_shader_detail_gpu(const char *program);
void test_model_surface();
void test_model_reuse();
void test_model_parameters();
void test_model_profiles();
void test_model_materials();
void test_model_validation();
void test_model_inspection();
void test_model_material_scope();
void test_model_export();
void test_model_bake();
void test_model_constructive();
void test_collab_protocol();
void test_collab_replay();
void test_automation_search();
void test_automation_navigation();
void test_automation_graph_optimize();
void test_automation_prototype_edit();
void test_automation_scene_edit();
void test_automation_asset_write();
void test_automation_refactor();
void test_automation_preview_registration();
void test_automation_profile_scenario_inputs();
void run_prototype_preview_lifetime_check(void);

int arena_play_run(const char *program);
int compiled_arena_run(const char *program);
int arena_scene_scale_run(const char *program, int count, int frames, bool profile);
int bench_arena_scale(int count, int frames, bool portable, bool profile, int mode);
int bench_neon_pursuit(int count, int frames, bool portable, bool profile);
int whole_function_audit();
int arena_play_review_run(const char *program, bool editor_mode, bool crowd);
void run_behavior_loader_checks();
void run_entity_presence_checks();
void test_surface_cpu();
void test_shader_effects_cpu();
void test_shader_image_cpu();
void test_png_encoding();
void test_physics();
void test_materials();
void test_material_reactions();
void test_material_thermal();
void test_material_detail();
void test_material_mpm();
void test_material_obstacles();
void test_material_particle_index();
void test_material_gravity();
void test_material_simulation();
void test_material_constitutive();
void test_material_mixed();
void test_material_blast();
void test_fields();
void test_system_schedule();
int bench_fields();
int bench_field_slices();
int bench_system_schedule();
void test_game_capabilities();
void test_dash();
void test_encounter();
void test_system_arithmetic();
void test_reference_index();
void test_refactor();
void test_system_nodes();
void test_component_presentation();
void test_file_watch();
void test_text_overlay();
void test_studio_commands();
void test_ui_document();
void test_game_ui();
void test_game_ui_presenter();
void test_kit_host();
void test_input_bindings();
void test_graph_optimizer();
int scene_documents_run(const char *program);
int studio_documents_run(const char *program);
int studio_models_run(const char *program);
int studio_properties_run(const char *program);
/* Lists and drag (workstream F4). */
int studio_lists_run(const char *program);
/* The Outliner (workstream P2). */
int studio_outliner_run(const char *program);
int studio_output_run(const char *program);
void test_ui_lists();
/* Members (workstream P5). */
int studio_members_run(const char *program);
int studio_inspector_run(const char *program);
int studio_shell_run(const char *program);
/* The Scene area (workstream C1). */
int studio_scene_run(const char *program);
/* The editor chrome (workstream C2). */
int studio_chrome_run(const char *program);
int studio_palette_run(const char *program);
int studio_graph_chrome_run(const char *program);
int studio_panels_run(const char *program);
/* The visual designer (workstream D1). */
int studio_designer_run(const char *program);
int studio_graph_inputs_run(const char *program);
/* Asset thumbnails. */
void test_studio_minimap();
int studio_thumbnails_run(const char *program);
bool test_rmlui_kit();
bool test_rmlui_binding();
/* Activations' arguments, event context and anchor, and contextmenu
   (test/editor/ui_activations.c, workstream F1). */
void test_ui_activations();
/* Game documents' events reach behaviors in the update that follows, typed
   (test/editor/game_ui_events.c). */
void test_game_ui_events();
/* Studio's editor nodes only in Studio's project
   (test/editor/studio_nodes_gate.c). */
void test_studio_nodes_gate();
/* The S0 UI bench (test/editor/ui_bench.h). */
int ui_bench_headless();
int ui_bench_studio(const char *program, bool members_only);
int rmlui_render_run(const char *program, bool gles);
/* Trackpad pinch (G). */
void test_studio_pinch();
/* UI source maps (workstream A) */
void test_ui_sources();
/* UI documents (workstream D). */
void test_ui_documents();
/* UI Inspector */
void test_ui_inspector();
/* UI language service (workstream B) */
void test_ui_source();
void test_ui_check();
void test_ui_pages();
/* UI wiring (workstream K) */
void test_ui_wiring();
/* Workstream C: the code editor. */
bool test_kit_code();
/* Workstream I: code intelligence in the Text Editor. */
void test_code_intelligence();

/* ---------- Settings (workstream F) ---------- */
void test_studio_settings();
/* The Scene view's game preview while editing. */
void test_scene_view();
/* One selection model, commands on the selection and the object clipboard. */
void test_scene_selection();
/* The graph debug target while the game runs. */
void test_debug_target();
/* Visual designer (workstream H). */
void test_ui_designer();

/* ---------- Authored status bar (workstream J) ---------- */
void test_kit_slot();

/* ---------- Wave 2 follow-ups (workstream P) ---------- */
void test_studio_zoom();
/* The Scene's transform tools. */
void test_scene_gizmo(const char *program);

/* ---------- Native elements for authored UI (workstream N) ---------- */
void test_ui_motion();

/* ---------- arena2d end to end (wave 3, workstream X) ---------- */
void test_arena_hud();

/* ---------- One document per area (workstream F5) ---------- */
void test_kit_documents();
/* ---------- Native controls (workstream F2) ---------- */
void test_ui_controls();
/* ---------- Popovers (workstream F3) ---------- */
void test_ui_popovers();

static int fails;

static bool test_timing_enabled;

static double test_clock_seconds()
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

#define TEST_TIMED_CALL(function)                                                         \
    do {                                                                                  \
        if (test_timing_enabled) {                                                        \
            double started = test_clock_seconds();                                        \
            function();                                                                   \
            printf("TEST_TIMING: %s %.3fs\n", #function, test_clock_seconds() - started); \
        } else {                                                                          \
            function();                                                                   \
        }                                                                                 \
    } while (0)

#define TEST_TIMED_EXPR(expression)                                                         \
    do {                                                                                    \
        if (test_timing_enabled) {                                                          \
            double started = test_clock_seconds();                                          \
            expression;                                                                     \
            printf("TEST_TIMING: %s %.3fs\n", #expression, test_clock_seconds() - started); \
        } else {                                                                            \
            expression;                                                                     \
        }                                                                                   \
    } while (0)

#define TEST_TIMED_CHECK(function, description)                                           \
    do {                                                                                  \
        bool passed;                                                                      \
        if (test_timing_enabled) {                                                        \
            double started = test_clock_seconds();                                        \
            passed = function();                                                          \
            printf("TEST_TIMING: %s %.3fs\n", #function, test_clock_seconds() - started); \
        } else {                                                                          \
            passed = function();                                                          \
        }                                                                                 \
        test_check(passed, description);                                                  \
    } while (0)

void test_check(int ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) {
        fails++;
    }
}

static char *program_name;

TEST_CASE(all_checks)
{
    stacktrace_init(program_name);
    TEST_TIMED_CALL(registry_validate);
    TEST_TIMED_CALL(test_automation_graph_edit);
    TEST_TIMED_CALL(test_automation_search);
    TEST_TIMED_CALL(test_automation_navigation);
    TEST_TIMED_CALL(test_automation_graph_optimize);
    TEST_TIMED_CALL(test_automation_prototype_edit);
    TEST_TIMED_CALL(test_automation_scene_edit);
    TEST_TIMED_CALL(test_automation_asset_write);
    TEST_TIMED_CALL(test_automation_refactor);
    TEST_TIMED_CALL(test_automation_preview_registration);
    TEST_TIMED_CALL(test_automation_profile_scenario_inputs);
    TEST_TIMED_CALL(run_content_catalog_checks);
    TEST_TIMED_CALL(test_reference_index);
    TEST_TIMED_CALL(test_refactor);
    TEST_TIMED_CALL(run_graph_model_checks);
    TEST_TIMED_CALL(run_graph_document_checks);
    TEST_TIMED_CALL(run_graph_comment_checks);
    TEST_TIMED_CALL(run_graph_find_checks);
    TEST_TIMED_CALL(run_graph_describe_checks);
    TEST_TIMED_CALL(run_graph_problems_checks);
    TEST_TIMED_CALL(run_graph_arrange_checks);
    TEST_TIMED_CALL(run_graph_inline_checks);
    TEST_TIMED_CALL(run_graph_editing_checks);
    TEST_TIMED_CALL(run_graph_collapse_checks);
    TEST_TIMED_CALL(run_runtime_execution_checks);
    TEST_TIMED_CALL(test_compiled_behaviors);
    TEST_TIMED_CALL(test_encounter_backend_parity);
    TEST_TIMED_CALL(run_runtime_instance_checks);
    TEST_TIMED_CALL(run_graph_compile_checks);
    TEST_TIMED_CALL(run_graph_conversion_checks);
    TEST_TIMED_CALL(run_graph_functions_checks);
    TEST_TIMED_CALL(run_graph_signature_checks);
    TEST_TIMED_CALL(test_debugging);
    TEST_TIMED_CALL(run_runtime_arrays_checks);
    TEST_TIMED_CALL(run_runtime_catalog_checks);
    TEST_TIMED_CALL(run_support_format_checks);
    TEST_TIMED_CALL(run_ecs_checks);
    TEST_TIMED_CALL(run_ecs_growth_checks);
    TEST_TIMED_CALL(run_component_read_checks);
    TEST_TIMED_CALL(run_prototype_checks);
    TEST_TIMED_CALL(run_scene_document_checks);
    TEST_TIMED_CALL(run_behavior_checks);
    TEST_TIMED_CALL(run_behavior_loader_checks);
    TEST_TIMED_CALL(run_entity_presence_checks);
    TEST_TIMED_CALL(run_live_checks);
    TEST_TIMED_CALL(run_live_preview_checks);
    TEST_TIMED_CALL(run_prototype_live_checks);
    TEST_TIMED_CALL(test_host_wait);
    TEST_TIMED_CALL(test_entities);
    TEST_TIMED_CALL(test_projectile);
    TEST_TIMED_CALL(test_combat);
    TEST_TIMED_CALL(test_physics);
    TEST_TIMED_CALL(test_materials);
    TEST_TIMED_CALL(test_material_reactions);
    TEST_TIMED_CALL(test_material_thermal);
    TEST_TIMED_CALL(test_material_detail);
    TEST_TIMED_CALL(test_material_mpm);
    TEST_TIMED_CALL(test_material_obstacles);
    TEST_TIMED_CALL(test_material_particle_index);
    TEST_TIMED_CALL(test_material_gravity);
    TEST_TIMED_CALL(test_material_simulation);
    TEST_TIMED_CALL(test_material_constitutive);
    TEST_TIMED_CALL(test_material_mixed);
    TEST_TIMED_CALL(test_material_blast);
    TEST_TIMED_CALL(test_fields);
    TEST_TIMED_CALL(test_system_schedule);
    TEST_TIMED_CALL(test_game_capabilities);
    TEST_TIMED_CALL(test_dash);
    TEST_TIMED_CALL(test_encounter);
    TEST_TIMED_CALL(test_targeting);
    TEST_TIMED_CALL(test_pursuit);
    TEST_TIMED_CALL(test_vector);
    TEST_TIMED_CALL(test_arena);
    TEST_TIMED_CALL(test_asset_value);
    TEST_TIMED_CALL(test_asset_source);
    TEST_TIMED_CALL(test_asset_code_checkpoint);
    TEST_TIMED_CALL(run_kdl_text_checks);
    TEST_TIMED_CALL(test_number_serialization);
    TEST_TIMED_CALL(test_auto_weapon);
    TEST_TIMED_CALL(test_graph_navigation);
    TEST_TIMED_CALL(test_extract);
    TEST_TIMED_CALL(test_input_bindings);
    TEST_TIMED_CALL(test_graph_optimizer);
    TEST_TIMED_CALL(test_shader_graph);
    TEST_TIMED_CALL(test_model_sampled);
    TEST_TIMED_CALL(test_modeling);
    TEST_TIMED_CALL(test_shader_detail);
    TEST_TIMED_CALL(test_model_surface);
    TEST_TIMED_CALL(test_model_reuse);
    TEST_TIMED_CALL(test_model_parameters);
    TEST_TIMED_CALL(test_model_profiles);
    TEST_TIMED_CALL(test_model_materials);
    TEST_TIMED_CALL(test_model_validation);
    TEST_TIMED_CALL(test_model_inspection);
    TEST_TIMED_CALL(test_model_material_scope);
    TEST_TIMED_CALL(test_model_export);
    TEST_TIMED_CALL(test_model_bake);
    TEST_TIMED_CALL(test_model_constructive);
    TEST_TIMED_CALL(test_surface_cpu);
    TEST_TIMED_CALL(test_shader_effects_cpu);
    TEST_TIMED_CALL(test_shader_image_cpu);
    TEST_TIMED_CALL(test_png_encoding);
    TEST_TIMED_CALL(test_typed_events);
    TEST_TIMED_CALL(test_component_conversion);
    TEST_TIMED_CALL(test_field_outputs);
    TEST_TIMED_CALL(test_function_wait);
    TEST_TIMED_CALL(test_function_fields);
    TEST_TIMED_CALL(test_particles);
    TEST_TIMED_CALL(test_labels);
    TEST_TIMED_CALL(test_math_expression);
    TEST_TIMED_CALL(test_behavior_math);
    TEST_TIMED_CALL(test_appearance);
    TEST_TIMED_CALL(test_retained_components);
    TEST_TIMED_CALL(test_editor_flow);
    TEST_TIMED_CALL(test_component_fields);
    TEST_TIMED_CALL(test_component_nodes);
    TEST_TIMED_CALL(test_editor_documents);
    TEST_TIMED_CALL(test_tree_watch);
    TEST_TIMED_CALL(test_rcss);
    TEST_TIMED_CALL(test_scene_debug);
    TEST_TIMED_CALL(test_scene_view);
    TEST_TIMED_CALL(test_scene_selection);
    TEST_TIMED_CALL(test_debug_target);
    TEST_TIMED_CALL(test_appearance_geometry);
    TEST_TIMED_CALL(test_mesh_surface);
    TEST_TIMED_CALL(test_mesh_asset);
    TEST_TIMED_CALL(test_orientation);
    TEST_TIMED_CALL(test_movement_input);
    TEST_TIMED_CALL(test_movement_trail);
    TEST_TIMED_CALL(test_sdf_motion);
    TEST_TIMED_CALL(test_material);
    TEST_TIMED_CALL(test_material_lod);
    TEST_TIMED_CALL(test_wave);
    TEST_TIMED_CALL(test_scene_history);
    TEST_TIMED_CALL(test_enemy_attack);
    TEST_TIMED_CALL(test_system_host_wait);
    TEST_TIMED_CALL(test_system_arithmetic);
    TEST_TIMED_CALL(test_system_nodes);
    TEST_TIMED_CALL(run_engine_checks);
    TEST_TIMED_CALL(run_parallel_checks);
    TEST_TIMED_CALL(run_editor_command_checks);
    TEST_TIMED_CALL(test_component_presentation);
    TEST_TIMED_CALL(test_file_watch);
    TEST_TIMED_CALL(test_collab_protocol);
    TEST_TIMED_CALL(test_collab_replay);
    TEST_TIMED_CALL(test_text_overlay);
    TEST_TIMED_CALL(test_studio_commands);
    TEST_TIMED_CALL(test_studio_minimap);
    TEST_TIMED_CALL(test_ui_document);
    TEST_TIMED_CALL(test_game_ui);
    TEST_TIMED_CALL(test_game_ui_presenter);
    TEST_TIMED_CALL(test_kit_host);
    TEST_TIMED_EXPR(test_editor_checkpoint_with_app(program_name));
    /* Trackpad pinch (G). */
    TEST_TIMED_CALL(test_studio_pinch);
    TEST_TIMED_CHECK(test_rmlui_kit, "Studio kit reconciliation, number fields, menus, and command");
    TEST_TIMED_CHECK(test_rmlui_binding, "RmlUi documents load from text, replace their style sheets, mix colors, bind JSON data, follow array edits with constant-time item lookups, lay out text in flex rows, build data-when copies and send input and change from text fields");
    TEST_TIMED_CALL(test_ui_activations);
    TEST_TIMED_CALL(test_game_ui_events);
    TEST_TIMED_CALL(test_studio_nodes_gate);
    /* UI source maps (workstream A) */
    TEST_TIMED_CALL(test_ui_sources);
    /* UI documents (workstream D). */
    TEST_TIMED_CALL(test_ui_documents);
    /* UI Inspector */
    TEST_TIMED_CALL(test_ui_inspector);
    /* UI language service (workstream B) */
    TEST_TIMED_CALL(test_ui_source);
    TEST_TIMED_CALL(test_ui_check);
    TEST_TIMED_CALL(test_ui_pages);
    /* UI wiring (workstream K) */
    TEST_TIMED_CALL(test_ui_wiring);
    /* Workstream C: the code editor. */
    TEST_TIMED_CHECK(test_kit_code, "Code editor: standard and Vim keys, text_edits with undo, :w, host chords, completions and cached rendering");
    /* Workstream I: code intelligence in the Text Editor. */
    TEST_TIMED_CALL(test_code_intelligence);
    /* ---------- Settings (workstream F) ---------- */
    TEST_TIMED_CALL(test_studio_settings);
    /* ---------- Authored status bar (workstream J) ---------- */
    TEST_TIMED_CALL(test_kit_slot);
    /* Visual designer (workstream H). */
    TEST_TIMED_CALL(test_ui_designer);
    /* ---------- Wave 2 follow-ups (workstream P) ---------- */
    TEST_TIMED_CALL(test_studio_zoom);
    TEST_TIMED_EXPR(test_scene_gizmo(program_name));
    /* ---------- Native elements for authored UI (workstream N) ---------- */
    TEST_TIMED_CALL(test_ui_motion);
    /* ---------- arena2d end to end (wave 3, workstream X) ---------- */
    TEST_TIMED_CALL(test_arena_hud);
    /* ---------- One document per area (workstream F5) ---------- */
    TEST_TIMED_CALL(test_kit_documents);
    /* ---------- Native controls (workstream F2) ---------- */
    TEST_TIMED_CALL(test_ui_controls);
    /* ---------- Popovers (workstream F3) ---------- */
    TEST_TIMED_CALL(test_ui_popovers);
    /* ---------- Lists and drag (workstream F4) ---------- */
    TEST_TIMED_CALL(test_ui_lists);

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    return fails == 0;
}

#undef TEST_TIMED_CALL
#undef TEST_TIMED_EXPR
#undef TEST_TIMED_CHECK

static char test_preferences[1024];

static void remove_test_preferences()
{
    remove(test_preferences);
}

static void isolate_test_preferences()
{
    const char *preferences = getenv("CHIBI_STUDIO_PREFS_PATH");
    if (preferences && *preferences) {
        return;
    }
#ifdef _WIN32
    char directory[MAX_PATH];
    assert(GetTempPathA(sizeof directory, directory));
    assert(GetTempFileNameA(directory, "csp", 0, test_preferences));
    assert(_putenv_s("CHIBI_STUDIO_PREFS_PATH", test_preferences) == 0);
#else
    snprintf(test_preferences, sizeof test_preferences, "/tmp/chibi-studio-prefs-XXXXXX");
    int descriptor = mkstemp(test_preferences);
    assert(descriptor >= 0);
    close(descriptor);
    assert(setenv("CHIBI_STUDIO_PREFS_PATH", test_preferences, 1) == 0);
#endif
    remove(test_preferences);
    assert(atexit(remove_test_preferences) == 0);
}

static void print_test_selectors()
{
    puts("Common focused native selectors (pass one to make test-fast):");
    puts("  authoring/graphs: --automation-authoring --graph-arrange --graph-editing --graph-inline --graph-optimize --graph-signature --graph-overhaul");
    puts("  assets/support: --asset-documents --rcss --tree-watch");
    puts("  runtime/compiler: --runtime-execution --behavior-parity --compiled-behaviors --simulation --system-arithmetic");
    puts("  Studio/UI: --editor-graph --editor-flow --scene-documents --studio-documents --studio-models --studio-lists --studio-outliner --studio-output --studio-properties --ui-documents --ui-designer --ui-controls --ui-popovers --ui-lists --rmlui-kit --code-intelligence --ui-motion");
    puts("  modeling/materials: --model-constructive --model-bake --model-export --model-inspection --modeling --materials --material-thermal --material-mpm");
    puts("  gameplay: --game-capabilities --arena-combat --arena-encounter --arena-hud");
    puts("  collaboration: --collab");
    puts("  Narrower costly checks: --automation-search --automation-refactor --automation-profile-scenario-inputs --editor-commands");
    puts("  Focused CPU checks: --shader-graph-codegen --prototype-preview-lifetime");
    puts("  GPU/render coverage: make test-render");
    puts("The no-selector invocation is the full all_checks suite; --whole-function-audit is a separate full-coverage check.");
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "--list-tests") || !strcmp(argv[1], "--help"))) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        print_test_selectors();
        return 0;
    }
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "multiple test_suite selectors or flags are not supported: %s\n", argv[i]);
            return 2;
        }
    }
    isolate_test_preferences();
    if (argc == 2 && !strcmp(argv[1], "--collab")) {
        test_collab_protocol();
        test_collab_replay();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--prototype-preview-lifetime")) {
        run_prototype_preview_lifetime_check();
        return 0;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    program_name = argc ? argv[0] : "test_suite";
    test_timing_enabled = getenv("CHIBI_TEST_TIMINGS") &&
                          !strcmp(getenv("CHIBI_TEST_TIMINGS"), "1");
    if (argc == 2 && !strcmp(argv[1], "--shader-graph-gpu")) {
        extern int shader_graph_gpu_run(const char *program);
        return shader_graph_gpu_run(program_name);
    }
    if (argc == 2 && (!strcmp(argv[1], "--sdf-quality") ||
                      !strcmp(argv[1], "--sdf-quality-gles"))) {
        extern int sdf_quality_run(const char *program, bool gles);
        return sdf_quality_run(program_name, !strcmp(argv[1], "--sdf-quality-gles"));
    }
    if (argc == 2 && !strcmp(argv[1], "--sdf-spatial")) {
        extern int sdf_spatial_run(const char *program);
        registry_validate();
        return sdf_spatial_run(program_name);
    }
    if (argc == 2 && (!strcmp(argv[1], "--sdf-visibility") ||
                      !strcmp(argv[1], "--sdf-visibility-gles"))) {
        extern int sdf_visibility_run(const char *program, bool gles);
        registry_validate();
        return sdf_visibility_run(program_name, !strcmp(argv[1], "--sdf-visibility-gles"));
    }
    if (argc == 2 && (!strcmp(argv[1], "--camera-orbit") ||
                      !strcmp(argv[1], "--camera-orbit-gles"))) {
        extern int camera_orbit_run(const char *program, bool gles);
        registry_validate();
        return camera_orbit_run(program_name, !strcmp(argv[1], "--camera-orbit-gles"));
    }
    if (argc == 2 && (!strcmp(argv[1], "--surface-depth") ||
                      !strcmp(argv[1], "--surface-depth-gles"))) {
        extern int surface_depth_run(const char *program, bool gles);
        registry_validate();
        return surface_depth_run(program_name, !strcmp(argv[1], "--surface-depth-gles"));
    }
    if (argc == 2 && (!strcmp(argv[1], "--surface-gather") ||
                      !strcmp(argv[1], "--surface-gather-gles"))) {
        extern int surface_gather_run(const char *program, bool gles);
        registry_validate();
        return surface_gather_run(program_name, !strcmp(argv[1], "--surface-gather-gles"));
    }
    if (argc >= 2 && !strcmp(argv[1], "--model-sampled")) {
        extern void test_model_sampled();
        registry_validate();
        test_model_sampled();
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--model-sampled-gpu")) {
        extern int test_model_sampled_gpu(const char *, const char *);
        registry_validate();
        return test_model_sampled_gpu(program_name, argc > 2 ? argv[2] : NULL);
    }
    if (argc == 2 && !strcmp(argv[1], "--shader-detail-gpu")) {
        registry_validate();
        return test_shader_detail_gpu(program_name);
    }
    if (argc == 2 && !strcmp(argv[1], "--sdf-material")) {
        extern int sdf_material_run(const char *program);
        registry_validate();
        return sdf_material_run(program_name);
    }
    /* Thumbnails persist nowhere but where a test puts them. */
    setenv("CHIBI_THUMBNAIL_CACHE", "", 1);
    /* Checks start from default preferences and never touch the user's:
       every build shares their file, and other sessions change it. */
    char prefs[] = "/tmp/chibi-test-prefs-XXXXXX";
    if (!getenv("CHIBI_PREFS_DIR")) {
        char *made = mkdtemp(prefs);
        assert(made);
        setenv("CHIBI_PREFS_DIR", made, 1);
    }
    if (argc > 1 && !strcmp(argv[1], "--compiled-behaviors")) {
        registry_validate();
        test_compiled_behaviors();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--editor-commands")) {
        registry_validate();
        run_editor_command_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--automation-search")) {
        registry_validate();
        test_automation_search();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--automation-refactor")) {
        registry_validate();
        test_automation_refactor();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--automation-profile-scenario-inputs")) {
        registry_validate();
        test_automation_profile_scenario_inputs();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--material-thermal")) {
        test_material_thermal();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--material-mpm")) {
        test_material_mpm();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--mesh-material")) {
        extern int mesh_material_run(const char *program);
        registry_validate();
        test_mesh_surface();
        test_mesh_asset();
        return mesh_material_run(program_name);
    }
    if (argc == 2 &&
        (!strcmp(argv[1], "--radiance") || !strcmp(argv[1], "--radiance-gles"))) {
        extern int radiance_run(const char *program, bool gles);
        return radiance_run(program_name, !strcmp(argv[1], "--radiance-gles"));
    }
    if (argc == 2 && !strcmp(argv[1], "--math-expression")) {
        registry_validate();
        test_math_expression();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--editor-checkpoint")) {
        registry_validate();
        test_editor_checkpoint_with_app(program_name);
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--game-ui-presenter")) {
        registry_validate();
        test_game_ui_presenter();
        return fails ? 1 : 0;
    }
    /* UI Inspector */
    if (argc == 2 && !strcmp(argv[1], "--ui-inspector")) {
        test_ui_inspector();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--editor-checkpoint")) {
        registry_validate();
        test_editor_checkpoint();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--graph-arrange")) {
        run_graph_arrange_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--graph-editing")) {
        registry_validate();
        run_graph_editing_checks();
        run_graph_collapse_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--graph-inline")) {
        registry_validate();
        run_graph_inline_checks();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--asset-documents")) {
        registry_validate();
        test_asset_value();
        run_kdl_text_checks();
        test_asset_source();
        test_asset_code_checkpoint();
        run_graph_document_checks();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--rcss")) {
        test_rcss();
        return fails ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--tree-watch")) {
        test_tree_watch();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--arrange-bench")) {
        return bench_graph_arrange(
            argc > 2 ? atoi(argv[2]) : 1,
            argc > 3 && *argv[3] ? argv[3] : NULL,
            argc > 4 ? argv[4] : NULL
        );
    }
    if (argc > 1 && !strcmp(argv[1], "--component-nodes")) {
        registry_validate();
        test_component_nodes();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--graph-signature")) {
        registry_validate();
        run_graph_signature_checks();
        return fails ? 1 : 0;
    }
    /* UI language service (workstream B) */
    if (argc > 1 && !strcmp(argv[1], "--ui-source")) {
        test_ui_sources();
        test_ui_source();
        test_ui_check();
        test_ui_pages();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--reference-index")) {
        registry_validate();
        test_reference_index();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--refactor")) {
        registry_validate();
        test_refactor();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--debugging")) {
        registry_validate();
        test_debugging();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--graph-optimize") == 0) {
        registry_validate();
        test_graph_optimizer();
        test_input_bindings();
        return fails ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-encounter-query-c")) {
        extern void encounter_query_code(const char *path, int count, int repeats);
        encounter_query_code(argv[2], 0, 0);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--encounter-query-code")) {
        extern void encounter_query_code(const char *path, int count, int repeats);
        encounter_query_code(argv[2], atoi(argv[3]), atoi(argv[4]));
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--encounter-query")) {
        extern void bench_encounter_query(int count, unsigned budget, int repeats);
        bench_encounter_query(atoi(argv[2]), (unsigned)atoi(argv[3]), atoi(argv[4]));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bench-spawn")) {
        registry_validate();
        extern void bench_spawn(int count);
        bench_spawn(atoi(argv[2]));
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--bench-behavior-destroy")) {
        bench_behavior_destroy(atoi(argv[2]));
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--pending")) {
        test_pending_backend(BEHAVIOR_SLJIT);
        test_pending_backend(BEHAVIOR_BYTECODE);
        test_pending_backend(BEHAVIOR_ORACLE);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--parallel-runtime")) {
        test_parallel_ticks();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--tick-batch")) {
        test_behavior_batches();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-sequence-c")) {
        emit_sequence_c(argv[2]);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "--bench-sequences")) {
        return bench_sequences(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-batch-c")) {
        emit_batch_c(argv[2]);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "--bench-batch")) {
        return bench_batch(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-command-c")) {
        registry_validate();
        emit_command_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-command")) {
        registry_validate();
        return bench_command(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 5 && strcmp(argv[1], "--bench-suspend") == 0) {
        return bench_suspend(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-delay-c")) {
        emit_delay_c(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--check-delay-c")) {
        test_delay_entries(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-delay")) {
        return bench_delay(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && strcmp(argv[1], "--whole-delay") == 0) {
        test_compiled_delays();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-recursion")) {
        registry_validate();
        test_compiled_recursion(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--recursive-function-audit")) {
        registry_validate();
        return whole_recursion_audit();
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-recursion-c")) {
        emit_recursion_c(argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-async")) {
        test_compiled_async(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-random")) {
        test_compiled_random(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-random-c")) {
        emit_random_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-random")) {
        return bench_random(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-math")) {
        registry_validate();
        test_compiled_math();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-motion")) {
        test_compiled_motion(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-data")) {
        test_compiled_data(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-search")) {
        test_compiled_search(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-search-c")) {
        emit_search_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-search")) {
        return bench_search(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 2 && !strcmp(argv[1], "--owned-ticks")) {
        test_owned_ticks();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--managed-reuse")) {
        test_managed_reuse();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-container-c")) {
        extern void emit_container_c(const char *path);
        emit_container_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-containers")) {
        extern int bench_containers(const char *path, int repeats, int rotation);
        return bench_containers(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-array-ops")) {
        test_array_ops(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-array-ops-c")) {
        emit_array_ops_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-array-ops")) {
        return bench_array_ops(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-owned-arrays")) {
        test_owned_arrays(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-owned-array-c")) {
        emit_owned_array_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-owned-arrays")) {
        return bench_owned_arrays(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-reflected")) {
        test_compiled_reflected(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-reflected-c")) {
        emit_reflected_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-reflected")) {
        return bench_reflected(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-rhythm")) {
        test_compiled_rhythm(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-rhythm-c")) {
        emit_rhythm_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-rhythm")) {
        return bench_rhythm(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-reaction")) {
        test_compiled_reaction(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-reaction-c")) {
        emit_reaction_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-reaction")) {
        return bench_reaction(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-names")) {
        test_compiled_names(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-name-c")) {
        emit_name_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-names")) {
        return bench_names(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-managed")) {
        test_compiled_managed(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-managed")) {
        return bench_managed(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-managed-c")) {
        emit_managed_c(argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-particles")) {
        test_compiled_particles(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-particle-c")) {
        emit_particle_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-particles")) {
        return bench_particles(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-material")) {
        test_compiled_material(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-material-c")) {
        emit_material_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-material")) {
        return bench_material(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-data-c")) {
        emit_data_c(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-nearest-c")) {
        emit_nearest_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-nearest")) {
        return bench_nearest(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 2 && !strcmp(argv[1], "--whole-nearest")) {
        test_targeting();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-motion-c")) {
        emit_motion_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-motion")) {
        return bench_motion(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-async-c")) {
        emit_async_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-async")) {
        return bench_async(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && strcmp(argv[1], "--whole-suspend") == 0) {
        test_compiled_suspensions();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-suspend-c") == 0) {
        emit_suspend_c(argv[2]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--check-suspend-c") == 0) {
        test_generated_suspensions(argv[2]);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--whole-command") == 0) {
        registry_validate();
        test_compiled_commands();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--predicate-branches")) {
        extern void test_predicate_branches();
        registry_validate();
        test_predicate_branches();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-predicate-c")) {
        extern void emit_predicate_c(const char *path);
        registry_validate();
        emit_predicate_c(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--check-predicate-c")) {
        extern void test_predicate_c(const char *path);
        registry_validate();
        test_predicate_c(argv[2]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-query-c") == 0) {
        registry_validate();
        emit_query_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-query") == 0) {
        registry_validate();
        return bench_query(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && strcmp(argv[1], "--whole-query") == 0) {
        registry_validate();
        test_compiled_queries();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-array-c") == 0) {
        registry_validate();
        emit_array_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-array") == 0) {
        registry_validate();
        return bench_array(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && strcmp(argv[1], "--whole-array") == 0) {
        registry_validate();
        test_compiled_arrays();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-store-c") == 0) {
        registry_validate();
        emit_store_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-store") == 0) {
        registry_validate();
        return bench_store(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && strcmp(argv[1], "--whole-store") == 0) {
        registry_validate();
        test_compiled_stores();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--emit-alias-c") == 0) {
        registry_validate();
        emit_alias_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-alias") == 0) {
        registry_validate();
        return bench_alias(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && strcmp(argv[1], "--emit-loop-c") == 0) {
        registry_validate();
        emit_loop_c(argv[2]);
        return 0;
    }
    if ((argc == 5 || argc == 6) && strcmp(argv[1], "--bench-loop") == 0) {
        registry_validate();
        return bench_loop(argv[2], atoi(argv[3]), atoi(argv[4]), argc == 6 ? atoi(argv[5]) : 64);
    }
    if (argc == 3 && strcmp(argv[1], "--emit-component-c") == 0) {
        registry_validate();
        emit_component_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-component") == 0) {
        registry_validate();
        return bench_component(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-switch")) {
        registry_validate();
        test_compiled_switches(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--instance-pages")) {
        registry_validate();
        instance_bank_smoke(BEHAVIOR_ORACLE);
        instance_bank_smoke(BEHAVIOR_BYTECODE);
        instance_bank_smoke(BEHAVIOR_SLJIT);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-retrigger")) {
        registry_validate();
        test_compiled_retrigger(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-retrigger-c")) {
        registry_validate();
        emit_retrigger_c(argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-multigate")) {
        registry_validate();
        test_compiled_multigate(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-multigate-c")) {
        registry_validate();
        emit_multigate_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-multigate")) {
        registry_validate();
        return bench_multigate(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc > 1 && !strcmp(argv[1], "--whole-state")) {
        registry_validate();
        test_compiled_states(argc == 3 ? argv[2] : NULL);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-state-c")) {
        registry_validate();
        emit_state_c(argv[2]);
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-state")) {
        registry_validate();
        return bench_state(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-switch")) {
        registry_validate();
        return bench_switch(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 3 && !strcmp(argv[1], "--emit-switch-c")) {
        registry_validate();
        emit_switch_c(argv[2]);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--whole-loop") == 0) {
        registry_validate();
        test_compiled_loops();
        test_compiled_conversions();
        test_compiled_selects();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--whole-alias") == 0) {
        registry_validate();
        test_compiled_aliases();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--whole-component") == 0) {
        registry_validate();
        test_compiled_components();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--whole-function-audit") == 0) {
        registry_validate();
        return whole_function_audit();
    }
    if (argc > 1 && strcmp(argv[1], "--whole-spawn") == 0) {
        registry_validate();
        test_compiled_spawns();
        return fails ? 1 : 0;
    }
    if (argc == 6 && (!strcmp(argv[1], "--bench-arena-scale") ||
                      !strcmp(argv[1], "--bench-arena-reference") ||
                      !strcmp(argv[1], "--bench-arena-clock"))) {
        registry_validate();
        return bench_arena_scale(
            atoi(argv[2]),
            atoi(argv[3]),
            atoi(argv[4]),
            atoi(argv[5]),
            !strcmp(argv[1], "--bench-arena-reference") ? 1
            : !strcmp(argv[1], "--bench-arena-clock")   ? 2
                                                        : 0
        );
    }
    if (argc == 5 && !strcmp(argv[1], "--bench-arena-scene-scale")) {
        registry_validate();
        return arena_scene_scale_run(program_name, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    }
    if (argc == 6 && !strcmp(argv[1], "--bench-neon-pursuit")) {
        registry_validate();
        return bench_neon_pursuit(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    }
    if (argc > 1 && strcmp(argv[1], "--compiled-arena") == 0) {
        registry_validate();
        return compiled_arena_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--backend-unavailable") == 0) {
        registry_validate();
        test_behavior_unavailable();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--behavior-parity") == 0) {
        registry_validate();
        test_compiled_behaviors();
        test_encounter_backend_parity();
        behavior_debug_backend(BEHAVIOR_BYTECODE);
        test_function_wait();
        test_encounter();
        test_host_wait();
        behavior_debug_backend(BEHAVIOR_ORACLE);
        test_function_wait();
        test_encounter();
        test_host_wait();
        behavior_debug_backend(BEHAVIOR_UNAVAILABLE);
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--arena-play") == 0) {
        registry_validate();
        return arena_play_run(program_name);
    }
    if (argc > 1 && (strcmp(argv[1], "--arena-play-review") == 0 ||
                     strcmp(argv[1], "--arena-editor-review") == 0 ||
                     strcmp(argv[1], "--arena-crowd-review") == 0)) {
        registry_validate();
        return arena_play_review_run(
            program_name,
            strcmp(argv[1], "--arena-play-review") != 0,
            strcmp(argv[1], "--arena-crowd-review") == 0
        );
    }
    if (argc > 1 && strcmp(argv[1], "--editor-graph") == 0) {
        registry_validate();
        test_graph_navigation();
        run_editor_command_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--editor-flow") == 0) {
        registry_validate();
        test_editor_flow();
        test_debug_target();
        run_runtime_instance_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--scene-documents") == 0) {
        registry_validate();
        test_editor_documents();
        return scene_documents_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--studio-documents") == 0) {
        registry_validate();
        int documents = studio_documents_run(program_name);
        int graph_inputs = studio_graph_inputs_run(program_name);
        int thumbnails = studio_thumbnails_run(program_name);
        return documents || graph_inputs || thumbnails || fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--studio-thumbnails") == 0) {
        registry_validate();
        return studio_thumbnails_run(program_name) || fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--studio-models") == 0) {
        registry_validate();
        return studio_models_run(program_name) || fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--studio-graph-inputs") == 0) {
        registry_validate();
        return studio_graph_inputs_run(program_name) || fails ? 1 : 0;
    }
    /* Lists and drag (workstream F4): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-lists") == 0) {
        registry_validate();
        return studio_lists_run(program_name) || fails ? 1 : 0;
    }
    /* The Outliner (workstream P2): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-outliner") == 0) {
        registry_validate();
        return studio_outliner_run(program_name) || fails ? 1 : 0;
    }
    /* Output (workstream P4): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-output") == 0) {
        registry_validate();
        return studio_output_run(program_name) || fails ? 1 : 0;
    }
    /* Members (workstream P5): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-members") == 0) {
        registry_validate();
        return studio_members_run(program_name) || fails ? 1 : 0;
    }
    /* The UI Inspector (workstream I1): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-inspector") == 0) {
        registry_validate();
        return studio_inspector_run(program_name) || fails ? 1 : 0;
    }
    /* The visual designer (workstream D1): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-designer") == 0) {
        registry_validate();
        return studio_designer_run(program_name) || fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--studio-shell") == 0) {
        registry_validate();
        return studio_shell_run(program_name) || fails ? 1 : 0;
    }
    /* The Scene area (workstream C1): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-scene") == 0) {
        registry_validate();
        return studio_scene_run(program_name) || fails ? 1 : 0;
    }
    /* The editor chrome (workstream C2): Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-chrome") == 0) {
        registry_validate();
        return studio_chrome_run(program_name) || fails ? 1 : 0;
    }
    /* The palette, pickers and refactor popover (workstream C4): Studio's,
       in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-palette") == 0) {
        registry_validate();
        return studio_palette_run(program_name) || fails ? 1 : 0;
    }
    /* Graph chrome (workstream G1): the graph's menus and Properties' Node
       and Member tabs, Studio's, in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-graph-chrome") == 0) {
        registry_validate();
        return studio_graph_chrome_run(program_name) || fails ? 1 : 0;
    }
    /* Authored panels, game UI and the areas they take turns in: Studio's,
       in a hidden window. */
    if (argc > 1 && strcmp(argv[1], "--studio-panels") == 0) {
        registry_validate();
        return studio_panels_run(program_name) || fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--studio-properties") == 0) {
        registry_validate();
        return studio_properties_run(program_name) || fails ? 1 : 0;
    }
    /* The S0 UI bench; `headless`, `studio` or `members` runs only those
       cases. */
    if (argc > 1 && strcmp(argv[1], "--ui-bench") == 0) {
        registry_validate();
        const char *only = argc > 2 ? argv[2] : "";
        bool members = strcmp(only, "members") == 0,
             headless = strcmp(only, "studio") != 0 && !members,
             studio = strcmp(only, "headless") != 0;
        int failed = headless ? ui_bench_headless() : 0;
        return failed || (studio && ui_bench_studio(program_name, members)) ? 1 : 0;
    }
    /* UI documents (workstream D). */
    if (argc > 1 && strcmp(argv[1], "--ui-documents") == 0) {
        test_ui_documents();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-kit") == 0) {
        return test_rmlui_kit() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-binding") == 0) {
        return test_rmlui_binding() ? 0 : 1;
    }
    /* Visual designer (workstream H) alone. */
    if (argc > 1 && strcmp(argv[1], "--ui-designer") == 0) {
        test_ui_designer();
        return fails ? 1 : 0;
    }
    /* Workstream C: the code editor alone. */
    if (argc > 1 && strcmp(argv[1], "--kit-code") == 0) {
        return test_kit_code() ? 0 : 1;
    }
    /* Workstream I: code intelligence in the Text Editor. */
    if (argc > 1 && strcmp(argv[1], "--code-intelligence") == 0) {
        test_code_intelligence();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-render") == 0) {
        return rmlui_render_run(program_name, false);
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-render-gles") == 0) {
        return rmlui_render_run(program_name, true);
    }
    if (argc > 1 && strcmp(argv[1], "--automation-authoring") == 0) {
        registry_validate();
        test_automation_graph_edit();
        test_automation_search();
        test_automation_navigation();
        test_automation_graph_optimize();
        test_automation_prototype_edit();
        test_automation_scene_edit();
        test_automation_asset_write();
        test_automation_refactor();
        test_automation_preview_registration();
        test_automation_profile_scenario_inputs();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--game-capabilities") == 0) {
        registry_validate();
        test_game_capabilities();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--runtime-execution") == 0) {
        registry_validate();
        run_runtime_execution_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--system-arithmetic") == 0) {
        registry_validate();
        test_system_arithmetic();
        test_system_nodes();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--arena-combat") == 0) {
        registry_validate();
        test_physics();
        test_dash();
        test_encounter();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--arena-encounter") == 0) {
        registry_validate();
        test_encounter();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--material-engine")) {
        test_fields();
        test_system_schedule();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--bench-fields")) {
        return bench_fields();
    }
    if (argc > 1 && !strcmp(argv[1], "--bench-field-slices")) {
        return bench_field_slices();
    }
    if (argc > 1 && !strcmp(argv[1], "--bench-system-schedule")) {
        return bench_system_schedule();
    }
    if (argc > 1 && strcmp(argv[1], "--materials") == 0) {
        test_materials();
        test_material_reactions();
        test_material_thermal();
        test_material_detail();
        test_material_mpm();
        test_material_obstacles();
        test_material_particle_index();
        test_material_gravity();
        test_material_simulation();
        test_material_constitutive();
        test_material_mixed();
        test_material_blast();
        test_fields();
        test_system_schedule();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--simulation") == 0) {
        registry_validate();
        run_engine_checks();
        test_physics();
        test_sdf_motion();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--graph-overhaul") == 0) {
        registry_validate();
        test_shader_graph();
        test_typed_events();
        test_extract();
        test_projectile();
        test_combat();
        test_auto_weapon();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--shader-graph-codegen")) {
        registry_validate();
        test_shader_graph();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--shader-detail") == 0) {
        registry_validate();
        test_shader_detail();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--model-constructive") == 0) {
        registry_validate();
        test_model_constructive();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--model-bake") == 0) {
        registry_validate();
        test_model_bake();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--model-export") == 0) {
        registry_validate();
        test_model_export();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--model-inspection") == 0) {
        registry_validate();
        test_model_inspection();
        test_model_material_scope();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--modeling") == 0) {
        registry_validate();
        test_modeling();
        test_shader_detail();
        test_model_surface();
        test_model_reuse();
        test_model_parameters();
        test_model_profiles();
        test_model_materials();
        test_model_validation();
        test_model_inspection();
        test_model_material_scope();
        test_model_export();
        test_model_bake();
        test_model_constructive();
        return fails ? 1 : 0;
    }
    /* ---------- Settings (workstream F) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--studio-settings")) {
        test_studio_settings();
        return fails ? 1 : 0;
    }
    /* ---------- Authored status bar (workstream J) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--kit-slot")) {
        test_kit_slot();
        return fails ? 1 : 0;
    }
    /* ---------- One document per area (workstream F5) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--kit-documents")) {
        test_kit_documents();
        return fails ? 1 : 0;
    }
    /* ---------- Wave 2 follow-ups (workstream P) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--ui-followups")) {
        test_game_ui_presenter();
        test_kit_host();
        test_studio_settings();
        test_studio_zoom();
        test_automation_search();
        return fails ? 1 : 0;
    }
    /* ---------- Native elements for authored UI (workstream N) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--ui-motion")) {
        test_check(test_rmlui_kit(), "Studio kit reconciliation, number fields, menus, and command");
        test_ui_motion();
        test_kit_slot();
        return fails ? 1 : 0;
    }
    /* ---------- Native controls (workstream F2) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--ui-controls")) {
        test_ui_controls();
        return fails ? 1 : 0;
    }
    /* ---------- Popovers (workstream F3) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--ui-popovers")) {
        test_ui_popovers();
        return fails ? 1 : 0;
    }
    /* ---------- Lists and drag (workstream F4) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--ui-lists")) {
        test_ui_lists();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--game-ui-events")) {
        test_game_ui_events();
        test_game_ui_presenter();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--studio-nodes-gate")) {
        test_studio_nodes_gate();
        return fails ? 1 : 0;
    }
    /* ---------- arena2d end to end (wave 3, workstream X) ---------- */
    if (argc > 1 && !strcmp(argv[1], "--arena-hud")) {
        test_arena_hud();
        return fails ? 1 : 0;
    }
    if (argc > 1) {
        fprintf(stderr, "unknown test_suite selector: %s\n", argv[1]);
        fprintf(stderr, "use %s --list-tests to see common focused selectors\n", program_name);
        return 2;
    }
    pu_run_test("all_checks", all_checks);
    return pu_test_failed() ? 1 : 0;
}

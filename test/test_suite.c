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
#define PICO_UNIT_IMPLEMENTATION
#include "suite_internal.h"

void test_automation_graph_edit();
void test_automation_graph_optimize();
void test_automation_prototype_edit();
void test_automation_scene_edit();
void test_automation_asset_write();

int shader_ui_run(const char *program);
int arena_ui_run(const char *program, bool profile_only);
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
void test_physics();
void test_materials();
void test_fields();
void test_system_schedule();
int bench_fields();
int bench_system_schedule();
void test_game_capabilities();
void test_player_widgets();
void test_dash();
void test_encounter();
void test_system_arithmetic();
void test_asset_index();
void test_component_presentation();
void test_input_bindings();
void test_graph_optimizer();
int scene_documents_run(const char *program);
bool test_rmlui_menu();
bool test_rmlui_kit();
int rmlui_render_run(const char *program);

static int fails;

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
    registry_validate();
    test_automation_graph_edit();
    test_automation_graph_optimize();
    test_automation_prototype_edit();
    test_automation_scene_edit();
    test_automation_asset_write();
    run_content_catalog_checks();
    test_asset_index();
    run_graph_model_checks();
    run_graph_document_checks();
    run_graph_arrange_checks();
    run_runtime_execution_checks();
    test_compiled_behaviors();
    test_encounter_backend_parity();
    run_runtime_instance_checks();
    run_graph_compile_checks();
    run_graph_conversion_checks();
    run_graph_functions_checks();
    run_runtime_arrays_checks();
    run_runtime_catalog_checks();
    run_support_format_checks();
    run_ecs_checks();
    run_ecs_growth_checks();
    run_component_read_checks();
    run_prototype_checks();
    run_scene_document_checks();
    run_behavior_checks();
    run_behavior_loader_checks();
    run_entity_presence_checks();
    run_live_checks();
    run_live_preview_checks();
    run_prototype_live_checks();
    test_host_wait();
    test_entities();
    test_projectile();
    test_combat();
    test_physics();
    test_materials();
    test_fields();
    test_system_schedule();
    test_game_capabilities();
    test_player_widgets();
    test_dash();
    test_encounter();
    test_targeting();
    test_pursuit();
    test_vector();
    test_arena();
    test_number_serialization();
    test_auto_weapon();
    test_graph_navigation();
    test_extract();
    test_input_bindings();
    test_graph_optimizer();
    test_shader_graph();
    test_surface_cpu();
    test_shader_effects_cpu();
    test_shader_image_cpu();
    test_typed_events();
    test_component_conversion();
    test_field_outputs();
    test_function_wait();
    test_function_fields();
    test_flags();
    test_particles();
    test_labels();
    test_math_expression();
    test_behavior_math();
    test_appearance();
    test_retained_components();
    test_editor_flow();
    test_component_fields();
    test_component_nodes();
    test_scene_navigation();
    test_scene_debug();
    test_appearance_geometry();
    test_orientation();
    test_movement_input();
    test_movement_trail();
    test_sdf_motion();
    test_material();
    test_wave();
    test_scene_history();
    test_enemy_attack();
    test_system_host_wait();
    test_system_arithmetic();
    run_engine_checks();
    run_parallel_checks();
    run_ecs_migration_checks();
    run_scene_checks();
    run_editor_command_checks();
    test_component_presentation();
    test_editor_checkpoint();
    test_check(test_ui_keyboard(), "shared icon keyboard activation and disabled routing");
    test_check(test_rmlui_menu(), "RmlUi menu commands, focus, layout, and lifecycle");
    test_check(test_rmlui_kit(), "Studio kit reconciliation, number fields, menus, and command");
    test_check(test_ui_scroll_smoothing(), "smooth scrolling is refresh-rate independent");
    test_check(test_gameplay_keyboard(), "gameplay keyboard navigation and text editing capture");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    return fails == 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    program_name = argc ? argv[0] : "test_suite";
    if (argc == 2 && !strcmp(argv[1], "--radiance")) {
        extern int radiance_run(const char *program);
        return radiance_run(program_name);
    }
    if (argc == 2 && !strcmp(argv[1], "--math-expression")) {
        registry_validate();
        test_math_expression();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--graph-arrange")) {
        run_graph_arrange_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--arrange-bench")) {
        return bench_graph_arrange(argc > 2 ? atoi(argv[2]) : 1, argc > 3 ? argv[3] : NULL);
    }
    if (argc > 1 && !strcmp(argv[1], "--component-nodes")) {
        registry_validate();
        test_component_nodes();
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
    if (argc > 1 && (strcmp(argv[1], "--arena-ui") == 0 ||
                     strcmp(argv[1], "--behavior-profile-ui") == 0)) {
        registry_validate();
        return arena_ui_run(program_name, strcmp(argv[1], "--behavior-profile-ui") == 0);
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
    if (argc > 1 && strcmp(argv[1], "--scene-documents") == 0) {
        registry_validate();
        test_scene_navigation();
        return scene_documents_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--shader-ui") == 0) {
        registry_validate();
        return shader_ui_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--ui-keyboard") == 0) {
        return test_ui_keyboard() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-menu") == 0) {
        return test_rmlui_menu() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-kit") == 0) {
        return test_rmlui_kit() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--rmlui-menu-render") == 0) {
        return rmlui_render_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--ui-scroll") == 0) {
        return test_ui_scroll_smoothing() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--automation-authoring") == 0) {
        registry_validate();
        test_automation_graph_edit();
        test_automation_graph_optimize();
        test_automation_prototype_edit();
        test_automation_scene_edit();
        test_automation_asset_write();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--game-capabilities") == 0) {
        registry_validate();
        test_game_capabilities();
        test_player_widgets();
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
    if (argc > 1 && !strcmp(argv[1], "--bench-system-schedule")) {
        return bench_system_schedule();
    }
    if (argc > 1 && strcmp(argv[1], "--materials") == 0) {
        test_materials();
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
    pu_run_test("all_checks", all_checks);
    return pu_test_failed() ? 1 : 0;
}

#define PICO_UNIT_IMPLEMENTATION
#include "suite_internal.h"

int shader_ui_run(const char *program);
int arena_ui_run(const char *program, bool profile_only);
int compiled_arena_run(const char *program);
int whole_function_audit();
int arena_play_review_run(const char *program, bool editor_mode, bool crowd);
void run_behavior_loader_checks();
void run_entity_presence_checks();
void test_surface_cpu();
void test_shader_effects_cpu();
void test_shader_image_cpu();
void test_physics();
void test_dash();
void test_encounter();
void test_system_arithmetic();
void test_asset_index();
void test_input_bindings();
int scene_documents_run(const char *program);

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
    run_content_catalog_checks();
    test_asset_index();
    run_graph_model_checks();
    run_graph_document_checks();
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
    test_editor_checkpoint();
    test_check(test_ui_keyboard(), "shared icon keyboard activation and disabled routing");
    test_check(test_ui_scroll_smoothing(), "smooth scrolling is refresh-rate independent");
    test_check(test_gameplay_keyboard(), "gameplay keyboard navigation and text editing capture");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    return fails == 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    program_name = argc ? argv[0] : "test_suite";
    if (argc == 3 && strcmp(argv[1], "--emit-component-c") == 0) {
        registry_validate();
        emit_component_c(argv[2]);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--bench-component") == 0) {
        registry_validate();
        return bench_component(argv[2], atoi(argv[3]), atoi(argv[4]));
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
    if (argc > 1 && strcmp(argv[1], "--ui-native") == 0) {
        registry_validate();
        return ui_native_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--render-checks") == 0) {
        registry_validate();
        return render_checks_run(program_name);
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
    if (argc > 1 && strcmp(argv[1], "--ui-scroll") == 0) {
        return test_ui_scroll_smoothing() ? 0 : 1;
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

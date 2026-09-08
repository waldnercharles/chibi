#define PICO_UNIT_IMPLEMENTATION
#include "suite_internal.h"

int shader_ui_run(const char *program);

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
    run_graph_model_checks();
    run_graph_document_checks();
    run_runtime_execution_checks();
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
    run_live_checks();
    run_live_preview_checks();
    run_prototype_live_checks();
    test_host_wait();
    test_entities();
    test_projectile();
    test_combat();
    test_targeting();
    test_pursuit();
    test_vector();
    test_arena();
    test_number_serialization();
    test_auto_weapon();
    test_graph_navigation();
    test_extract();
    test_shader_graph();
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
    run_engine_checks();
    run_parallel_checks();
    run_ecs_migration_checks();
    run_scene_checks();
    run_editor_command_checks();
    test_editor_checkpoint();
    test_check(test_ui_keyboard(), "shared icon keyboard activation and disabled routing");
    test_check(test_gameplay_keyboard(), "gameplay keyboard navigation and text editing capture");

    if (!fails) {
        fails += ui_integration_run(program_name) != 0;
    }

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    return fails == 0;
}

int main(int argc, char **argv)
{
    program_name = argc ? argv[0] : "test_suite";
    if (argc > 1 && strcmp(argv[1], "--ui-integration") == 0) {
        registry_validate();
        return ui_integration_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--editor-graph") == 0) {
        registry_validate();
        test_graph_navigation();
        run_editor_command_checks();
        return fails ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "--shader-ui") == 0) {
        registry_validate();
        return shader_ui_run(program_name);
    }
    if (argc > 1 && strcmp(argv[1], "--ui-keyboard") == 0) {
        return test_ui_keyboard() ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--runtime-execution") == 0) {
        registry_validate();
        run_runtime_execution_checks();
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

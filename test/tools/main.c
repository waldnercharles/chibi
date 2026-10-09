#define PICO_UNIT_IMPLEMENTATION
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

void test_tool_execution_policy();
void test_tool_package();
void test_tool_collab();
void test_tool_test_policy();
void test_tool_validation_plan();
void test_tool_publish();
void test_tool_types();
void test_tool_ecs();
void test_tool_ecs_relationships();
void test_tool_ecs_hierarchy();
void test_tool_ecs_world();
void test_tool_ecs_query();
void test_tool_ecs_query_benchmark();
void test_tool_prepared(const char *producer, const char *consumer);
void test_tool_prepared_runtime(const char *directory);
void test_tool_scene();
void test_scene_prepared();
#ifdef CHIBI_TEST_RENDER_RUNTIME
void test_render_resources();
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
void test_compiled_math();
void test_compiled_integers();
void test_compiled_conversions();
void test_compiled_selects();
void test_compiled_aliases();
void test_persistent_stores();
void test_parallel_ticks();
void test_owned_ticks();
static bool source_runtime_active;
bool test_source_runtime_active()
{
    return source_runtime_active;
}
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
void test_source_native_features();
void test_source_presentation();
void test_system_schedule();
void test_code_system_runtime();
void test_source_edit();
void test_shader_parameters_prepared();
int run_language_semantic_corpora(const char *);
int run_language_prepare_corpora(const char *);
int run_language_prepare_corpora_sljit(const char *);
void test_source_native_numeric_queries();
void test_source_native_material_features();
#endif
#ifdef CHIBI_TEST_FEATURE_COVERAGE
int run_feature_coverage(const char *project);
#endif
#ifdef CHIBI_TEST_BOX3D
void test_box3d();
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_native();
void test_tool_native_values();
void test_tool_native_catalog();
void test_tool_physics();
#endif

static bool shader_parameters;
bool test_shader_parameters_active()
{
    return shader_parameters;
}

int main(int argc, char **argv)
{
    bool prepared = argc == 3 && !strcmp(argv[1], "--prepared");
    bool read = argc == 3 && !strcmp(argv[1], "--prepared-read");
    bool package = argc == 2 && !strcmp(argv[1], "--package");
    bool unit = argc == 2 && !strcmp(argv[1], "--unit");
    bool query_benchmark = argc == 2 && !strcmp(argv[1], "--ecs-query-benchmark");
    bool scene_prepared = argc == 2 && !strcmp(argv[1], "--scene-prepared");
    bool scene = argc == 2 && !strcmp(argv[1], "--scene");
    bool native = false, native_values = false, native_catalog = false,
         physics = false, render_resources = false, box3d = false;
    bool whole_math = false, whole_integer = false, whole_conversion = false,
         whole_select = false, owned_tick = false, parallel_tick = false,
         whole_alias = false, persistent_store = false;
    bool source_features = false, source_numeric = false,
         source_material = false, source_presentation = false,
         system_schedule = false, source_systems = false, source_edit = false;
    int corpus = 0;
    bool coverage = false;
#ifdef CHIBI_TEST_FEATURE_COVERAGE
    coverage = argc == 3 && !strcmp(argv[1], "--feature-coverage");
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    whole_math = argc == 2 && !strcmp(argv[1], "--whole-math");
    whole_integer = argc == 2 && !strcmp(argv[1], "--whole-integer");
    whole_conversion = argc == 2 && !strcmp(argv[1], "--whole-conversion");
    persistent_store = argc == 2 && !strcmp(argv[1], "--persistent-store");
    whole_alias = argc == 2 && !strcmp(argv[1], "--whole-alias");
    whole_select = argc == 2 && !strcmp(argv[1], "--whole-select");
    owned_tick = argc == 2 && !strcmp(argv[1], "--owned-tick");
    parallel_tick = argc == 2 && !strcmp(argv[1], "--parallel-tick");
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
    shader_parameters = argc == 2 && !strcmp(argv[1], "--shader-parameters-prepared");
    source_edit = argc == 2 && !strcmp(argv[1], "--source-edit");
    if (argc == 3) {
        corpus = !strcmp(argv[1], "--semantic-corpora")      ? 1
               : !strcmp(argv[1], "--prepare-corpora")       ? 2
               : !strcmp(argv[1], "--prepare-corpora-sljit") ? 3
                                                             : 0;
    }
    system_schedule = argc == 2 && !strcmp(argv[1], "--system-schedule");
    source_systems = argc == 2 && !strcmp(argv[1], "--source-systems");
    source_presentation = argc == 2 && !strcmp(argv[1], "--source-presentation");
    source_features = argc == 2 && !strcmp(argv[1], "--source-native-features");
    source_numeric = argc == 2 && !strcmp(argv[1], "--source-native-numeric-queries");
    source_material = argc == 2 && !strcmp(argv[1], "--source-native-material");
#endif
#ifdef CHIBI_TEST_RENDER_RUNTIME
    render_resources = argc == 2 && !strcmp(argv[1], "--render-resources");
#endif
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_catalog = argc == 2 && !strcmp(argv[1], "--native-catalog");
    native_values = argc == 2 && !strcmp(argv[1], "--native-values");
    native = argc == 2 && !strcmp(argv[1], "--native");
    physics = argc == 2 && !strcmp(argv[1], "--physics");
#endif
    if (argc != 1 && !corpus && !coverage && !shader_parameters &&
        !source_edit && !package && !unit && !query_benchmark && !prepared &&
        !read && !scene && !scene_prepared && !native && !native_values &&
        !native_catalog && !physics && !render_resources && !box3d &&
        !persistent_store && !whole_alias && !whole_math && !whole_integer &&
        !whole_conversion && !whole_select && !owned_tick && !parallel_tick &&
        !source_features && !source_numeric && !source_material &&
        !source_presentation && !system_schedule && !source_systems) {
        fprintf(
            stderr,
            "Usage: %s [--package | --unit | --ecs-query-benchmark | "
            "--prepared "
            "test_language | --prepared-read directory | --scene | "
            "--scene-prepared | --native | --native-values | --native-catalog "
            "| --physics "
            "| --render-resources | --box3d | --persistent-store | "
            "--whole-alias | "
            "--whole-math | --whole-integer | --whole-conversion | "
            "--whole-select | --owned-tick | "
            "--parallel-tick | --source-native-features | "
            "--source-native-numeric-queries | --source-native-material | "
            "--source-presentation | --system-schedule | --source-systems | "
            "--source-edit | --shader-parameters-prepared | --semantic-corpora "
            "manifest | --prepare-corpora manifest | --prepare-corpora-sljit "
            "manifest | --feature-coverage project]\n",
            argv[0]
        );
        return 2;
    }
    tool_init();
    pu_display_time(true);
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    source_runtime_active = corpus || coverage || shader_parameters ||
                            source_edit || system_schedule || source_systems ||
                            persistent_store || source_presentation || whole_alias ||
                            whole_math || whole_integer || whole_conversion ||
                            whole_select || owned_tick || parallel_tick ||
                            source_features || source_numeric || source_material;
    if (source_runtime_active) {
#ifdef CHIBI_TEST_FEATURE_COVERAGE
        if (coverage) {
            return run_feature_coverage(argv[2]);
        }
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
        if (corpus) {
            return corpus == 1 ? run_language_semantic_corpora(argv[2])
                 : corpus == 2 ? run_language_prepare_corpora(argv[2])
                               : run_language_prepare_corpora_sljit(argv[2]);
        }
        if (shader_parameters) {
            test_shader_parameters_prepared();
        } else if (source_edit) {
            test_source_edit();
        } else if (system_schedule) {
            test_system_schedule();
        } else if (source_systems) {
            test_code_system_runtime();
        } else if (source_presentation) {
            test_source_presentation();
        } else if (source_features) {
            test_source_native_features();
        } else if (source_numeric) {
            test_source_native_numeric_queries();
        } else if (source_material) {
            test_source_native_material_features();
        } else
#endif
            if (persistent_store) {
            test_persistent_stores();
        } else if (whole_alias) {
            test_compiled_aliases();
        } else if (whole_math) {
            test_compiled_math();
        } else if (whole_integer) {
            test_compiled_integers();
        } else if (whole_conversion) {
            test_compiled_conversions();
        } else if (whole_select) {
            test_compiled_selects();
        } else if (owned_tick) {
            test_owned_ticks();
        } else {
            test_parallel_ticks();
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_RENDER_RUNTIME
    if (render_resources) {
        test_render_resources();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_BOX3D
    if (box3d) {
        test_box3d();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    if (native || native_values || native_catalog || physics) {
        if (physics) {
            test_tool_physics();
        } else if (native_catalog) {
            test_tool_native_catalog();
        } else if (native_values) {
            test_tool_native_values();
        } else {
            test_tool_native();
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
    if (package) {
        test_tool_package();
    } else if (scene_prepared) {
        test_scene_prepared();
    } else if (scene) {
        test_tool_scene();
    } else if (prepared) {
        test_tool_prepared(argv[2], argv[0]);
    } else if (read) {
        test_tool_prepared_runtime(argv[2]);
    } else if (unit) {
        test_tool_execution_policy();
    } else if (query_benchmark) {
        test_tool_ecs_query_benchmark();
    } else {
        test_tool_execution_policy();
        test_tool_types();
        test_tool_ecs();
        test_tool_ecs_relationships();
        test_tool_ecs_hierarchy();
        test_tool_ecs_world();
        test_tool_ecs_query();
        test_tool_package();
        test_tool_collab();
        test_tool_test_policy();
        test_tool_validation_plan();
        test_tool_publish();
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

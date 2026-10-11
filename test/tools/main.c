#define PICO_UNIT_IMPLEMENTATION
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

#ifdef CHIBI_TEST_WEBGPU_STAGING
int test_webgpu_staging_start();
int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--webgpu-staging")) {
        return 2;
    }
    return test_webgpu_staging_start();
}
#else
#ifdef CHIBI_TEST_TILEMAP
void test_tool_tilemap();
#endif
#ifdef CHIBI_TEST_TILEMAP_PROJECT
void test_tool_tilemap_project();
#endif
void test_tool_execution_policy();
void test_tool_package();
void test_tool_collab();
void test_tool_test_policy();
void test_tool_validation_plan();
void test_tool_publish();
void test_tool_web_storage();
void test_tool_webgpu_staging();
void test_tool_types();
void test_tool_ecs();
void test_tool_ecs_relationships();
void test_tool_ecs_hierarchy();
void test_tool_ecs_world();
void test_tool_ecs_query();
void test_tool_ecs_query_benchmark();
void test_tool_validate_project(const char *binary);
void test_tool_prototype_expansion(const char *binary);
void test_tool_prepared(const char *producer, const char *consumer);
void test_tool_prepared_runtime(const char *directory);
void test_tool_scene();
void test_scene_prepared();
#ifdef CHIBI_TEST_RENDER_RUNTIME
void test_render_resources();
void test_render_interior_border();
void test_render_viewport_resize();
void test_render_radiance_layout();
void test_render_radiance();
void test_render_radiance_depth();
void test_render_material_sampling();
void test_render_material_borders();
void test_render_material_coefficients();
void test_render_material_lod();
void test_render_scene2d();
void test_render_scene3d(bool gles);
void test_render_scene3d_gi(bool gles);
void test_device_controls();
#endif
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
void test_render_material_ramps();
void test_render_sdf_visibility();
void test_render_radiance_surface();
void test_render_material_pixels();
void test_render_radiance_horizons(int directions);
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
void test_new_asset();
void test_asset_refactor();
static bool source_runtime_active;
bool test_source_runtime_active()
{
    return source_runtime_active;
}
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
void test_source_native_features();
void test_source_presentation();
void test_component_presentation();
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
#ifdef CHIBI_TEST_GRAPH_AUTHORING
void test_graph_comments();
void test_graph_indexes();
void test_graph_inline();
void test_graph_find();
void test_graph_describe();
void test_graph_functions();
void test_graph_signature();
void test_graph_collapse();
void test_graph_problems();
void test_vector_nodes();
#endif
#ifdef CHIBI_TEST_MATERIAL_RUNTIME
void test_material_destruction();
void test_material_fluids();
void test_material_mpm();
void test_material_thermal();
void test_material_reactions();
void test_material_simulation();
void test_material_mixed();
void test_material_obstacles();
void test_material_particle_index();
void test_material_blast();
void test_material_constitutive();
void test_material_detail();
void test_material_impulse();
void test_material_benchmark();
void test_contact_benchmark();
void test_native_catalog_enable();
enum
{
    MATERIAL_NONE,
    MATERIAL_DESTRUCTION,
    MATERIAL_FLUIDS,
    MATERIAL_MPM,
    MATERIAL_THERMAL,
    MATERIAL_SIMULATION,
    MATERIAL_ECS,
    MATERIAL_BENCHMARK,
    CONTACT_BENCHMARK,
    MATERIAL_SELECTORS
};
static const char *const material_selectors[MATERIAL_SELECTORS] = {
    [MATERIAL_DESTRUCTION] = "--material-destruction",
    [MATERIAL_FLUIDS] = "--material-fluids",
    [MATERIAL_MPM] = "--material-mpm",
    [MATERIAL_THERMAL] = "--material-thermal",
    [MATERIAL_SIMULATION] = "--material-simulation",
    [MATERIAL_ECS] = "--material-ecs",
    [MATERIAL_BENCHMARK] = "--material-benchmark",
    [CONTACT_BENCHMARK] = "--contact-benchmark",
};
#endif
#ifdef CHIBI_TEST_BOX3D
void test_box3d();
#endif
#ifdef CHIBI_TEST_IMAGE_PREPARATION
void test_sky_files();
#endif
#ifdef CHIBI_TEST_MESH_PREPARATION
void test_mesh_files();
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
void test_model_import();
#endif
#ifdef CHIBI_TEST_REFLECT
void test_tool_reflect();
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_native();
void test_tool_native_values();
void test_tool_native_catalog();
void test_tool_physics();
void test_tool_physics3d();
void test_tool_transforms();
#endif

static bool shader_parameters;
bool test_shader_parameters_active()
{
    return shader_parameters;
}

#ifdef CHIBI_TEST_MATERIAL_GRAPHS
static bool material_graphs;
bool test_material_graphs_active()
{
    return material_graphs;
}
#endif

#ifdef CHIBI_TEST_MODEL_IMPORT
/* The import cases, and the render cases that draw what imports write. */
static bool model_import_types;
bool test_model_import_active()
{
    return model_import_types;
}
#endif

int main(int argc, char **argv)
{
    bool tilemap = false, tilemap_project = false;
#ifdef CHIBI_TEST_TILEMAP
    tilemap = argc == 2 && !strcmp(argv[1], "--tilemap");
#endif
#ifdef CHIBI_TEST_TILEMAP_PROJECT
    tilemap_project = argc == 2 && !strcmp(argv[1], "--tilemap-project");
#endif
    bool prepared = argc == 3 && !strcmp(argv[1], "--prepared");
    bool read = argc == 3 && !strcmp(argv[1], "--prepared-read");
    bool package = argc == 2 && !strcmp(argv[1], "--package");
    bool unit = argc == 2 && !strcmp(argv[1], "--unit");
    bool webgpu_staging = argc == 2 && !strcmp(argv[1], "--webgpu-staging");
    bool web_storage = argc == 2 && !strcmp(argv[1], "--web-storage");
    bool query_benchmark = argc == 2 && !strcmp(argv[1], "--ecs-query-benchmark");
    bool scene_prepared = argc == 2 && !strcmp(argv[1], "--scene-prepared");
    bool scene = argc == 2 && !strcmp(argv[1], "--scene");
    bool validate_project = argc == 3 && !strcmp(argv[1], "--validate-project");
    bool expansion = argc == 3 && !strcmp(argv[1], "--prototype-expansion");
    bool native = false, native_values = false, native_catalog = false,
         physics = false, physics3d = false, transforms = false,
         render_resources = false, interior_border = false,
         viewport_resize = false, sdf_visibility = false, radiance = false,
         radiance_depth = false, material_sampling = false, scene2d = false,
         scene3d = false, material_ramps = false, radiance_surface = false,
         material_pixels = false, box3d = false, sky_files = false,
         mesh_files = false, model_import = false, material_render = false;
    /* The horizon directions --radiance-horizons4 or 8 checks. */
    int radiance_horizons = 0;
    bool whole_math = false, whole_integer = false, whole_conversion = false,
         whole_select = false, owned_tick = false, parallel_tick = false,
         whole_alias = false, persistent_store = false;
    bool source_features = false, source_numeric = false,
         source_material = false, source_presentation = false,
         system_schedule = false, source_systems = false, source_edit = false,
         new_asset = false, asset_refactor = false;
    int corpus = 0;
    /* The material selector given, or none. */
    int material = 0;
#ifdef CHIBI_TEST_MATERIAL_RUNTIME
    for (int i = MATERIAL_NONE + 1; argc == 2 && i < MATERIAL_SELECTORS; i++) {
        if (!strcmp(argv[1], material_selectors[i])) {
            material = i;
        }
    }
#endif
    bool coverage = false;
    bool graph_document = false, graph_presentation = false,
         graph_functions = false, vector_nodes = false;
#ifdef CHIBI_TEST_GRAPH_AUTHORING
    graph_document = argc == 2 && !strcmp(argv[1], "--graph-document");
    graph_presentation = argc == 2 && !strcmp(argv[1], "--graph-presentation");
    graph_functions = argc == 2 && !strcmp(argv[1], "--graph-functions");
    vector_nodes = argc == 2 && !strcmp(argv[1], "--vector-nodes");
#endif
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
    new_asset = argc == 2 && !strcmp(argv[1], "--new-asset");
    asset_refactor = argc == 2 && !strcmp(argv[1], "--asset-refactor");
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
    interior_border = argc == 2 && !strcmp(argv[1], "--interior-border");
    viewport_resize = argc == 2 && !strcmp(argv[1], "--viewport-resize");
    radiance = argc == 2 && !strcmp(argv[1], "--radiance");
    radiance_depth = argc == 2 && !strcmp(argv[1], "--radiance-depth");
    material_sampling = argc == 2 && !strcmp(argv[1], "--material-sampling");
    material_render = argc == 2 && !strcmp(argv[1], "--material-render");
    scene2d = argc == 2 && !strcmp(argv[1], "--scene2d");
    bool scene3d_gi = argc == 2 && (!strcmp(argv[1], "--scene3d-gi") ||
                                    !strcmp(argv[1], "--scene3d-gi-gles"));
    bool scene3d_gles = argc == 2 && (!strcmp(argv[1], "--scene3d-gles") ||
                                      !strcmp(argv[1], "--scene3d-gi-gles"));
    scene3d = scene3d_gi || scene3d_gles ||
              (argc == 2 && !strcmp(argv[1], "--scene3d"));
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
    material_ramps = argc == 2 && !strcmp(argv[1], "--material-ramps");
    sdf_visibility = argc == 2 && !strcmp(argv[1], "--sdf-visibility");
    radiance_surface = argc == 2 && !strcmp(argv[1], "--radiance-surface");
    material_pixels = argc == 2 && !strcmp(argv[1], "--material-pixels");
    if (argc == 2 && !strcmp(argv[1], "--radiance-horizons4")) {
        radiance_horizons = 4;
    }
    if (argc == 2 && !strcmp(argv[1], "--radiance-horizons8")) {
        radiance_horizons = 8;
    }
    material_graphs = scene3d || material_ramps || radiance_surface ||
                      radiance_horizons || material_pixels;
#endif
#endif
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
#ifdef CHIBI_TEST_IMAGE_PREPARATION
    sky_files = argc == 2 && !strcmp(argv[1], "--sky-files");
#endif
#ifdef CHIBI_TEST_MESH_PREPARATION
    mesh_files = argc == 2 && !strcmp(argv[1], "--mesh-files");
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
    model_import = argc == 2 && !strcmp(argv[1], "--model-import");
    model_import_types = model_import || scene3d;
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_catalog = argc == 2 && !strcmp(argv[1], "--native-catalog");
    native_values = argc == 2 && !strcmp(argv[1], "--native-values");
    native = argc == 2 && !strcmp(argv[1], "--native");
    physics = argc == 2 && !strcmp(argv[1], "--physics");
    physics3d = argc == 2 && !strcmp(argv[1], "--physics3d");
    transforms = argc == 2 && !strcmp(argv[1], "--transforms");
#endif
    if (argc != 1 && !corpus && !coverage && !shader_parameters && !source_edit &&
        !new_asset && !asset_refactor && !tilemap && !tilemap_project &&
        !package && !unit && !web_storage && !webgpu_staging &&
        !query_benchmark && !prepared && !read && !scene && !scene_prepared &&
        !native && !native_values && !native_catalog && !physics && !physics3d &&
        !transforms && !render_resources && !interior_border && !viewport_resize &&
        !sdf_visibility && !radiance && !radiance_depth && !material_sampling &&
        !scene2d && !scene3d && !material_ramps && !radiance_surface &&
        !radiance_horizons && !material_pixels && !box3d && !sky_files &&
        !mesh_files && !model_import && !persistent_store && !whole_alias &&
        !whole_math && !whole_integer && !whole_conversion && !whole_select &&
        !owned_tick && !parallel_tick && !source_features && !source_numeric &&
        !source_material && !source_presentation && !system_schedule &&
        !source_systems && !validate_project && !expansion && !graph_document &&
        !graph_presentation && !graph_functions && !vector_nodes &&
        !material_render && !material) {
        fprintf(
            stderr,
            "Usage: %s [--tilemap | --tilemap-project | --package | --unit | "
            "--web-storage | "
            "--webgpu-staging | "
            "--ecs-query-benchmark | "
            "--validate-project chibi_ctl | "
            "--prototype-expansion chibi_ctl | "
            "--prepared "
            "test_language | --prepared-read directory | --scene | "
            "--scene-prepared | --native | --native-values | --native-catalog "
            "| --physics | --physics3d | --transforms "
            "| --render-resources | --material-render | --interior-border | "
            "--viewport-resize | "
            "--sdf-visibility | "
            "--radiance | --radiance-depth | --material-sampling | --scene2d | "
            "--scene3d | --scene3d-gles | --scene3d-gi | --scene3d-gi-gles | "
            "--material-ramps | --radiance-surface | --radiance-horizons4 | "
            "--radiance-horizons8 | --material-pixels | "
            "--box3d | "
            "--sky-files | --mesh-files | --model-import | "
            "--material-destruction | --material-fluids | --material-mpm | "
            "--material-thermal | "
            "--material-simulation | --material-ecs | --material-benchmark | "
            "--contact-benchmark | "
            "--persistent-store | "
            "--whole-alias | "
            "--whole-math | --whole-integer | --whole-conversion | "
            "--whole-select | --owned-tick | "
            "--parallel-tick | --source-native-features | "
            "--source-native-numeric-queries | --source-native-material | "
            "--source-presentation | --system-schedule | --source-systems | "
            "--source-edit | --new-asset | --asset-refactor | "
            "--shader-parameters-prepared | "
            "--graph-document | --graph-presentation | --graph-functions | "
            "--vector-nodes | "
            "--semantic-corpora "
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
                            source_edit || new_asset || asset_refactor ||
                            system_schedule || source_systems || persistent_store ||
                            source_presentation || whole_alias || whole_math ||
                            whole_integer || whole_conversion || whole_select ||
                            owned_tick || parallel_tick || source_features ||
                            source_numeric || source_material || graph_document ||
                            graph_presentation || graph_functions || vector_nodes;
    if (source_runtime_active) {
#ifdef CHIBI_TEST_FEATURE_COVERAGE
        if (coverage) {
            return run_feature_coverage(argv[2]);
        }
#endif
#ifdef CHIBI_TEST_GRAPH_AUTHORING
        /* The graph palette composes over the game and Studio type packs. */
        if (graph_document || graph_presentation || graph_functions || vector_nodes) {
            if (graph_document) {
                test_graph_comments();
                test_graph_indexes();
            } else if (graph_presentation) {
                test_graph_inline();
                test_graph_find();
                test_graph_describe();
            } else if (graph_functions) {
                test_graph_functions();
                test_graph_signature();
                test_graph_collapse();
                test_graph_problems();
            } else {
                test_vector_nodes();
            }
            pu_print_stats();
            return pu_test_failed() ? 1 : 0;
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
            test_component_presentation();
        } else if (source_features) {
            test_source_native_features();
        } else if (source_numeric) {
            test_source_native_numeric_queries();
        } else if (source_material) {
            test_source_native_material_features();
        } else
#endif
            if (new_asset) {
            test_new_asset();
        } else if (asset_refactor) {
            test_asset_refactor();
        } else if (persistent_store) {
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
#ifdef CHIBI_TEST_MATERIAL_RUNTIME
    if (material) {
        test_native_catalog_enable(); /* Contact and material events copy a CF_V2. */
        switch (material) {
            case MATERIAL_DESTRUCTION: test_material_destruction(); break;
            case MATERIAL_FLUIDS: test_material_fluids(); break;
            case MATERIAL_MPM: test_material_mpm(); break;
            case MATERIAL_THERMAL: test_material_thermal(); break;
            case MATERIAL_SIMULATION:
                test_material_simulation();
                test_material_mixed();
                test_material_obstacles();
                test_material_particle_index();
                test_material_reactions();
                break;
            case MATERIAL_ECS:
                test_material_blast();
                test_material_constitutive();
                test_material_detail();
                test_material_impulse();
                break;
            case MATERIAL_BENCHMARK: test_material_benchmark(); break;
            case CONTACT_BENCHMARK: test_contact_benchmark(); break;
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_RENDER_RUNTIME
    if (render_resources || interior_border || viewport_resize || radiance ||
        radiance_depth || material_sampling || scene2d || scene3d || material_render) {
        if (interior_border) {
            test_render_interior_border();
        } else if (viewport_resize) {
            test_render_viewport_resize();
        } else if (radiance) {
            test_render_radiance();
        } else if (radiance_depth) {
            test_render_radiance_depth();
        } else if (material_sampling) {
            test_render_material_sampling();
        } else if (scene2d) {
            test_render_scene2d();
        } else if (scene3d_gi) {
            test_render_scene3d_gi(scene3d_gles);
        } else if (scene3d) {
            test_render_scene3d(scene3d_gles);
        } else if (material_render) {
            test_render_material_coefficients();
            test_render_material_lod();
        } else {
            test_render_resources();
            test_render_radiance_layout();
            test_render_material_borders();
            test_device_controls();
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_MATERIAL_GRAPHS
    if (material_ramps || sdf_visibility || radiance_surface ||
        radiance_horizons || material_pixels) {
        if (sdf_visibility) {
            test_render_sdf_visibility();
        } else if (material_pixels) {
            test_render_material_pixels();
        } else if (radiance_surface) {
            test_render_radiance_surface();
        } else if (radiance_horizons) {
            test_render_radiance_horizons(radiance_horizons);
        } else {
            test_render_material_ramps();
        }
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
#ifdef CHIBI_TEST_IMAGE_PREPARATION
    if (sky_files) {
        test_sky_files();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_MESH_PREPARATION
    if (mesh_files) {
        test_mesh_files();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_MODEL_IMPORT
    if (model_import) {
        test_model_import();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    if (native || native_values || native_catalog || physics || physics3d || transforms) {
        if (physics) {
            test_tool_physics();
        } else if (physics3d) {
            test_tool_physics3d();
        } else if (transforms) {
            test_tool_transforms();
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
#ifdef CHIBI_TEST_TILEMAP
    if (tilemap) {
        test_tool_tilemap();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_TILEMAP_PROJECT
    if (tilemap_project) {
        test_tool_tilemap_project();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
    if (webgpu_staging) {
        test_tool_webgpu_staging();
    } else if (package) {
        test_tool_package();
    } else if (scene_prepared) {
        test_scene_prepared();
    } else if (scene) {
        test_tool_scene();
    } else if (prepared) {
        test_tool_prepared(argv[2], argv[0]);
    } else if (read) {
        test_tool_prepared_runtime(argv[2]);
    } else if (validate_project) {
        test_tool_validate_project(argv[2]);
    } else if (expansion) {
        test_tool_prototype_expansion(argv[2]);
    } else if (unit) {
        test_tool_execution_policy();
    } else if (web_storage) {
        test_tool_web_storage();
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
#ifdef CHIBI_TEST_REFLECT
        test_tool_reflect();
#endif
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

#endif

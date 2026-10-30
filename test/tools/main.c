#if !defined(__EMSCRIPTEN__) && !defined(CHIBI_TEST_PROGRAM_ONLY)
#define pu_run_test tool_pico_run_test
#endif
#define PICO_UNIT_IMPLEMENTATION
#ifndef CHIBI_TEST_PROGRAM_ONLY
#include "test/tools/fixture.h"
#include "test/tools/graph_dispatch.h"
#endif
#include "third_party/pico_headers/pico_unit.h"
#undef pu_run_test

#include <stdio.h>
#include <string.h>

#if defined(CHIBI_TEST_PROGRAM_ONLY)
#include "types/catalog.h"

#include <assert.h>

/* This consumer has no reflected native structs or enums. */
int type_catalog_pack_count()
{
    return 0;
}

type_catalog_pack type_catalog_pack_at(int)
{
    assert(false);
    return (type_catalog_pack){ 0 };
}

void test_program_sdk();
int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--program-sdk")) {
        return 2;
    }
    pu_display_time(true);
    test_program_sdk();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
#elif defined(CHIBI_TEST_WEBGPU_STAGING) || defined(CHIBI_TEST_AUDIO_WEB)
#ifdef CHIBI_TEST_WEBGPU_STAGING
int test_webgpu_staging_start();
#endif
#ifdef CHIBI_TEST_AUDIO_WEB
int test_web_audio_start();
#endif
int main(int argc, char **argv)
{
#ifdef CHIBI_TEST_AUDIO_WEB
    if (argc == 2 && !strcmp(argv[1], "--audio-browser")) {
        return test_web_audio_start();
    }
#endif
#ifdef CHIBI_TEST_WEBGPU_STAGING
    if (argc == 2 && !strcmp(argv[1], "--webgpu-staging")) {
        return test_webgpu_staging_start();
    }
#endif
    return 2;
}
#else
#ifdef CHIBI_TEST_SHADER_BAKE
void test_tool_shader_bake();
void test_tool_shader_source_debug();
#endif
#ifdef CHIBI_TEST_TILEMAP
void test_tool_tilemap();
#endif
void test_tool_execution_policy();
void test_tool_package();
void test_tool_chibi_cli(const char *binary, const char *self);
void test_tool_chibi_cli_process(const char *binary, const char *self);
void test_tool_chibi_cli_build(const char *binary);
void test_tool_chibi_cli_doctor(const char *binary);
void test_tool_chibi_cli_installed(const char *binary, const char *self);
void test_native_build(const char *binary);
void test_native_workload_benchmark(const char *binary);
void test_tool_chibi_cli_export(const char *binary);
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
void test_tool_compiler_artifact(const char *producer, const char *host);
void test_tool_compiler_bootstrap(const char *producer, const char *host);
void test_tool_bootstrap_recovery(const char *host, const char *artifact);
#endif
#ifdef CHIBI_TEST_BUILD_PROCESS
void test_native_aot_driver(const char *source);
void test_tool_build_process(const char *worker, const char *host);
void test_tool_build_process_files(const char *worker, const char *host);
void test_tool_build_timings(const char *worker, const char *host);
void test_build_graph(const char *host);
void test_build_graph_link_response(const char *host);
void test_build_graph_support(const char *host);
void test_build_graph_reflect(const char *host);
void test_build_graph_generated(const char *host);
void test_build_graph_debug_optimization(const char *host);
void test_build_graph_programs(const char *host);
#ifndef _WIN32
void test_build_sdk(const char *host, const char *build, const char *third_party);
void test_runtime_sdk(const char *host);
#endif
#endif
void test_tool_third_party_publish();
void test_tool_collab();
void test_tool_test_policy();
void test_tool_validation_plan();
void test_tool_web_storage();
void test_tool_player_url();
#ifdef CHIBI_TEST_AUDIO_ANALYSIS
void test_tool_audio_analysis();
#endif
#ifdef CHIBI_TEST_SHADER_WEBGPU
void test_tool_shader_webgpu();
#endif
void test_tool_webgpu_staging();
void test_tool_wasm_browser();
void test_tool_types();
void test_tool_ecs();
void test_tool_ecs_strings();
void test_tool_ecs_relationships();
void test_tool_ecs_hierarchy();
void test_tool_ecs_world();
void test_tool_ecs_query();
void test_tool_ecs_query_benchmark();
void test_arena();
#ifdef CHIBI_TEST_PROGRAM
void test_program_sdk();
#endif
void test_tool_scene();
#ifdef CHIBI_TEST_SCENE_SNAPSHOT_READ
#endif
void test_scene_prepared();
#ifdef CHIBI_TEST_RENDER_RUNTIME
void test_render_resources();
void test_particle_fields();
void test_tool_audio_output();
void test_tool_audio_browser();
void test_tool_player_clock();
void test_terrain_playable_render(
    const char *player,
    const char *artifact,
    const char *resources,
    const char *shaders,
    const char *output
);
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
void test_render_material_terrain_seams();
void test_device_controls();
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
void test_compiled_math();
void test_compiled_integers();
void test_compiled_conversions();
void test_compiled_selects();
void test_persistent_stores();
static bool source_runtime_active;
bool test_source_runtime_active()
{
    return source_runtime_active;
}
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
void test_tool_scheduler_benchmark();
void test_tool_scheduler();
#endif
#ifdef CHIBI_TEST_MATERIAL_RUNTIME
void test_material_destruction();
void test_material_fluids();
void test_material_mpm();
void test_material_simulation();
void test_material_mixed();
void test_material_obstacles();
void test_material_particle_index();
void test_material_benchmark();
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
#ifdef CHIBI_TEST_REFLECT
void test_tool_reflect();
void test_tool_reflect_ranges();
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_terrain_seams();
void test_terrain_storage();
void test_terrain_checkpoint(const char *binary);
int terrain_checkpoint_child(const char *filename);
void test_tool_native();
void test_tool_native_values();
void test_tool_native_catalog();
void test_tool_physics();
void test_tool_physics3d();
void test_tool_transforms();
#endif

#ifdef CHIBI_TEST_ASSET_VALUES
void test_asset_text();
void test_asset_value();
#endif
#ifdef CHIBI_TEST_UI_DOCUMENT
void test_ui_document();
void test_rcss();
void test_file_watch();
void test_text_overlay();
#endif
#ifdef CHIBI_TEST_FMT
void test_fmt();
#endif
#ifdef CHIBI_TEST_PARTICLES
void test_particles();
#endif
#ifdef CHIBI_TEST_PNG_ENCODING
void test_png_encoding();
#endif

/* Selectors of documents, files and runtime pieces with one suite each. */
static const char *const document_selectors[] = {
    "--player-url",      "--audio-analysis",
    "--audio-browser",   "--shader-webgpu",
    "--asset-values",    "--ui-document",
    "--text-files",      "--fmt",
    "--particles",       "--png-encoding",
    "--reference-index", "--asset-source",
    "--behavior-loader", "--engine-clock",
    "--preview-layout",  "--fields",
    "--player-clock",
};

static bool selected(int argc, char **argv, const char *name)
{
    return argc == 2 && !strcmp(argv[1], name);
}

static bool document_selector(int argc, char **argv)
{
    for (size_t i = 0; i < sizeof document_selectors / sizeof *document_selectors; i++) {
        if (selected(argc, argv, document_selectors[i])) {
            return true;
        }
    }
    return false;
}

/* Runs the suites of a document selector; false when none is built. */
static bool run_document_suites(int argc, char **argv)
{
    if (selected(argc, argv, "--player-url")) {
        test_tool_player_url();
        return true;
    }
#ifdef CHIBI_TEST_RENDER_RUNTIME
    if (selected(argc, argv, "--player-clock")) {
        test_tool_player_clock();
        return true;
    }
    if (selected(argc, argv, "--audio-browser")) {
        test_tool_audio_browser();
        return true;
    }
#endif
#ifdef CHIBI_TEST_AUDIO_ANALYSIS
    if (selected(argc, argv, "--audio-analysis")) {
        test_tool_audio_analysis();
#ifdef CHIBI_TEST_RENDER_RUNTIME
        test_tool_audio_output();
#endif
        return true;
    }
#endif
#ifdef CHIBI_TEST_SHADER_WEBGPU
    if (selected(argc, argv, "--shader-webgpu")) {
        test_tool_shader_webgpu();
        return true;
    }
#endif
#ifdef CHIBI_TEST_ASSET_VALUES
    if (selected(argc, argv, "--asset-values")) {
        test_asset_text();
        test_asset_value();
        return true;
    }
#endif
#ifdef CHIBI_TEST_UI_DOCUMENT
    if (selected(argc, argv, "--ui-document")) {
        test_ui_document();
        test_rcss();
        return true;
    }
    if (selected(argc, argv, "--text-files")) {
        test_file_watch();
        test_text_overlay();
        return true;
    }
#endif
#ifdef CHIBI_TEST_FMT
    if (selected(argc, argv, "--fmt")) {
        test_fmt();
        return true;
    }
#endif
#ifdef CHIBI_TEST_PARTICLES
    if (selected(argc, argv, "--particles")) {
        test_particles();
        return true;
    }
#endif
#ifdef CHIBI_TEST_PNG_ENCODING
    if (selected(argc, argv, "--png-encoding")) {
        test_png_encoding();
        return true;
    }
#endif
#ifdef CHIBI_TEST_AUTHORING_DOCUMENTS
    /* These read the game's reflected types through the type catalog. */
    source_runtime_active = true;
    if (selected(argc, argv, "--reference-index")) {
        return true;
    }
    if (selected(argc, argv, "--asset-source")) {
        return true;
    }
#endif
    return false;
}

static bool shader_parameters;
bool test_shader_parameters_active()
{
    return shader_parameters;
}

void test_tool_case_timings(const char *self);
int tool_case_timing_probe(const char *mode);
int tool_tty_echo_child(bool terminal);

int main(int argc, char **argv)
{
    if ((argc == 2 || (argc == 3 && !strcmp(argv[2], "--tty"))) &&
        !strcmp(argv[1], "--tty-echo-child")) {
        return tool_tty_echo_child(argc == 3);
    }
    if (argc == 3 && !strcmp(argv[1], "--case-timing-probe")) {
        return tool_case_timing_probe(argv[2]);
    }
    if (argc == 2 && !strcmp(argv[1], "--case-timings")) {
        tool_init();
        test_tool_case_timings(argv[0]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--native-aot-driver")) {
#ifdef CHIBI_TEST_BUILD_PROCESS
        tool_init();
        pu_display_time(true);
        test_native_aot_driver(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 5 && !strcmp(argv[1], "--build-sdk")) {
#if defined(CHIBI_TEST_BUILD_PROCESS) && !defined(_WIN32)
        tool_init();
        pu_display_time(true);
        test_build_sdk(argv[2], argv[3], argv[4]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 3 && !strcmp(argv[1], "--runtime-sdk")) {
#if defined(CHIBI_TEST_BUILD_PROCESS) && !defined(_WIN32)
        tool_init();
        pu_display_time(true);
        test_runtime_sdk(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 3 && !strcmp(argv[1], "--build-graph-link-response")) {
#ifdef CHIBI_TEST_BUILD_PROCESS
        tool_init();
        pu_display_time(true);
        test_build_graph_link_response(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 3 && (!strcmp(argv[1], "--build-graph") ||
                      !strcmp(argv[1], "--build-graph-support") ||
                      !strcmp(argv[1], "--build-graph-reflect") ||
                      !strcmp(argv[1], "--build-graph-generated") ||
                      !strcmp(argv[1], "--build-graph-programs") ||
                      !strcmp(argv[1], "--build-graph-debug-optimization"))) {
#ifdef CHIBI_TEST_BUILD_PROCESS
        tool_init();
        pu_display_time(true);
        if (!strcmp(argv[1], "--build-graph")) {
            test_build_graph(argv[2]);
        } else if (!strcmp(argv[1], "--build-graph-support")) {
            test_build_graph_support(argv[2]);
        } else if (!strcmp(argv[1], "--build-graph-reflect")) {
            test_build_graph_reflect(argv[2]);
        } else if (!strcmp(argv[1], "--build-graph-debug-optimization")) {
            test_build_graph_debug_optimization(argv[2]);
        } else if (!strcmp(argv[1], "--build-graph-programs")) {
            test_build_graph_programs(argv[2]);
        } else {
            test_build_graph_generated(argv[2]);
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 4 && !strcmp(argv[1], "--compiler-artifact")) {
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
        tool_init();
        pu_display_time(true);
        test_tool_compiler_artifact(argv[2], argv[3]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private compiler artifact host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 4 && !strcmp(argv[1], "--compiler-bootstrap")) {
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
        tool_init();
        pu_display_time(true);
        test_tool_compiler_bootstrap(argv[2], argv[3]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private compiler artifact host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 4 && !strcmp(argv[1], "--bootstrap-recovery")) {
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
        tool_init();
        pu_display_time(true);
        test_tool_bootstrap_recovery(argv[2], argv[3]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private compiler artifact host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 4 && (!strcmp(argv[1], "--build-process") ||
                      !strcmp(argv[1], "--build-process-files") ||
                      !strcmp(argv[1], "--build-timings"))) {
#ifdef CHIBI_TEST_BUILD_PROCESS
        tool_init();
        pu_display_time(true);
        if (!strcmp(argv[1], "--build-process-files")) {
            test_tool_build_process_files(argv[2], argv[3]);
        } else if (!strcmp(argv[1], "--build-timings")) {
            test_tool_build_timings(argv[2], argv[3]);
        } else {
            test_tool_build_process(argv[2], argv[3]);
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires the private native build host\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 2 && !strcmp(argv[1], "--program-sdk")) {
#ifdef CHIBI_TEST_PROGRAM
        pu_display_time(true);
        test_program_sdk();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s requires Chibi::program\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 3 && !strcmp(argv[1], "--chibi-cli")) {
        tool_init();
        pu_display_time(true);
        test_tool_chibi_cli(argv[2], argv[0]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && (!strcmp(argv[1], "--chibi-cli-build") ||
                      !strcmp(argv[1], "--chibi-cli-doctor"))) {
        tool_init();
        pu_display_time(true);
        if (!strcmp(argv[1], "--chibi-cli-build")) {
            test_tool_chibi_cli_build(argv[2]);
        } else {
            test_tool_chibi_cli_doctor(argv[2]);
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--chibi-cli-process")) {
        tool_init();
        pu_display_time(true);
        test_tool_chibi_cli_process(argv[2], argv[0]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--chibi-cli-export")) {
        tool_init();
        pu_display_time(true);
        test_tool_chibi_cli_export(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--native-build")) {
        tool_init();
        pu_display_time(true);
        test_native_build(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--native-workload-benchmark")) {
        tool_init();
        pu_display_time(true);
        test_native_workload_benchmark(argv[2]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--chibi-cli-installed")) {
        tool_init();
        pu_display_time(true);
        test_tool_chibi_cli_installed(argv[2], argv[0]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--surface-transport")) {
    }
    if (argc == 2 && (!strcmp(argv[1], "--scene3d-camera") ||
                      !strcmp(argv[1], "--scene3d-volume") ||
                      !strcmp(argv[1], "--scene3d-volume-gpu") ||
                      !strcmp(argv[1], "--scene3d-terrain") ||
                      !strcmp(argv[1], "--scene3d-terrain-gpu"))) {
#ifdef CHIBI_TEST_RENDER_RUNTIME
        void test_tool_scene3d_camera();
        void test_scene3d_volume();
        void test_scene3d_volume_gpu();
        void test_tool_scene3d_terrain();
        void test_tool_scene3d_terrain_gpu();
        tool_init();
        pu_display_time(true);
        if (!strcmp(argv[1], "--scene3d-camera")) {
            test_tool_scene3d_camera();
        } else if (!strcmp(argv[1], "--scene3d-volume")) {
            test_scene3d_volume();
        } else if (!strcmp(argv[1], "--scene3d-volume-gpu")) {
            test_scene3d_volume_gpu();
        } else if (!strcmp(argv[1], "--scene3d-terrain")) {
            test_tool_scene3d_terrain();
        } else {
            test_tool_scene3d_terrain_gpu();
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        return 2;
#endif
    }
    if (argc == 2 && !strcmp(argv[1], "--material-surface-mesh")) {
#ifdef CHIBI_TEST_NATIVE_RUNTIME
        void test_tool_material_surface_mesh();
        void test_tool_terrain_volume();
        tool_init();
        pu_display_time(true);
        test_tool_material_surface_mesh();
        test_tool_terrain_volume();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        return 2;
#endif
    }
    if (argc == 2 && !strcmp(argv[1], "--terrain-cut-benchmark")) {
#ifdef CHIBI_TEST_RENDER_RUNTIME
        void test_tool_terrain_cut_benchmark();
        tool_init();
        pu_display_time(true);
        test_tool_terrain_cut_benchmark();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        return 2;
#endif
    }
    if (argc == 2 && (!strcmp(argv[1], "--material-mesh-cache") ||
                      !strcmp(argv[1], "--hrc-spot"))) {
    }
    if (argc == 7 && !strcmp(argv[1], "--terrain-playable-render")) {
#ifdef CHIBI_TEST_RENDER_RUNTIME
        tool_init();
        pu_display_time(true);
        test_terrain_playable_render(argv[2], argv[3], argv[4], argv[5], argv[6]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        return 2;
#endif
    }
    if (document_selector(argc, argv)) {
        tool_init();
        pu_display_time(true);
        if (!run_document_suites(argc, argv)) {
            fprintf(stderr, "%s is not built in this configuration\n", argv[1]);
            return 2;
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--ecs-strings")) {
        tool_init();
        pu_display_time(true);
        test_tool_ecs_strings();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--ecs-query")) {
        tool_init();
        pu_display_time(true);
        test_tool_ecs_query();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--reflect-ranges")) {
#ifdef CHIBI_TEST_REFLECT
        tool_init();
        pu_display_time(true);
        test_tool_reflect_ranges();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s is not built in this configuration\n", argv[1]);
        return 2;
#endif
    }
    if ((argc == 2 && !strcmp(argv[1], "--terrain-checkpoint")) ||
        (argc == 3 && !strcmp(argv[1], "--terrain-checkpoint-child"))) {
#ifdef CHIBI_TEST_NATIVE_RUNTIME
        tool_init();
#ifdef CHIBI_TEST_SOURCE_RUNTIME
        source_runtime_active = true;
#endif
        if (argc == 3) {
            return terrain_checkpoint_child(argv[2]);
        }
        pu_display_time(true);
        test_terrain_checkpoint(argv[0]);
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s is not built in this configuration\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 2 && !strcmp(argv[1], "--terrain-storage")) {
#ifdef CHIBI_TEST_NATIVE_RUNTIME
        tool_init();
        pu_display_time(true);
        test_terrain_storage();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        return 2;
#endif
    }
    if ((argc == 3 && !strcmp(argv[1], "--terrain-playable")) ||
        (argc == 6 && !strcmp(argv[1], "--terrain-playable-child"))) {
    }
    if (argc == 2 && !strcmp(argv[1], "--terrain-seams")) {
#ifdef CHIBI_TEST_NATIVE_RUNTIME
        tool_init();
        pu_display_time(true);
        test_tool_terrain_seams();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s is not built in this configuration\n", argv[1]);
        return 2;
#endif
    }
    if (argc == 2 && (!strcmp(argv[1], "--shader-bake") ||
                      !strcmp(argv[1], "--shader-source-debug"))) {
#ifdef CHIBI_TEST_SHADER_BAKE
        tool_init();
        pu_display_time(true);
        if (!strcmp(argv[1], "--shader-source-debug")) {
            test_tool_shader_source_debug();
        } else {
            test_tool_shader_bake();
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
#else
        fprintf(stderr, "%s is not built in this configuration\n", argv[1]);
        return 2;
#endif
    }
    bool tilemap = false;
#ifdef CHIBI_TEST_TILEMAP
    tilemap = argc == 2 && !strcmp(argv[1], "--tilemap");
#endif
    bool package = argc == 2 && !strcmp(argv[1], "--package");
    bool third_party_publish = argc == 2 && !strcmp(argv[1], "--third-party-publish");
    bool collab = argc == 2 && !strcmp(argv[1], "--collab");
    bool unit = argc == 2 && !strcmp(argv[1], "--unit");
    bool test_runner = argc == 2 && !strcmp(argv[1], "--test-runner");
    bool validation_plan = argc == 2 && !strcmp(argv[1], "--validation-plan");
    bool webgpu_staging = argc == 2 && !strcmp(argv[1], "--webgpu-staging");
    bool wasm_browser = argc == 2 && !strcmp(argv[1], "--wasm-browser");
    bool web_storage = argc == 2 && !strcmp(argv[1], "--web-storage");
    bool query_benchmark = argc == 2 && !strcmp(argv[1], "--ecs-query-benchmark");
    bool arena = argc == 2 && !strcmp(argv[1], "--arena");
    bool types = argc == 2 && !strcmp(argv[1], "--types");
    bool scene_prepared = argc == 2 && !strcmp(argv[1], "--scene-prepared");
    bool scene = argc == 2 && !strcmp(argv[1], "--scene");
#ifdef CHIBI_TEST_SCENE_SNAPSHOT_READ
#endif
    bool native = false, native_values = false, native_catalog = false,
         physics = false, physics3d = false, transforms = false,
         render_resources = false, interior_border = false, viewport_resize = false,
         sdf_visibility = false, radiance = false, radiance_depth = false,
         material_sampling = false, scene2d = false, terrain_seams = false,
         radiance_surface = false, material_pixels = false, box3d = false,
         sky_files = false, mesh_files = false, model_import = false,
         model_graphs = false, model_catalog = false, render_geometry = false,
         shader_graphs = false, shader_parity = false, shader_programs = false,
         material_cameras = false, mesh_materials = false, sdf_quality = false,
         scene3d_post = false, scene3d_shapes = false,
         texture_transform = false, material_render = false;
    /* The horizon directions --radiance-horizons4 or 8 checks. */
    int radiance_horizons = 0;
    bool whole_math = false, whole_integer = false, whole_conversion = false,
         whole_select = false, persistent_store = false;
    bool system_schedule = false, scheduler_benchmark = false,

         source_systems = false, source_query_rows = false, new_asset = false,
         asset_refactor = false;
    /* The material selector given, or none. */
    int material = 0;
#ifdef CHIBI_TEST_MATERIAL_RUNTIME
    for (int i = MATERIAL_NONE + 1; argc == 2 && i < MATERIAL_SELECTORS; i++) {
        if (!strcmp(argv[1], material_selectors[i])) {
            material = i;
        }
    }
#endif
    bool graph_document = false, graph_presentation = false,
         graph_functions = false, vector_nodes = false;
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    whole_math = argc == 2 && !strcmp(argv[1], "--whole-math");
    whole_integer = argc == 2 && !strcmp(argv[1], "--whole-integer");
    whole_conversion = argc == 2 && !strcmp(argv[1], "--whole-conversion");
    persistent_store = argc == 2 && !strcmp(argv[1], "--persistent-store");
    whole_select = argc == 2 && !strcmp(argv[1], "--whole-select");
    new_asset = argc == 2 && !strcmp(argv[1], "--new-asset");
    asset_refactor = argc == 2 && !strcmp(argv[1], "--asset-refactor");
#endif
#ifdef CHIBI_TEST_SOURCE_FEATURES
    shader_parameters = argc == 2 && !strcmp(argv[1], "--shader-parameters-prepared");
    system_schedule = argc == 2 && !strcmp(argv[1], "--system-schedule");
    scheduler_benchmark = argc == 2 && !strcmp(argv[1], "--scheduler-benchmark");
    source_systems = argc == 2 && !strcmp(argv[1], "--source-systems");
    source_query_rows = argc == 2 && !strcmp(argv[1], "--source-query-rows");
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
    terrain_seams = argc == 2 && !strcmp(argv[1], "--material-terrain-seams");
#endif
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
#ifdef CHIBI_TEST_IMAGE_PREPARATION
    sky_files = argc == 2 && !strcmp(argv[1], "--sky-files");
#endif
#ifdef CHIBI_TEST_RENDER_GEOMETRY
    render_geometry = argc == 2 && !strcmp(argv[1], "--render-geometry");
    scene3d_post = argc == 2 && !strcmp(argv[1], "--scene3d-post");
    scene3d_shapes = argc == 2 && !strcmp(argv[1], "--scene3d-shapes");
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_catalog = argc == 2 && !strcmp(argv[1], "--native-catalog");
    native_values = argc == 2 && !strcmp(argv[1], "--native-values");
    native = argc == 2 && !strcmp(argv[1], "--native");
    physics = argc == 2 && !strcmp(argv[1], "--physics");
    physics3d = argc == 2 && !strcmp(argv[1], "--physics3d");
    transforms = argc == 2 && !strcmp(argv[1], "--transforms");
#endif
    if (argc != 1 && !shader_parameters && !new_asset && !asset_refactor &&
        !tilemap && !package && !third_party_publish && !collab && !unit &&
        !test_runner && !validation_plan && !web_storage && !webgpu_staging &&
        !wasm_browser && !query_benchmark && !scene && !scene_prepared &&
        !native && !native_values && !native_catalog && !physics && !physics3d &&
        !transforms && !render_resources && !interior_border && !viewport_resize &&
        !sdf_visibility && !radiance && !radiance_depth && !material_sampling &&
        !scene2d && !terrain_seams && !texture_transform && !arena && !types &&
        !render_geometry && !radiance_surface && !radiance_horizons &&
        !material_pixels && !box3d && !sky_files && !mesh_files && !model_import &&
        !model_graphs && !model_catalog && !shader_graphs && !shader_parity &&
        !shader_programs && !material_cameras && !mesh_materials &&
        !sdf_quality && !scene3d_post && !scene3d_shapes && !persistent_store &&
        !whole_math && !whole_integer && !whole_conversion && !whole_select &&
        !system_schedule && !scheduler_benchmark && !source_systems &&
        !source_query_rows && !graph_document && !graph_presentation &&
        !graph_functions && !vector_nodes && !material_render && !material) {
        fprintf(
            stderr,
            "Usage: %s [--tilemap | --package | "
            "--third-party-publish | --collab | "
            "--unit | "
            "--test-runner | --validation-plan | "
            "--reflect-ranges | "
            "--ecs-query | --ecs-strings | "
            "--shader-bake | --terrain-seams | "
            "--material-terrain-seams | --terrain-checkpoint | "
            "--terrain-storage | "
            "--terrain-playable project "
            "| "
            "--terrain-playable-render player artifact resources shaders "
            "output | "
            "--web-storage | "
            "--webgpu-staging | --wasm-browser | "
            "--ecs-query-benchmark | --arena | --types | "
            ""
            "--scene | "
            "--scene-prepared | "
            "--native | --native-values "
            "| --native-catalog "
            "| --physics | --physics3d | --transforms "
            "| --render-resources | --render-geometry | --material-render | "
            "--interior-border | "
            "--viewport-resize | "
            "--sdf-visibility | "
            "--radiance | --radiance-depth | --material-sampling | --scene2d | "
            "--radiance-surface | --radiance-horizons4 | "
            "--radiance-horizons8 | --material-pixels | "
            "--texture-transform | "
            "--box3d | "
            "--sky-files | --mesh-files | --model-import | "
            "--material-destruction | --material-fluids | --material-mpm | "
            "--material-thermal | "
            "--material-simulation | --material-ecs | --material-benchmark | "
            "--contact-benchmark | "
            "--model-graphs | --model-catalog | --shader-graphs | "
            "--shader-parity | "
            "--shader-programs | --material-cameras | --mesh-materials | "
            "--sdf-quality | --scene3d-post | --scene3d-shapes | "
            "--persistent-store | "
            ""
            "--whole-math | --whole-integer | --whole-conversion | "
            "--whole-select | "
            ""
            ""
            "--system-schedule | --scheduler-benchmark "
            "| --source-systems | "
            "--source-query-rows | "
            ""
            ""
            "--new-asset | --asset-refactor | "
            "--shader-parameters-prepared | "
            "--graph-document | --graph-presentation | --graph-functions | "
            "--vector-nodes | "
            "--asset-values "
            "| "
            "--ui-document | --text-files | --fmt | --particles | "
            "--player-clock | --audio-analysis | --audio-browser | "
            "--shader-webgpu | "
            "--player-url | "
            "--png-encoding | "
            "--reference-index | --asset-source | --behavior-loader | "
            "--engine-clock | --preview-layout | --fields]\n",
            argv[0]
        );
        return 2;
    }
    tool_init();
    pu_display_time(true);
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    source_runtime_active = shader_parameters || new_asset || asset_refactor ||
                            system_schedule || scheduler_benchmark ||
                            source_systems || source_query_rows ||
                            persistent_store || whole_math || whole_integer ||
                            whole_conversion || whole_select || graph_document ||
                            graph_presentation || graph_functions || vector_nodes;
    if (source_runtime_active) {
#ifdef CHIBI_TEST_SOURCE_FEATURES
        if (shader_parameters) {
#ifdef CHIBI_TEST_RENDER_RUNTIME
            test_particle_fields();
#endif
        } else if (system_schedule) {
            test_tool_scheduler();
        } else if (scheduler_benchmark) {
            test_tool_scheduler_benchmark();
        } else if (source_systems) {
        } else if (source_query_rows) {
        } else
#endif
            if (new_asset) {
        } else if (asset_refactor) {
        } else if (persistent_store) {
            test_persistent_stores();
        } else if (whole_math) {
            test_compiled_math();
        } else if (whole_integer) {
            test_compiled_integers();
        } else if (whole_conversion) {
            test_compiled_conversions();
        } else if (whole_select) {
            test_compiled_selects();
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
            case MATERIAL_SIMULATION:
                test_material_simulation();
                test_material_mixed();
                test_material_obstacles();
                test_material_particle_index();
                break;
            case MATERIAL_ECS: break;
            case MATERIAL_BENCHMARK: test_material_benchmark(); break;
        }
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
#ifdef CHIBI_TEST_RENDER_RUNTIME
    if (render_resources || interior_border || viewport_resize || radiance ||
        radiance_depth || material_sampling || scene2d || terrain_seams ||
        material_render) {
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
        } else if (terrain_seams) {
            test_render_material_terrain_seams();
        } else if (scene2d) {
            test_render_scene2d();
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
    if (argc == 2 && test_graph_dispatch(argv[1])) {
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
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
    if (webgpu_staging) {
        test_tool_webgpu_staging();
    } else if (wasm_browser) {
        test_tool_wasm_browser();
    } else if (third_party_publish) {
        test_tool_third_party_publish();
    } else if (collab) {
        test_tool_collab();
    } else if (package) {
        test_tool_package();
    } else if (scene_prepared) {
        test_scene_prepared();
    } else if (scene) {
        test_tool_scene();
    } else if (unit) {
        test_tool_execution_policy();
    } else if (test_runner) {
        test_tool_test_policy();
    } else if (validation_plan) {
        test_tool_validation_plan();
    } else if (web_storage) {
        test_tool_web_storage();
    } else if (query_benchmark) {
        test_tool_ecs_query_benchmark();
    } else if (types) {
        test_tool_types();
    } else if (arena) {
        test_arena();
    } else if (types) {
        test_tool_types();
    } else {
        test_tool_execution_policy();
        test_tool_types();
        test_tool_ecs();
        test_tool_ecs_relationships();
        test_tool_ecs_hierarchy();
        test_tool_ecs_world();
        test_tool_ecs_query();
        test_tool_package();
        test_tool_third_party_publish();
        test_tool_collab();
        test_tool_test_policy();
        test_tool_validation_plan();
#ifdef CHIBI_TEST_AUDIO_ANALYSIS
        test_tool_audio_analysis();
#ifdef CHIBI_TEST_RENDER_RUNTIME
        test_tool_audio_output();
#endif
#endif
#ifdef CHIBI_TEST_REFLECT
        test_tool_reflect();
#endif
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

#endif

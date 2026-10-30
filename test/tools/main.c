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
#else
void test_tool_execution_policy();
void test_tool_chibi_cli(const char *binary, const char *self);
void test_tool_chibi_cli_process(const char *binary, const char *self);
void test_tool_chibi_cli_build(const char *binary);
void test_tool_chibi_cli_doctor(const char *binary);
void test_tool_chibi_cli_installed(const char *binary, const char *self);
void test_native_build(const char *binary);
void test_native_workload_benchmark(const char *binary);
void test_tool_chibi_cli_export(const char *binary);
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
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
void test_tool_collab();
void test_tool_web_storage();
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
#ifdef CHIBI_TEST_BOX3D
void test_box3d();
#endif
#ifdef CHIBI_TEST_REFLECT
void test_tool_reflect();
void test_tool_reflect_ranges();
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_native_values();
#endif

#ifdef CHIBI_TEST_ASSET_VALUES
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

/* Selectors of documents, files and runtime pieces with one suite each. */
static const char *const document_selectors[] = {
    "--asset-values",    "--ui-document",
    "--text-files",      "--fmt",
    "--reference-index", "--asset-source",
    "--behavior-loader", "--engine-clock",
    "--preview-layout",  "--fields",
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
#ifdef CHIBI_TEST_ASSET_VALUES
    if (selected(argc, argv, "--asset-values")) {
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
    bool collab = argc == 2 && !strcmp(argv[1], "--collab");
    bool unit = argc == 2 && !strcmp(argv[1], "--unit");
    bool wasm_browser = argc == 2 && !strcmp(argv[1], "--wasm-browser");
    bool web_storage = argc == 2 && !strcmp(argv[1], "--web-storage");
    bool query_benchmark = argc == 2 && !strcmp(argv[1], "--ecs-query-benchmark");
    bool arena = argc == 2 && !strcmp(argv[1], "--arena");
    bool types = argc == 2 && !strcmp(argv[1], "--types");
    bool scene = argc == 2 && !strcmp(argv[1], "--scene");
#ifdef CHIBI_TEST_SCENE_SNAPSHOT_READ
#endif
    bool native_values = false, box3d = false, mesh_files = false,
         model_import = false, model_graphs = false, model_catalog = false,
         shader_graphs = false;
    bool whole_math = false, whole_integer = false, whole_conversion = false,
         whole_select = false, persistent_store = false;
    bool system_schedule = false, scheduler_benchmark = false,

         source_systems = false, source_query_rows = false, new_asset = false,
         asset_refactor = false;
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
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_values = argc == 2 && !strcmp(argv[1], "--native-values");
#endif
    if (argc != 1 && !shader_parameters && !new_asset && !asset_refactor &&
        !collab && !unit && !web_storage && !wasm_browser && !query_benchmark &&
        !scene && !native_values && !arena && !types && !box3d && !mesh_files &&
        !model_import && !model_graphs && !model_catalog && !shader_graphs &&
        !persistent_store && !whole_math && !whole_integer && !whole_conversion &&
        !whole_select && !system_schedule && !scheduler_benchmark &&
        !source_systems && !source_query_rows && !graph_document &&
        !graph_presentation && !graph_functions && !vector_nodes) {
        fprintf(
            stderr,
            "Usage: %s [--collab | "
            "--unit | "
            "--validation-plan | "
            "--reflect-ranges | "
            "--ecs-query | --ecs-strings | "
            "| "
            "--web-storage | "
            "--wasm-browser | "
            "--ecs-query-benchmark | --arena | --types | "
            ""
            "--scene | "
            ""
            "--native-values "
            "| "
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            "--box3d | "
            "--sky-files | --mesh-files | --model-import | "
            "--model-graphs | --model-catalog | --shader-graphs | "
            ""
            ""
            ""
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
            "--ui-document | --text-files | --fmt | "
            ""
            ""
            ""
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
#ifdef CHIBI_TEST_BOX3D
    if (box3d) {
        test_box3d();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
    if (argc == 2 && test_graph_dispatch(argv[1])) {
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    if (native_values) {
        test_tool_native_values();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
#endif
    if (wasm_browser) {
        test_tool_wasm_browser();
    } else if (collab) {
        test_tool_collab();
    } else if (scene) {
        test_tool_scene();
    } else if (unit) {
        test_tool_execution_policy();
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
        test_tool_collab();
#ifdef CHIBI_TEST_REFLECT
        test_tool_reflect();
#endif
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

#endif

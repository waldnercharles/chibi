#if !defined(__EMSCRIPTEN__) && !defined(CHIBI_TEST_PROGRAM_ONLY)
#define pu_run_test tool_pico_run_test
#endif
#define PICO_UNIT_IMPLEMENTATION
#ifndef CHIBI_TEST_PROGRAM_ONLY
#include "test/tools/fixture.h"
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
void test_tool_chibi_cli(const char *binary, const char *self);
void test_tool_chibi_cli_process(const char *binary, const char *self);
void test_tool_chibi_cli_build(const char *binary);
void test_tool_chibi_cli_doctor(const char *binary);
void test_tool_chibi_cli_installed(const char *binary, const char *self);
void test_native_workload_benchmark(const char *binary);
void test_tool_chibi_cli_export(const char *binary);
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
void test_tool_compiler_bootstrap(const char *host);
void test_tool_bootstrap_recovery(const char *host, const char *artifact);
void test_tool_bootstrap_load(const char *artifact);
#endif
#ifdef CHIBI_TEST_BUILD_PROCESS
void test_tool_build_process(const char *worker, const char *host);
void test_tool_build_process_files(const char *worker, const char *host);
void test_tool_build_timings(const char *worker, const char *host);
#endif
void test_tool_collab();
void test_tool_web_storage();
void test_tool_wasm_browser();
void test_tool_types();
void test_arena();
#ifdef CHIBI_TEST_PROGRAM
void test_program_sdk();
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
void test_compiled_math();
void test_compiled_integers();
void test_compiled_selects();
void test_persistent_stores();
static bool source_runtime_active;
bool test_source_runtime_active()
{
    return source_runtime_active;
}
#endif
#ifdef CHIBI_TEST_BOX3D
void test_box3d();
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_native_values();
#endif

#ifdef CHIBI_TEST_ASSET_VALUES
void test_asset_value();
#endif
#ifdef CHIBI_TEST_FMT
void test_fmt();
#endif

/* Selectors of documents, files and runtime pieces with one suite each. */
static const char *const document_selectors[] = { "--asset-values", "--fmt" };

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
#ifdef CHIBI_TEST_FMT
    if (selected(argc, argv, "--fmt")) {
        test_fmt();
        return true;
    }
#endif
    return false;
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
    if (argc == 3 && !strcmp(argv[1], "--compiler-bootstrap")) {
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
        tool_init();
        pu_display_time(true);
        test_tool_compiler_bootstrap(argv[2]);
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
    if (argc == 3 && !strcmp(argv[1], "--bootstrap-load")) {
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
        tool_init();
        pu_display_time(true);
        test_tool_bootstrap_load(argv[2]);
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
    bool collab = argc == 2 && !strcmp(argv[1], "--collab");
    bool wasm_browser = argc == 2 && !strcmp(argv[1], "--wasm-browser");
    bool web_storage = argc == 2 && !strcmp(argv[1], "--web-storage");
    bool arena = argc == 2 && !strcmp(argv[1], "--arena");
    bool types = argc == 2 && !strcmp(argv[1], "--types");
    bool native_values = false, box3d = false;
    bool whole_math = false, whole_integer = false, whole_select = false,
         persistent_store = false;
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    whole_math = argc == 2 && !strcmp(argv[1], "--whole-math");
    whole_integer = argc == 2 && !strcmp(argv[1], "--whole-integer");
    persistent_store = argc == 2 && !strcmp(argv[1], "--persistent-store");
    whole_select = argc == 2 && !strcmp(argv[1], "--whole-select");
#endif
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    native_values = argc == 2 && !strcmp(argv[1], "--native-values");
#endif
    if (argc != 1 && !collab && !web_storage && !wasm_browser &&
        !native_values && !arena && !types && !box3d && !persistent_store &&
        !whole_math && !whole_integer && !whole_select) {
        fprintf(
            stderr,
            "Usage: %s [--collab | --web-storage | "
            "--wasm-browser | --arena | --types | "
            "--native-values | --box3d | --persistent-store | --whole-math | "
            "--whole-integer | --whole-select | "
            "--asset-values | --fmt]\n",
            argv[0]
        );
        return 2;
    }
    tool_init();
    pu_display_time(true);
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    source_runtime_active = persistent_store || whole_math || whole_integer ||
                            whole_select;
    if (source_runtime_active) {
        if (persistent_store) {
            test_persistent_stores();
        } else if (whole_math) {
            test_compiled_math();
        } else if (whole_integer) {
            test_compiled_integers();
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
    } else if (web_storage) {
        test_tool_web_storage();
    } else if (types) {
        test_tool_types();
    } else if (arena) {
        test_arena();
    } else if (types) {
        test_tool_types();
    } else {
        test_tool_types();
        test_tool_collab();
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

#endif

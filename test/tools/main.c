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
#ifdef CHIBI_TEST_COMPILER_ARTIFACT
void test_tool_compiler_bootstrap(const char *host);
void test_tool_bootstrap_recovery(const char *host, const char *artifact);
void test_tool_bootstrap_load(const char *artifact);
#endif
void test_tool_web_storage();
void test_tool_types();
#ifdef CHIBI_TEST_PROGRAM
void test_program_sdk();
#endif
#ifdef CHIBI_TEST_SOURCE_RUNTIME
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
void test_tool_case_timings(const char *self);
int tool_case_timing_probe(const char *mode);

int main(int argc, char **argv)
{
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
    bool web_storage = argc == 2 && !strcmp(argv[1], "--web-storage");
    bool types = argc == 2 && !strcmp(argv[1], "--types");
    bool box3d = false;
    bool persistent_store = false;
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    persistent_store = argc == 2 && !strcmp(argv[1], "--persistent-store");
#endif
#ifdef CHIBI_TEST_BOX3D
    box3d = argc == 2 && !strcmp(argv[1], "--box3d");
#endif
    if (argc != 1 && !web_storage && !types && !box3d && !persistent_store) {
        fprintf(
            stderr,
            "Usage: %s [--web-storage | --types | --box3d | "
            "--persistent-store]\n",
            argv[0]
        );
        return 2;
    }
    tool_init();
    pu_display_time(true);
#ifdef CHIBI_TEST_SOURCE_RUNTIME
    source_runtime_active = persistent_store;
    if (source_runtime_active) {
        test_persistent_stores();
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
    if (web_storage) {
        test_tool_web_storage();
    } else {
        test_tool_types();
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

#endif

#define PICO_UNIT_IMPLEMENTATION
#include "package_compiler_chibi.h"
#include "test/parallel.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

void test_language_parallel();
void test_language_record_values();
void test_language_record_document();
void test_language_record_compatibility();
void test_language_execution();
void test_language_execution_scope();
void test_language_program_session();
void test_language_source_systems();
void test_language_capture();
void test_numeric_ranges();
#ifdef CHIBI_TEST_ASSET_COMPILER_HOST
void test_language_asset_compiler_host();
#endif
void test_language_host_packages_compiler();
void test_language_component_write_ir();
void test_language_component_write_layout();
void test_language_text_format();
void test_language_native_census(const char *report);
void test_language_native_census_flattening();

int main(int argc, char **argv)
{
    if (!test_parallel_init()) {
        return 2;
    }
    if (argc == 2 && !strcmp(argv[1], "--test-parallel")) {
        test_language_parallel();
        test_parallel_finish();
        pu_print_stats();
        return pu_test_failed() ? 1 : 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--package-compiler-chibi-native-call")) {
        return test_language_package_compiler_chibi_native_call(argc - 2, argv + 2);
    }
    if (argc == 3 && !strcmp(argv[1], "--stack-overflow-child")) {
        return test_language_stack_overflow_child(argv[2]);
    }
    bool execution = argc == 2 && !strcmp(argv[1], "--execution");
    bool execution_scope = argc == 2 && !strcmp(argv[1], "--execution-scope");
    bool record_compatibility = argc == 2 &&
                                !strcmp(argv[1], "--record-compatibility");
    bool record_document = argc == 2 && !strcmp(argv[1], "--record-document");
    bool record_values = argc == 2 && !strcmp(argv[1], "--record-values");
    bool program = argc == 2 && !strcmp(argv[1], "--program");
    bool program_session = argc == 2 && !strcmp(argv[1], "--program-session");
    bool source_systems = argc == 2 && !strcmp(argv[1], "--source-systems");
    bool ranges = argc == 2 && !strcmp(argv[1], "--ranges");
    bool integer_shift_counts = (argc == 2 || argc == 3) &&
                                !strcmp(argv[1], "--integer-shift-counts");
    bool asset_compiler_host = argc == 2 && !strcmp(argv[1], "--asset-compiler-host");
    bool package_compiler_chibi_scalar = argc == 2 &&
                                         !strcmp(argv[1], "--package-compiler-chibi-scalar");
    bool package_compiler_chibi_native = argc == 2 &&
                                         !strcmp(argv[1], "--package-compiler-chibi-native");
    bool native_census = (argc == 2 || argc == 3) &&
                         !strcmp(argv[1], "--native-census");
    bool native_census_flattening = argc == 2 &&
                                    !strcmp(argv[1], "--native-census-flattening");
    bool host_packages_compiler = argc == 2 &&
                                  !strcmp(argv[1], "--host-packages-compiler");
    bool component_write = argc == 2 && !strcmp(argv[1], "--component-write");
    bool component_write_layout = argc == 2 &&
                                  !strcmp(argv[1], "--component-write-layout");
    bool text_format = argc == 2 && !strcmp(argv[1], "--text-format");
    if ((!execution && !execution_scope && !record_values && !record_document &&
         !record_compatibility && !program && !program_session && !source_systems &&
         !ranges && !integer_shift_counts && !asset_compiler_host &&
         !package_compiler_chibi_scalar && !package_compiler_chibi_native &&
         !native_census && !native_census_flattening && !host_packages_compiler &&
         !component_write && !component_write_layout && !text_format)) {
        fprintf(
            stderr,
            "Usage: %s --test-parallel | "
            "--execution | "
            "--execution-scope  "
            "  "
            "| --program | --program-session | --source-systems | "
            ""
            ""
            ""
            ""
            ""
            "--ranges | "
            ""
            "--integer-shift-counts [case] | "
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            "--package-compiler-chibi-types -imports "
            "-fallible "
            "-math-native "
            "| "
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            "--package-compiler-chibi-scalar | "
            "--package-compiler-chibi-native | --native-census [report] | "
            "--native-census-flattening | "
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            ""
            "--package-imports -compiler | "
            ""
            ""
            ""
            ""
            "--component-write | --component-write-layout | "
            ""
            ""
            "--activation-cache "
            "| "
            "--binding-benchmark "
            "| "
            ""
            ""
            ""
            ""
            "--text-format | "
            ""
            "--record-values | "
            "--record-document | "
            "--record-compatibility\n",
            argv[0]
        );
        return 2;
    }
    pu_display_time(true);
    if (record_compatibility) {
        test_language_record_compatibility();
    } else if (record_document) {
        test_language_record_document();
    } else if (record_values) {
        test_language_record_values();
    } else if (text_format) {
        test_language_text_format();
    } else if (package_compiler_chibi_scalar) {
        test_language_package_compiler_chibi_scalar();
    } else if (package_compiler_chibi_native) {
        test_language_package_compiler_chibi_native();
    } else if (native_census) {
        test_language_native_census(argc == 3 ? argv[2] : NULL);
    } else if (native_census_flattening) {
        test_language_native_census_flattening();
    } else if (host_packages_compiler) {
        test_language_host_packages_compiler();
    } else if (asset_compiler_host) {
#ifdef CHIBI_TEST_ASSET_COMPILER_HOST
        test_language_asset_compiler_host();
#else
        fprintf(stderr, "asset compiler host component unavailable on this platform/configuration\n");
        return 1;
#endif
    } else if (ranges) {
        test_numeric_ranges();
    } else if (component_write_layout) {
        test_language_component_write_layout();
    } else if (component_write) {
        test_language_component_write_ir();
    } else if (program) {
        test_language_capture();
    } else if (program_session) {
        test_language_program_session();
    } else if (source_systems) {
        test_language_source_systems();
    } else if (execution_scope) {
        test_language_execution_scope();
    } else if (execution) {
        test_language_execution();
    }
    test_parallel_finish();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

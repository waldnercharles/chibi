#define PICO_UNIT_IMPLEMENTATION
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
void test_language_capture();
void test_numeric_ranges();
void test_language_host_packages_compiler();
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
    bool execution = argc == 2 && !strcmp(argv[1], "--execution");
    bool execution_scope = argc == 2 && !strcmp(argv[1], "--execution-scope");
    bool record_compatibility = argc == 2 &&
                                !strcmp(argv[1], "--record-compatibility");
    bool record_document = argc == 2 && !strcmp(argv[1], "--record-document");
    bool record_values = argc == 2 && !strcmp(argv[1], "--record-values");
    bool program = argc == 2 && !strcmp(argv[1], "--program");
    bool ranges = argc == 2 && !strcmp(argv[1], "--ranges");
    bool host_packages_compiler = argc == 2 &&
                                  !strcmp(argv[1], "--host-packages-compiler");
    if ((!execution && !execution_scope && !record_values && !record_document &&
         !record_compatibility && !program && !ranges && !host_packages_compiler)) {
        fprintf(
            stderr,
            "Usage: %s --test-parallel | --execution | --execution-scope | "
            "--program | "
            "--ranges | "
            "--host-packages-compiler | "
            "--record-values | --record-document | --record-compatibility\n",
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
    } else if (host_packages_compiler) {
        test_language_host_packages_compiler();
    } else if (ranges) {
        test_numeric_ranges();
    } else if (program) {
        test_language_capture();
    } else if (execution_scope) {
        test_language_execution_scope();
    } else if (execution) {
        test_language_execution();
    }
    test_parallel_finish();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

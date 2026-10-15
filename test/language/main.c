#define PICO_UNIT_IMPLEMENTATION
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

void test_language_syntax();
void test_language_corpus(const char *manifest);
void test_language_execution();
void test_language_wasm();
void test_language_semantic();
void test_language_program();
void test_language_capture();
void test_language_query_program();
void test_language_query_boundary();
void test_language_incremental();
void test_language_modules();
void test_numeric_ranges();
void test_language_scalarize();
void test_language_component_write();
void test_language_component_write_source();
void test_language_component_write_ir();
void test_language_component_write_layout();
void test_language_benchmark();
void test_language_activation_benchmark();
void test_language_binding_benchmark();
void test_language_borrow_benchmark();
void test_language_incremental_benchmark();
void test_language_borrow();
void test_language_reductions();
void test_language_binding_schedule();
void test_language_activation_cache();
void test_language_headless();
void test_language_format();
void test_language_navigation();
void test_language_publication();
void test_field_runtime();
void test_prototype_document();
void test_ui_authoring();
void test_language_text_format();
void test_graph_model();
void test_graph_authoring();
void test_model_runtime();
void test_language_format_corpus(const char *manifest);
void test_language_prepared_write(const char *directory);

int main(int argc, char **argv)
{
    bool format = (argc == 2 || argc == 4) && !strcmp(argv[1], "--format");
    bool prepared = argc == 3 && !strcmp(argv[1], "--prepared-write");
    bool corpus = argc == 4 && !strcmp(argv[2], "--corpus");
    bool semantic = argc == 2 && !strcmp(argv[1], "--semantic");
    bool execution = argc == 2 && !strcmp(argv[1], "--execution");
    bool benchmark = argc == 2 && !strcmp(argv[1], "--script-benchmark");
    bool activation_benchmark = argc == 2 &&
                                !strcmp(argv[1], "--activation-benchmark");
    bool binding_benchmark = argc == 2 && !strcmp(argv[1], "--binding-benchmark");
    bool borrow_benchmark = argc == 2 && !strcmp(argv[1], "--borrow-benchmark");
    bool incremental_benchmark = argc == 2 &&
                                 !strcmp(argv[1], "--incremental-benchmark");
    bool wasm = argc == 2 && !strcmp(argv[1], "--wasm");
    bool program = argc == 2 && !strcmp(argv[1], "--program");
    bool query_program = argc == 2 && !strcmp(argv[1], "--query-program");
    bool incremental = argc == 2 && !strcmp(argv[1], "--incremental");
    bool modules = argc == 2 && !strcmp(argv[1], "--modules");
    bool ranges = argc == 2 && !strcmp(argv[1], "--ranges");
    bool component_write = argc == 2 && !strcmp(argv[1], "--component-write");
    bool component_write_layout = argc == 2 &&
                                  !strcmp(argv[1], "--component-write-layout");
    bool scalarize = argc == 2 && !strcmp(argv[1], "--scalarize");
    bool reductions = argc == 2 && !strcmp(argv[1], "--reductions");
    bool borrow = argc == 2 && !strcmp(argv[1], "--borrow");
    bool binding_schedule = argc == 2 && !strcmp(argv[1], "--binding-schedule");
    bool activation_cache = argc == 2 && !strcmp(argv[1], "--activation-cache");
    bool headless = argc == 2 && !strcmp(argv[1], "--headless");
    bool navigation = argc == 2 && !strcmp(argv[1], "--navigation");
    bool publication = argc == 2 && !strcmp(argv[1], "--publication");
    bool field_runtime = argc == 2 && !strcmp(argv[1], "--field-runtime");
    bool prototype = argc == 2 && !strcmp(argv[1], "--prototype-document");
    bool model_runtime = argc == 2 && !strcmp(argv[1], "--model-runtime");
    bool graph_authoring = argc == 2 && !strcmp(argv[1], "--graph-authoring");
    bool graph_model = argc == 2 && !strcmp(argv[1], "--graph-model");
    bool text_format = argc == 2 && !strcmp(argv[1], "--text-format");
    bool ui_authoring = argc == 2 && !strcmp(argv[1], "--ui-authoring");
    if ((format && argc == 4 && !corpus) ||
        (!format && !prepared && !execution && !semantic && !benchmark &&
         !activation_benchmark && !binding_benchmark && !borrow_benchmark &&
         !incremental_benchmark && !wasm && !program && !query_program &&
         !incremental && !modules && !ranges && !component_write &&
         !component_write_layout && !scalarize && !borrow && !reductions &&
         !binding_schedule && !activation_cache && !headless && !navigation &&
         !publication && !field_runtime && !prototype && !ui_authoring &&
         !text_format && !graph_model && !graph_authoring && !model_runtime &&
         ((argc != 2 && !corpus) || strcmp(argv[1], "--syntax")))) {
        fprintf(
            stderr,
            "Usage: %s --syntax [--corpus manifest] | --execution | --semantic "
            "| --program | --query-program | --incremental | --modules | "
            "--ranges | "
            "--component-write | --component-write-layout | --scalarize | "
            "--borrow | --reductions | "
            "--binding-schedule | "
            "--activation-cache "
            "| "
            "--script-benchmark | --activation-benchmark | --binding-benchmark "
            "| "
            "--borrow-benchmark | --incremental-benchmark | --wasm | "
            "--headless | "
            "--navigation | "
            "--publication | "
            "--field-runtime | --prototype-document | --ui-authoring | "
            "--text-format | --graph-model | --graph-authoring | "
            "--model-runtime | "
            "--format [--corpus manifest] | --prepared-write "
            "directory\n",
            argv[0]
        );
        return 2;
    }
    pu_display_time(true);
    if (wasm) {
        test_language_wasm();
    } else if (benchmark) {
        test_language_benchmark();
    } else if (activation_benchmark) {
        test_language_activation_benchmark();
    } else if (binding_benchmark) {
        test_language_binding_benchmark();
    } else if (borrow_benchmark) {
        test_language_borrow_benchmark();
    } else if (incremental_benchmark) {
        test_language_incremental_benchmark();
    } else if (reductions) {
        test_language_reductions();
    } else if (borrow) {
        test_language_borrow();
    } else if (binding_schedule) {
        test_language_binding_schedule();
    } else if (activation_cache) {
        test_language_activation_cache();
    } else if (model_runtime) {
        test_model_runtime();
    } else if (graph_authoring) {
        test_graph_authoring();
    } else if (graph_model) {
        test_graph_model();
    } else if (text_format) {
        test_language_text_format();
    } else if (ui_authoring) {
        test_ui_authoring();
    } else if (prototype) {
        test_prototype_document();
    } else if (field_runtime) {
        test_field_runtime();
    } else if (navigation) {
        test_language_navigation();
    } else if (publication) {
        test_language_publication();
    } else if (format) {
        test_language_format();
        if (corpus) {
            test_language_format_corpus(argv[3]);
        }
    } else if (prepared) {
        test_language_prepared_write(argv[2]);
    } else if (headless) {
        test_language_headless();
    } else if (query_program) {
        test_language_query_program();
        test_language_query_boundary();
    } else if (modules) {
        test_language_modules();
    } else if (incremental) {
        test_language_incremental();
    } else if (ranges) {
        test_numeric_ranges();
    } else if (component_write_layout) {
        test_language_component_write_layout();
    } else if (component_write) {
        test_language_component_write();
        test_language_component_write_source();
        test_language_component_write_ir();
    } else if (scalarize) {
        test_language_scalarize();
    } else if (program) {
        test_language_program();
        test_language_capture();
        test_language_query_boundary();
    } else if (semantic) {
        test_language_semantic();
    } else if (execution) {
        test_language_execution();
    } else {
        test_language_syntax();
        if (corpus) {
            test_language_corpus(argv[3]);
        }
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

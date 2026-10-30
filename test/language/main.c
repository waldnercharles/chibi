#define PICO_UNIT_IMPLEMENTATION
#include "bounded_compiler_chibi.h"
#include "compiler_fixture.h"
#include "compositional_records.h"
#include "package_compiler_chibi.h"
#include "prepared_writer_chibi.h"
#include "test/parallel.h"
#include "third_party/pico_headers/pico_unit.h"
#include "type_relations_chibi.h"

#include <stdio.h>
#include <string.h>

void test_language_syntax();
void test_language_parallel();
void test_language_inferred_string_arrays(bool repro);
void test_language_outliner_projection();
void test_language_row_array();
void test_language_packed();
void test_language_packed_benchmark();
void test_language_runtime_attribution();
void test_language_runtime_attribution_benchmark();
void test_language_query_binding_benchmark();
void test_language_scalar_read_benchmark();
void test_language_record_values();
void test_language_record_publication(bool repro);
void test_language_record_document();
void test_language_record_state();
void test_language_record_compatibility();
#ifdef CHIBI_TEST_RECORD_SERVICES
void test_language_record_services();
void test_language_record_global_compatibility();
#endif
void test_language_record_semantic();
void test_language_record_runtime();
void test_language_record_prepared_runtime();
void test_language_corpus(const char *manifest);
void test_language_execution();
void test_language_execution_scope();
void test_language_wasm();
void test_language_semantic();
void test_language_effects();
void test_language_maps();
void test_language_iteration();
void test_language_generic();
void test_language_source_assets();
void test_language_symbol_references();
void test_language_program();
void test_language_program_session();
void test_language_native_char_results();
void test_language_retention();
void test_language_capture();
void test_language_query_program();
void test_language_query_values();
void test_language_query_boundary();
void test_language_incremental();
void test_language_operations();
void test_language_modules();
void test_language_native_calls();
void test_numeric_ranges();
void test_numeric_exact_core();
void test_language_integer_semantics();
void test_language_integer_arithmetic();
void test_language_integer_shift_counts(const char *name);
void test_language_integer_shift_counts_authored();
void test_language_package_graph();
void test_language_package_graph_chibi();
void test_language_shader_source_chibi();
void test_language_asset_packaging_chibi();
#ifdef CHIBI_TEST_ASSET_COMPILER_HOST
void test_language_asset_compiler_host();
#endif
void test_language_syntax_chibi();
void test_language_source_tokenizer_chibi();
void test_language_source_parser_chibi();
void test_language_package_syntax();
void test_language_package_syntax_chibi();
void test_language_package_paths_chibi();
void test_language_package_routes_chibi();
void test_language_package_snapshot_chibi();
void test_language_scalar_lowering_chibi();
void test_language_unit_algebra_chibi();
void test_language_signature_reader_chibi();
void test_language_snapshot_reader_chibi();
void test_language_package_execution();
void test_language_package_resolution();
void test_language_package_imports();
void test_language_host_packages();
void test_language_host_packages_compiler();
void test_language_package_files();
void test_language_package_core();
void test_language_package_math();
void test_language_package_math_benchmark();
void test_language_package_portable();
void test_language_package_portable_compiler();
void test_language_package_tests();
void test_language_argument_effects_chibi();
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
void test_language_build_workflow();
void test_language_navigation();
void test_language_publication();
void test_field_runtime();
void test_prototype_document();
void test_ui_authoring();
void test_language_text_format();
void test_language_prepared_write(const char *directory);
void test_language_compiler_artifact_write(const char *directory);
void test_language_record_producer(const char *directory);
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
    bool record_producer = argc == 3 && !strcmp(argv[1], "--record-produce");
    bool prepared = argc == 3 && !strcmp(argv[1], "--prepared-write");
    bool compiler_artifact = argc == 3 &&
                             !strcmp(argv[1], "--compiler-artifact-write");
    bool corpus = argc == 4 && !strcmp(argv[2], "--corpus");
    bool semantic = argc == 2 && !strcmp(argv[1], "--semantic");
    bool inferred_arrays = argc == 2 && !strcmp(argv[1], "--inferred-string-arrays");
    bool inferred_array_repro = argc == 2 &&
                                !strcmp(argv[1], "--inferred-string-array-repro");
    bool execution = argc == 2 && !strcmp(argv[1], "--execution");
    bool execution_scope = argc == 2 && !strcmp(argv[1], "--execution-scope");
    bool benchmark = argc == 2 && !strcmp(argv[1], "--script-benchmark");
    bool activation_benchmark = argc == 2 &&
                                !strcmp(argv[1], "--activation-benchmark");
    bool binding_benchmark = argc == 2 && !strcmp(argv[1], "--binding-benchmark");
    bool borrow_benchmark = argc == 2 && !strcmp(argv[1], "--borrow-benchmark");
    bool incremental_benchmark = argc == 2 &&
                                 !strcmp(argv[1], "--incremental-benchmark");
    bool record_prepared_runtime = argc == 2 && !strcmp(argv[1], "--record-prepared");
    bool outliner_projection = argc == 2 && !strcmp(argv[1], "--outliner-projection");
    bool row_array = argc == 2 && !strcmp(argv[1], "--row-array");
    bool record_runtime = argc == 2 && !strcmp(argv[1], "--record-runtime");
    bool compositional_records = argc == 2 &&
                                 !strcmp(argv[1], "--compositional-records");
    bool record_semantic = argc == 2 && !strcmp(argv[1], "--record-semantic");
    bool record_compatibility = argc == 2 &&
                                !strcmp(argv[1], "--record-compatibility");
    bool record_state = argc == 2 && !strcmp(argv[1], "--record-state");
    bool record_document = argc == 2 && !strcmp(argv[1], "--record-document");
    bool record_services = argc == 2 && !strcmp(argv[1], "--record-services");
    bool record_values = argc == 2 && !strcmp(argv[1], "--record-values");
    bool record_publication = argc == 2 && !strcmp(argv[1], "--record-publication");
    bool record_publication_repro = argc == 2 &&
                                    !strcmp(argv[1], "--record-publication-repro");
    bool packed_benchmark = argc == 2 && !strcmp(argv[1], "--packed-benchmark");
    bool query_binding_benchmark = argc == 2 &&
                                   !strcmp(argv[1], "--query-binding-benchmark");
    bool scalar_read_benchmark = argc == 2 &&
                                 !strcmp(argv[1], "--scalar-read-benchmark");
    bool runtime_attribution = argc == 2 && !strcmp(argv[1], "--runtime-attribution");
    bool runtime_attribution_benchmark = argc == 2 &&
                                         !strcmp(argv[1], "--runtime-attribution-benchmark");
    bool wasm = argc == 2 && !strcmp(argv[1], "--wasm");
    bool native_char_results = argc == 2 && !strcmp(argv[1], "--native-char-results");
    bool program = argc == 2 && !strcmp(argv[1], "--program");
    bool program_session = argc == 2 && !strcmp(argv[1], "--program-session");
    bool query_program = argc == 2 && !strcmp(argv[1], "--query-program");
    bool query_values = argc == 2 && !strcmp(argv[1], "--query-values");
    bool incremental = argc == 2 && !strcmp(argv[1], "--incremental");
    bool operations = argc == 2 && !strcmp(argv[1], "--operations");
    bool modules = argc == 2 && !strcmp(argv[1], "--modules");
    bool native_calls = argc == 2 && !strcmp(argv[1], "--native-calls");
    bool ranges = argc == 2 && !strcmp(argv[1], "--ranges");
    bool numeric_exact_core = argc == 2 && !strcmp(argv[1], "--numeric-exact-core");
    bool integer_shift_counts = (argc == 2 || argc == 3) &&
                                !strcmp(argv[1], "--integer-shift-counts");
    bool integer_semantics = argc == 2 && !strcmp(argv[1], "--integer-semantics");
    bool integer_arithmetic = argc == 2 && !strcmp(argv[1], "--integer-arithmetic");
    bool integer_shift_counts_authored = argc == 2 &&
                                         !strcmp(argv[1], "--integer-shift-counts-authored");
    bool package_graph_chibi = argc == 2 && !strcmp(argv[1], "--package-graph-chibi");
    bool asset_compiler_host = argc == 2 && !strcmp(argv[1], "--asset-compiler-host");
    bool asset_packaging_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--asset-packaging-chibi");
    bool shader_source_chibi = argc == 2 && !strcmp(argv[1], "--shader-source-chibi");
    bool syntax_chibi = argc == 2 && !strcmp(argv[1], "--syntax-chibi");
    bool source_tokenizer_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--source-tokenizer-chibi");
    bool source_parser_chibi = argc == 2 && !strcmp(argv[1], "--source-parser-chibi");
    bool package_paths_chibi = argc == 2 && !strcmp(argv[1], "--package-paths-chibi");
    bool package_routes_chibi = argc == 2 &&
                                !strcmp(argv[1], "--package-routes-chibi");
    bool package_snapshot_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--package-snapshot-chibi");
    bool compiler_fixture = argc == 2 && !strcmp(argv[1], "--compiler-fixture");
    bool bounded_compiler_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--bounded-compiler-chibi");
    bool prepared_writer_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--prepared-writer-chibi");
    bool package_compiler_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--package-compiler-chibi");
    bool package_compiler_chibi_libraries = argc == 2 &&
                                            !strcmp(argv[1], "--package-compiler-chibi-libraries");
    bool package_compiler_chibi_types = argc == 2 &&
                                        !strcmp(argv[1], "--package-compiler-chibi-types");
    bool package_compiler_chibi_fallible = argc == 2 &&
                                           !strcmp(argv[1], "--package-compiler-chibi-fallible");
    bool package_compiler_chibi_math_native = argc == 2 &&
                                              !strcmp(argv[1], "--package-compiler-chibi-math-native");
    bool package_compiler_chibi_literals = argc == 2 &&
                                           !strcmp(argv[1], "--package-compiler-chibi-literals");
    bool package_compiler_chibi_behaviors = argc == 2 &&
                                            !strcmp(argv[1], "--package-compiler-chibi-behaviors");
    bool package_compiler_chibi_queries = argc == 2 &&
                                          !strcmp(argv[1], "--package-compiler-chibi-queries");
    bool package_compiler_chibi_actions = argc == 2 &&
                                          !strcmp(argv[1], "--package-compiler-chibi-actions");
    bool package_compiler_chibi_sharing = argc == 2 &&
                                          !strcmp(argv[1], "--package-compiler-chibi-sharing");
    bool package_compiler_chibi_debug_free = argc == 2 &&
                                             !strcmp(argv[1], "--package-compiler-chibi-debug-free");
    bool package_compiler_chibi_scalar = argc == 2 &&
                                         !strcmp(argv[1], "--package-compiler-chibi-scalar");
    bool package_compiler_chibi_native_arguments = argc == 2 &&
                                                   !strcmp(argv[1], "--package-compiler-chibi-native-arguments");
    bool package_compiler_chibi_native_arguments_oracle = argc == 2 &&
                                                          !strcmp(argv[1], "--package-compiler-chibi-native-arguments-oracle");
    bool package_compiler_chibi_native = argc == 2 &&
                                         !strcmp(argv[1], "--package-compiler-chibi-native");
    bool native_census = (argc == 2 || argc == 3) &&
                         !strcmp(argv[1], "--native-census");
    bool native_census_flattening = argc == 2 &&
                                    !strcmp(argv[1], "--native-census-flattening");
    bool package_compiler_chibi_behaviors_rest = argc == 2 &&
                                                 !strcmp(argv[1], "--package-compiler-chibi-behaviors-rest");
    bool package_compiler_chibi_imports = argc == 2 &&
                                          !strcmp(argv[1], "--package-compiler-chibi-imports");
    bool package_compiler_chibi_compiled = argc == 2 &&
                                           !strcmp(argv[1], "--package-compiler-chibi-compiled");
    bool package_compiler_chibi_types_compiled = argc == 2 &&
                                                 !strcmp(argv[1], "--package-compiler-chibi-types-compiled");
    bool package_compiler_chibi_collections = argc == 2 &&
                                              !strcmp(argv[1], "--package-compiler-chibi-collections");
    bool package_compiler_chibi_collections_compiled = argc == 2 &&
                                                       !strcmp(argv[1], "--package-compiler-chibi-collections-compiled");
    bool package_compiler_chibi_imports_compiled = argc == 2 &&
                                                   !strcmp(argv[1], "--package-compiler-chibi-imports-compiled");
    bool package_compiler_chibi_bootstrap = argc == 2 &&
                                            !strcmp(argv[1], "--package-compiler-chibi-bootstrap");
    bool scalar_lowering_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--scalar-lowering-chibi");
    bool type_relations_chibi = argc == 2 &&
                                !strcmp(argv[1], "--type-relations-chibi");
    bool unit_algebra_chibi = argc == 2 && !strcmp(argv[1], "--unit-algebra-chibi");
    bool signature_reader_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--signature-reader-chibi");
    bool snapshot_reader_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--snapshot-reader-chibi");
    bool package_graph = argc == 2 && !strcmp(argv[1], "--package-graph");
    bool package_syntax = argc == 2 && !strcmp(argv[1], "--package-syntax");
    bool package_syntax_chibi = argc == 2 &&
                                !strcmp(argv[1], "--package-syntax-chibi");
    bool package_execution = argc == 2 && !strcmp(argv[1], "--package-execution");
    bool package_resolution = argc == 2 && !strcmp(argv[1], "--package-resolution");
    bool package_imports = argc == 2 && !strcmp(argv[1], "--package-imports");
    bool host_packages = argc == 2 && !strcmp(argv[1], "--host-packages");
    bool host_packages_compiler = argc == 2 &&
                                  !strcmp(argv[1], "--host-packages-compiler");
    bool package_core = argc == 2 && !strcmp(argv[1], "--package-core");
    bool package_math = argc == 2 && !strcmp(argv[1], "--package-math");
    bool package_math_benchmark = argc == 2 &&
                                  !strcmp(argv[1], "--package-math-benchmark");
    bool package_portable_compiler = argc == 2 &&
                                     !strcmp(argv[1], "--package-portable-compiler");
    bool package_portable = argc == 2 && !strcmp(argv[1], "--package-portable");
    bool package_tests = argc == 2 && !strcmp(argv[1], "--package-tests");
    bool argument_effects_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--argument-effects-chibi");
    bool package_files = argc == 2 && !strcmp(argv[1], "--package-files");
    bool component_write = argc == 2 && !strcmp(argv[1], "--component-write");
    bool component_write_layout = argc == 2 &&
                                  !strcmp(argv[1], "--component-write-layout");
    bool scalarize = argc == 2 && !strcmp(argv[1], "--scalarize");
    bool reductions = argc == 2 && !strcmp(argv[1], "--reductions");
    bool borrow = argc == 2 && !strcmp(argv[1], "--borrow");
    bool binding_schedule = argc == 2 && !strcmp(argv[1], "--binding-schedule");
    bool activation_cache = argc == 2 && !strcmp(argv[1], "--activation-cache");
    bool build_workflow = argc == 2 && !strcmp(argv[1], "--build-workflow");
    bool headless = argc == 2 && !strcmp(argv[1], "--headless");
    bool navigation = argc == 2 && !strcmp(argv[1], "--navigation");
    bool publication = argc == 2 && !strcmp(argv[1], "--publication");
    bool field_runtime = argc == 2 && !strcmp(argv[1], "--field-runtime");
    bool prototype = argc == 2 && !strcmp(argv[1], "--prototype-document");
    bool text_format = argc == 2 && !strcmp(argv[1], "--text-format");
    bool ui_authoring = argc == 2 && !strcmp(argv[1], "--ui-authoring");
    if ((!prepared && !compiler_artifact && !record_producer && !execution &&
         !execution_scope && !semantic && !inferred_arrays && !inferred_array_repro &&
         !benchmark && !activation_benchmark && !binding_benchmark &&
         !borrow_benchmark && !incremental_benchmark && !record_publication &&
         !record_publication_repro && !record_values && !record_document &&
         !record_state && !record_services && !record_compatibility &&
         !record_semantic && !record_prepared_runtime && !outliner_projection &&
         !row_array && !record_runtime && !compositional_records &&
         !runtime_attribution && !runtime_attribution_benchmark &&
         !query_binding_benchmark && !scalar_read_benchmark && !packed_benchmark &&
         !wasm && !native_char_results && !program && !program_session &&
         !query_program && !query_values && !incremental && !operations &&
         !modules && !native_calls && !ranges && !numeric_exact_core &&
         !integer_semantics && !integer_arithmetic && !integer_shift_counts &&
         !integer_shift_counts_authored && !package_graph && !package_graph_chibi &&
         !asset_compiler_host && !asset_packaging_chibi && !shader_source_chibi &&
         !syntax_chibi && !source_tokenizer_chibi && !source_parser_chibi &&
         !package_syntax && !package_syntax_chibi && !package_paths_chibi &&
         !package_routes_chibi && !package_snapshot_chibi && !scalar_lowering_chibi &&
         !compiler_fixture && !bounded_compiler_chibi && !prepared_writer_chibi &&
         !package_compiler_chibi && !package_compiler_chibi_libraries &&
         !package_compiler_chibi_types && !package_compiler_chibi_fallible &&
         !package_compiler_chibi_imports && !package_compiler_chibi_math_native &&
         !package_compiler_chibi_imports && !package_compiler_chibi_bootstrap &&
         !package_compiler_chibi_compiled && !package_compiler_chibi_types_compiled &&
         !package_compiler_chibi_collections && !package_compiler_chibi_collections_compiled &&
         !package_compiler_chibi_imports_compiled &&
         !package_compiler_chibi_literals && !package_compiler_chibi_behaviors &&
         !package_compiler_chibi_behaviors_rest && !package_compiler_chibi_queries &&
         !package_compiler_chibi_actions && !package_compiler_chibi_sharing &&
         !package_compiler_chibi_debug_free && !package_compiler_chibi_scalar &&
         !package_compiler_chibi_native && !native_census &&
         !native_census_flattening && !package_compiler_chibi_native_arguments &&
         !package_compiler_chibi_native_arguments_oracle && !type_relations_chibi &&
         !unit_algebra_chibi && !signature_reader_chibi && !snapshot_reader_chibi &&
         !package_execution && !package_resolution && !package_imports &&
         !host_packages && !host_packages_compiler && !package_files &&
         !package_core && !package_math && !package_math_benchmark &&
         !package_portable && !package_portable_compiler && !package_tests &&
         !argument_effects_chibi && !component_write && !component_write_layout &&
         !scalarize && !borrow && !reductions && !binding_schedule &&
         !activation_cache && !headless && !build_workflow && !navigation &&
         !publication && !field_runtime && !prototype && !ui_authoring &&
         !text_format && ((argc != 2 && !corpus) || strcmp(argv[1], "--syntax")))) {
        fprintf(
            stderr,
            "Usage: %s --test-parallel | --syntax [--corpus manifest] | "
            "--execution | "
            "--execution-scope | --semantic | --compositional-records "
            "| --inferred-string-arrays | --inferred-string-array-repro "
            "| --program | --program-session | "
            "--native-char-results | "
            "--query-program | --query-values | "
            "--incremental | "
            "--operations | "
            "--modules | --native-calls | "
            "--ranges | --numeric-exact-core | --integer-semantics | "
            "--integer-arithmetic | "
            "--integer-shift-counts [case] | --integer-shift-counts-authored | "
            "--package-graph | --package-graph-chibi | --shader-source-chibi | "
            "--asset-packaging-chibi | --syntax-chibi | "
            "--source-tokenizer-chibi | --source-parser-chibi | "
            "--package-paths-chibi | "
            "--package-routes-chibi | --package-snapshot-chibi | "
            ""
            "--scalar-lowering-chibi | --compiler-fixture | "
            "--bounded-compiler-chibi | "
            "--prepared-writer-chibi | --package-compiler-chibi | "
            "--package-compiler-chibi-types | --package-compiler-chibi-imports "
            "| --package-compiler-chibi-fallible "
            "| --package-compiler-chibi-math-native "
            "| "
            "--package-compiler-chibi-bootstrap | "
            "--package-compiler-chibi-compiled | "
            "--package-compiler-chibi-types-compiled | "
            "--package-compiler-chibi-collections | "
            "--package-compiler-chibi-collections-compiled | "
            "--package-compiler-chibi-imports-compiled | "
            "--package-compiler-chibi-literals | "
            "--package-compiler-chibi-behaviors | "
            "--package-compiler-chibi-actions | "
            "--package-compiler-chibi-behaviors-rest | "
            "--package-compiler-chibi-queries | "
            "--package-compiler-chibi-libraries | "
            "--package-compiler-chibi-sharing | "
            "--package-compiler-chibi-debug-free | "
            "--package-compiler-chibi-scalar | "
            "--package-compiler-chibi-native | --native-census [report] | "
            "--native-census-flattening | "
            "--package-compiler-chibi-native-arguments | "
            "--package-compiler-chibi-native-arguments-oracle | "
            ""
            ""
            "--type-relations-chibi | "
            "--unit-algebra-chibi | "
            "--signature-reader-chibi | --snapshot-reader-chibi | "
            ""
            ""
            ""
            ""
            "--package-syntax | --package-syntax-chibi | --package-execution | "
            "--package-resolution | "
            "--package-imports | --host-packages | --host-packages-compiler | "
            "--package-files | --package-core | "
            "--package-math | --package-math-benchmark | --package-portable | "
            "--package-portable-compiler | "
            "--package-tests | --argument-effects-chibi | "
            "--component-write | --component-write-layout | --scalarize | "
            "--borrow | --reductions | "
            "--binding-schedule | "
            "--activation-cache "
            "| "
            "--script-benchmark | --activation-benchmark | --binding-benchmark "
            "| "
            "--borrow-benchmark | --incremental-benchmark | --wasm | "
            "--headless | --build-workflow | "
            "--navigation | "
            "--publication | "
            "--field-runtime | --prototype-document "
            "| --ui-authoring | "
            "--text-format | --runtime-attribution | "
            "--runtime-attribution-benchmark | --query-binding-benchmark | "
            "--scalar-read-benchmark | --packed-benchmark | --record-values | "
            "--record-semantic | --record-document | --record-services | "
            "--record-compatibility | --row-array | "
            "--outliner-projection | --record-publication | "
            "--record-publication-repro | "
            "--compiler-artifact-write "
            "directory | --prepared-write "
            "directory\n",
            argv[0]
        );
        return 2;
    }
    pu_display_time(true);
    if (inferred_arrays || inferred_array_repro) {
        test_language_inferred_string_arrays(inferred_array_repro);
    } else if (record_publication || record_publication_repro) {
        test_language_record_publication(record_publication_repro);
    } else if (outliner_projection) {
        test_language_outliner_projection();
    } else if (row_array) {
        test_language_row_array();
        test_language_packed();
    } else if (record_compatibility) {
        test_language_record_compatibility();
#ifdef CHIBI_TEST_RECORD_SERVICES
        test_language_record_global_compatibility();
#endif
    } else if (record_services) {
#ifdef CHIBI_TEST_RECORD_SERVICES
        test_language_record_services();
#else
        fprintf(stderr, "Record services require the native entity/UI SDK targets\n");
        return 2;
#endif
    } else if (record_producer) {
        test_language_record_producer(argv[2]);
    } else if (record_prepared_runtime) {
        test_language_record_prepared_runtime();
    } else if (record_runtime) {
        test_language_record_runtime();
    } else if (compositional_records) {
        test_language_compositional_records();
    } else if (record_semantic) {
        test_language_record_semantic();
    } else if (record_state) {
        test_language_record_state();
    } else if (record_document) {
        test_language_record_document();
    } else if (record_values) {
        test_language_record_values();
    } else if (runtime_attribution) {
        test_language_runtime_attribution();
    } else if (packed_benchmark) {
        test_language_packed_benchmark();
    } else if (query_binding_benchmark) {
        test_language_query_binding_benchmark();
    } else if (scalar_read_benchmark) {
        test_language_scalar_read_benchmark();
    } else if (runtime_attribution_benchmark) {
        test_language_runtime_attribution_benchmark();
    } else if (wasm) {
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
    } else if (text_format) {
        test_language_text_format();
    } else if (ui_authoring) {
        test_ui_authoring();
    } else if (prototype) {
        test_prototype_document();
#ifdef CHIBI_TEST_SOURCE_ASSETS
        test_language_source_assets();
#endif
    } else if (field_runtime) {
        test_field_runtime();
    } else if (navigation) {
        test_language_navigation();
    } else if (publication) {
        test_language_publication();
    } else if (prepared) {
        test_language_prepared_write(argv[2]);
    } else if (compiler_artifact) {
        test_language_compiler_artifact_write(argv[2]);
    } else if (build_workflow) {
        test_language_build_workflow();
    } else if (headless) {
        test_language_headless();
    } else if (query_values) {
        test_language_query_values();
    } else if (query_program) {
        test_language_query_values();
        test_language_query_program();
        test_language_query_boundary();
        test_language_maps();
        test_language_iteration();
        test_language_generic();
    } else if (modules) {
        test_language_modules();
    } else if (native_calls) {
        test_language_native_calls();
    } else if (incremental) {
        test_language_incremental();
    } else if (operations) {
        test_language_operations();
    } else if (numeric_exact_core) {
        test_numeric_exact_core();
    } else if (integer_shift_counts) {
        test_language_integer_shift_counts(argc == 3 ? argv[2] : NULL);
    } else if (integer_shift_counts_authored) {
        test_language_integer_shift_counts_authored();
    } else if (integer_semantics) {
        test_language_integer_semantics();
    } else if (integer_arithmetic) {
        test_language_integer_arithmetic();
    } else if (package_syntax) {
        test_language_package_syntax();
    } else if (package_syntax_chibi) {
        test_language_package_syntax_chibi();
    } else if (package_paths_chibi) {
        test_language_package_paths_chibi();
    } else if (package_routes_chibi) {
        test_language_package_routes_chibi();
    } else if (package_snapshot_chibi) {
        test_language_package_snapshot_chibi();
    } else if (compiler_fixture) {
        test_language_compiler_fixture();
    } else if (bounded_compiler_chibi) {
        test_language_bounded_compiler_chibi();
    } else if (prepared_writer_chibi) {
        test_language_prepared_writer_chibi();
    } else if (package_compiler_chibi) {
        test_language_package_compiler_chibi();
    } else if (package_compiler_chibi_types) {
        test_language_package_compiler_chibi_types();
    } else if (package_compiler_chibi_fallible) {
        test_language_package_compiler_chibi_fallible();
    } else if (package_compiler_chibi_math_native) {
        test_language_package_compiler_chibi_math_native();
    } else if (package_compiler_chibi_imports) {
        test_language_package_compiler_chibi_imports();
    } else if (package_compiler_chibi_compiled) {
        test_language_package_compiler_chibi_compiled();
    } else if (package_compiler_chibi_types_compiled) {
        test_language_package_compiler_chibi_types_compiled();
    } else if (package_compiler_chibi_collections) {
        test_language_package_compiler_chibi_collections();
    } else if (package_compiler_chibi_collections_compiled) {
        test_language_package_compiler_chibi_collections_compiled();
    } else if (package_compiler_chibi_imports_compiled) {
        test_language_package_compiler_chibi_imports_compiled();
    } else if (package_compiler_chibi_bootstrap) {
        test_language_package_compiler_chibi_bootstrap();
    } else if (package_compiler_chibi_literals) {
        test_language_package_compiler_chibi_literals();
    } else if (package_compiler_chibi_behaviors) {
        test_language_package_compiler_chibi_behaviors();
    } else if (package_compiler_chibi_actions) {
        test_language_package_compiler_chibi_actions();
    } else if (package_compiler_chibi_libraries) {
        test_language_package_compiler_chibi_libraries();
    } else if (package_compiler_chibi_behaviors_rest) {
        test_language_package_compiler_chibi_behaviors_rest();
    } else if (package_compiler_chibi_queries) {
        test_language_package_compiler_chibi_queries();
    } else if (package_compiler_chibi_sharing) {
        test_language_package_compiler_chibi_sharing();
    } else if (package_compiler_chibi_debug_free) {
        test_language_package_compiler_chibi_debug_free();
    } else if (package_compiler_chibi_scalar) {
        test_language_package_compiler_chibi_scalar();
    } else if (package_compiler_chibi_native_arguments) {
        test_language_package_compiler_chibi_native_arguments();
    } else if (package_compiler_chibi_native_arguments_oracle) {
        test_language_package_compiler_chibi_native_arguments_oracle();
    } else if (package_compiler_chibi_native) {
        test_language_package_compiler_chibi_native();
    } else if (native_census) {
        test_language_native_census(argc == 3 ? argv[2] : NULL);
    } else if (native_census_flattening) {
        test_language_native_census_flattening();
    } else if (scalar_lowering_chibi) {
        test_language_scalar_lowering_chibi();
    } else if (type_relations_chibi) {
        test_language_type_relations_chibi();
    } else if (unit_algebra_chibi) {
        test_language_unit_algebra_chibi();
    } else if (signature_reader_chibi) {
        test_language_signature_reader_chibi();
    } else if (snapshot_reader_chibi) {
        test_language_snapshot_reader_chibi();
    } else if (package_execution) {
        test_language_package_execution();
    } else if (package_resolution) {
        test_language_package_resolution();
    } else if (package_imports) {
        test_language_package_imports();
    } else if (host_packages) {
        test_language_host_packages();
    } else if (host_packages_compiler) {
        test_language_host_packages_compiler();
    } else if (package_core) {
        test_language_package_core();
    } else if (package_math) {
        test_language_package_math();
    } else if (package_math_benchmark) {
        test_language_package_math_benchmark();
    } else if (package_portable_compiler) {
        test_language_package_portable_compiler();
    } else if (package_portable) {
        test_language_package_portable();
    } else if (package_tests) {
        test_language_package_tests();
    } else if (argument_effects_chibi) {
        test_language_argument_effects_chibi();
    } else if (package_files) {
        test_language_package_files();
    } else if (package_graph_chibi) {
        test_language_package_graph_chibi();
    } else if (asset_compiler_host) {
#ifdef CHIBI_TEST_ASSET_COMPILER_HOST
        test_language_asset_compiler_host();
#else
        fprintf(stderr, "asset compiler host component unavailable on this platform/configuration\n");
        return 1;
#endif
    } else if (asset_packaging_chibi) {
        test_language_asset_packaging_chibi();
    } else if (shader_source_chibi) {
        test_language_shader_source_chibi();
    } else if (syntax_chibi) {
        test_language_syntax_chibi();
    } else if (source_tokenizer_chibi) {
        test_language_source_tokenizer_chibi();
    } else if (source_parser_chibi) {
        test_language_source_parser_chibi();
    } else if (package_graph) {
        test_language_package_graph();
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
    } else if (native_char_results) {
        test_language_native_char_results();
    } else if (program) {
        test_language_program();
        test_language_retention();
        test_language_capture();
        test_language_query_boundary();
    } else if (program_session) {
        test_language_program_session();
    } else if (semantic) {
        test_language_semantic();
        test_language_effects();
        test_language_symbol_references();
    } else if (execution_scope) {
        test_language_execution_scope();
    } else if (execution) {
        test_language_execution();
    } else {
        test_language_syntax();
        if (corpus) {
            test_language_corpus(argv[3]);
        }
    }
    test_parallel_finish();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

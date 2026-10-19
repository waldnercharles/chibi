#define PICO_UNIT_IMPLEMENTATION
#include "bounded_compiler_chibi.h"
#include "effect_analysis_chibi.h"
#include "package_compiler_chibi.h"
#include "prepared_writer_chibi.h"
#include "record_members_chibi.h"
#include "third_party/pico_headers/pico_unit.h"
#include "type_relations_chibi.h"
#include "union_members_chibi.h"

#include <stdio.h>
#include <string.h>

void test_language_syntax();
void test_language_inferred_string_arrays(bool repro);
void test_language_outliner_projection();
void test_language_row_array();
void test_language_runtime_attribution();
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
void test_language_wasm();
void test_language_semantic();
void test_language_program();
void test_language_capture();
void test_language_query_program();
void test_language_query_boundary();
void test_language_incremental();
void test_language_modules();
void test_numeric_ranges();
void test_numeric_exact_core();
void test_language_package_graph();
void test_language_package_graph_chibi();
void test_language_syntax_chibi();
void test_language_package_syntax();
void test_language_package_syntax_chibi();
void test_language_package_paths_chibi();
void test_language_package_routes_chibi();
void test_language_package_snapshot_chibi();
void test_language_constant_proof_chibi();
void test_language_constant_scalar_chibi();
void test_language_scalar_lowering_chibi();
void test_language_lexical_binding_chibi();
void test_language_nominal_binding_chibi();
void test_language_named_binding_chibi();
void test_language_type_constructors_chibi();
void test_language_unit_algebra_chibi();
void test_language_signature_reader_chibi();
void test_language_snapshot_reader_chibi();
void test_language_callable_headers_chibi();
void test_language_unit_analysis_chibi(int regression);
void test_language_package_execution();
void test_language_package_resolution();
void test_language_package_imports();
void test_language_package_files();
void test_language_package_core();
void test_language_package_tests();
void test_language_docs_markdown();
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
void test_language_format();
void test_language_root_state();
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
void test_language_compiler_artifact_write(const char *directory);
void test_language_record_producer(const char *directory);

int main(int argc, char **argv)
{
    bool format = (argc == 2 || argc == 4) && !strcmp(argv[1], "--format");
    bool record_producer = argc == 3 && !strcmp(argv[1], "--record-produce");
    bool prepared = argc == 3 && !strcmp(argv[1], "--prepared-write");
    bool compiler_artifact = argc == 3 &&
                             !strcmp(argv[1], "--compiler-artifact-write");
    bool corpus = argc == 4 && !strcmp(argv[2], "--corpus");
    bool semantic = argc == 2 && !strcmp(argv[1], "--semantic");
    bool inferred_arrays = argc == 2 && !strcmp(argv[1], "--inferred-string-arrays");
    bool inferred_array_repro = argc == 2 &&
                                !strcmp(argv[1], "--inferred-string-array-repro");
    bool root_state = argc == 2 && !strcmp(argv[1], "--root-state");
    bool execution = argc == 2 && !strcmp(argv[1], "--execution");
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
    bool runtime_attribution = argc == 2 && !strcmp(argv[1], "--runtime-attribution");
    bool wasm = argc == 2 && !strcmp(argv[1], "--wasm");
    bool program = argc == 2 && !strcmp(argv[1], "--program");
    bool query_program = argc == 2 && !strcmp(argv[1], "--query-program");
    bool incremental = argc == 2 && !strcmp(argv[1], "--incremental");
    bool modules = argc == 2 && !strcmp(argv[1], "--modules");
    bool ranges = argc == 2 && !strcmp(argv[1], "--ranges");
    bool numeric_exact_core = argc == 2 && !strcmp(argv[1], "--numeric-exact-core");
    bool package_graph_chibi = argc == 2 && !strcmp(argv[1], "--package-graph-chibi");
    bool syntax_chibi = argc == 2 && !strcmp(argv[1], "--syntax-chibi");
    bool package_paths_chibi = argc == 2 && !strcmp(argv[1], "--package-paths-chibi");
    bool package_routes_chibi = argc == 2 &&
                                !strcmp(argv[1], "--package-routes-chibi");
    bool package_snapshot_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--package-snapshot-chibi");
    bool constant_proof_chibi = argc == 2 &&
                                !strcmp(argv[1], "--constant-proof-chibi");
    bool constant_scalar_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--constant-scalar-chibi");
    bool bounded_compiler_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--bounded-compiler-chibi");
    bool prepared_writer_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--prepared-writer-chibi");
    bool package_compiler_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--package-compiler-chibi");
    bool package_compiler_chibi_types = argc == 2 &&
                                        !strcmp(argv[1], "--package-compiler-chibi-types");
    bool package_compiler_chibi_bootstrap = argc == 2 &&
                                            !strcmp(argv[1], "--package-compiler-chibi-bootstrap");
    bool scalar_lowering_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--scalar-lowering-chibi");
    bool lexical_binding_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--lexical-binding-chibi");
    bool nominal_binding_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--nominal-binding-chibi");
    bool named_binding_chibi = argc == 2 && !strcmp(argv[1], "--named-binding-chibi");
    bool type_constructors_chibi = argc == 2 &&
                                   !strcmp(argv[1], "--type-constructors-chibi");
    bool type_relations_chibi = argc == 2 &&
                                !strcmp(argv[1], "--type-relations-chibi");
    bool unit_algebra_chibi = argc == 2 && !strcmp(argv[1], "--unit-algebra-chibi");
    bool signature_reader_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--signature-reader-chibi");
    bool snapshot_reader_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--snapshot-reader-chibi");
    bool callable_headers_chibi = argc == 2 &&
                                  !strcmp(argv[1], "--callable-headers-chibi");
    bool unit_analysis_chibi = argc == 2 && !strcmp(argv[1], "--unit-analysis-chibi");
    bool unit_analysis_zero_id = argc == 2 &&
                                 !strcmp(argv[1], "--unit-analysis-zero-id");
    bool unit_analysis_deep = argc == 2 && !strcmp(argv[1], "--unit-analysis-deep");
    bool union_members_chibi = argc == 2 && !strcmp(argv[1], "--union-members-chibi");
    bool effect_analysis_chibi = argc == 2 &&
                                 !strcmp(argv[1], "--effect-analysis-chibi");
    bool record_members_chibi = argc == 2 &&
                                !strcmp(argv[1], "--record-members-chibi");
    bool package_graph = argc == 2 && !strcmp(argv[1], "--package-graph");
    bool package_syntax = argc == 2 && !strcmp(argv[1], "--package-syntax");
    bool package_syntax_chibi = argc == 2 &&
                                !strcmp(argv[1], "--package-syntax-chibi");
    bool package_execution = argc == 2 && !strcmp(argv[1], "--package-execution");
    bool package_resolution = argc == 2 && !strcmp(argv[1], "--package-resolution");
    bool package_imports = argc == 2 && !strcmp(argv[1], "--package-imports");
    bool package_core = argc == 2 && !strcmp(argv[1], "--package-core");
    bool package_tests = argc == 2 && !strcmp(argv[1], "--package-tests");
    bool docs_markdown = argc == 2 && !strcmp(argv[1], "--docs-markdown");
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
    bool model_runtime = argc == 2 && !strcmp(argv[1], "--model-runtime");
    bool graph_authoring = argc == 2 && !strcmp(argv[1], "--graph-authoring");
    bool graph_model = argc == 2 && !strcmp(argv[1], "--graph-model");
    bool text_format = argc == 2 && !strcmp(argv[1], "--text-format");
    bool ui_authoring = argc == 2 && !strcmp(argv[1], "--ui-authoring");
    if ((format && argc == 4 && !corpus) ||
        (!format && !prepared && !compiler_artifact && !record_producer &&
         !execution && !semantic && !inferred_arrays && !inferred_array_repro &&
         !root_state && !benchmark && !activation_benchmark && !binding_benchmark &&
         !borrow_benchmark && !incremental_benchmark && !record_publication &&
         !record_publication_repro && !record_values && !record_document &&
         !record_state && !record_services && !record_compatibility &&
         !record_semantic && !record_prepared_runtime && !outliner_projection &&
         !row_array && !record_runtime && !runtime_attribution && !wasm &&
         !program && !query_program && !incremental && !modules && !ranges &&
         !numeric_exact_core && !package_graph && !package_graph_chibi &&
         !syntax_chibi && !package_syntax && !package_syntax_chibi &&
         !package_paths_chibi && !package_routes_chibi && !package_snapshot_chibi &&
         !constant_proof_chibi && !constant_scalar_chibi && !scalar_lowering_chibi &&
         !bounded_compiler_chibi && !prepared_writer_chibi && !package_compiler_chibi &&
         !package_compiler_chibi_types && !package_compiler_chibi_bootstrap &&
         !lexical_binding_chibi && !nominal_binding_chibi && !named_binding_chibi &&
         !type_constructors_chibi && !type_relations_chibi &&
         !unit_algebra_chibi && !signature_reader_chibi && !snapshot_reader_chibi &&
         !callable_headers_chibi && !unit_analysis_chibi && !unit_analysis_zero_id &&
         !unit_analysis_deep && !union_members_chibi && !effect_analysis_chibi &&
         !record_members_chibi && !package_execution && !package_resolution &&
         !package_imports && !package_files && !package_core && !package_tests &&
         !docs_markdown && !component_write && !component_write_layout &&
         !scalarize && !borrow && !reductions && !binding_schedule &&
         !activation_cache && !headless && !build_workflow && !navigation &&
         !publication && !field_runtime && !prototype && !ui_authoring &&
         !text_format && !graph_model && !graph_authoring && !model_runtime &&
         ((argc != 2 && !corpus) || strcmp(argv[1], "--syntax")))) {
        fprintf(
            stderr,
            "Usage: %s --syntax [--corpus manifest] | --execution | --semantic "
            "| --inferred-string-arrays | --inferred-string-array-repro "
            "| --root-state | --program | --query-program | --incremental | "
            "--modules | "
            "--ranges | --numeric-exact-core | --package-graph | "
            "--package-graph-chibi | --syntax-chibi | --package-paths-chibi | "
            "--package-routes-chibi | --package-snapshot-chibi | "
            "--constant-proof-chibi | --constant-scalar-chibi | "
            "--scalar-lowering-chibi | --bounded-compiler-chibi | "
            "--prepared-writer-chibi | --package-compiler-chibi | "
            "--package-compiler-chibi-types | "
            "--package-compiler-chibi-bootstrap | "
            "--lexical-binding-chibi | --nominal-binding-chibi | "
            "--named-binding-chibi | "
            "--type-constructors-chibi | --type-relations-chibi | "
            "--unit-algebra-chibi | "
            "--signature-reader-chibi | --snapshot-reader-chibi | "
            "--callable-headers-chibi | --unit-analysis-chibi | "
            "--unit-analysis-zero-id | --unit-analysis-deep | "
            "--union-members-chibi | --effect-analysis-chibi | "
            "--record-members-chibi | "
            "--package-syntax | --package-syntax-chibi | --package-execution | "
            "--package-resolution | "
            "--package-imports | --package-files | --package-core | "
            "--package-tests | --docs-markdown | "
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
            "--field-runtime | --prototype-document | --ui-authoring | "
            "--text-format | --graph-model | --graph-authoring | "
            "--model-runtime | --runtime-attribution | --record-values | "
            "--record-semantic | --record-document | --record-services | "
            "--record-compatibility | --row-array | "
            "--outliner-projection | --record-publication | "
            "--record-publication-repro | "
            "--format [--corpus manifest] | --compiler-artifact-write "
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
    } else if (root_state) {
        test_language_root_state();
    } else if (format) {
        test_language_format();
        if (corpus) {
            test_language_format_corpus(argv[3]);
        }
    } else if (prepared) {
        test_language_prepared_write(argv[2]);
    } else if (compiler_artifact) {
        test_language_compiler_artifact_write(argv[2]);
    } else if (build_workflow) {
        test_language_build_workflow();
    } else if (headless) {
        test_language_headless();
    } else if (query_program) {
        test_language_query_program();
        test_language_query_boundary();
    } else if (modules) {
        test_language_modules();
    } else if (incremental) {
        test_language_incremental();
    } else if (numeric_exact_core) {
        test_numeric_exact_core();
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
    } else if (constant_proof_chibi) {
        test_language_constant_proof_chibi();
    } else if (constant_scalar_chibi) {
        test_language_constant_scalar_chibi();
    } else if (bounded_compiler_chibi) {
        test_language_bounded_compiler_chibi();
    } else if (prepared_writer_chibi) {
        test_language_prepared_writer_chibi();
    } else if (package_compiler_chibi) {
        test_language_package_compiler_chibi();
    } else if (package_compiler_chibi_types) {
        test_language_package_compiler_chibi_types();
    } else if (package_compiler_chibi_bootstrap) {
        test_language_package_compiler_chibi_bootstrap();
    } else if (scalar_lowering_chibi) {
        test_language_scalar_lowering_chibi();
    } else if (lexical_binding_chibi) {
        test_language_lexical_binding_chibi();
    } else if (nominal_binding_chibi) {
        test_language_nominal_binding_chibi();
    } else if (named_binding_chibi) {
        test_language_named_binding_chibi();
    } else if (type_constructors_chibi) {
        test_language_type_constructors_chibi();
    } else if (type_relations_chibi) {
        test_language_type_relations_chibi();
    } else if (unit_algebra_chibi) {
        test_language_unit_algebra_chibi();
    } else if (signature_reader_chibi) {
        test_language_signature_reader_chibi();
    } else if (snapshot_reader_chibi) {
        test_language_snapshot_reader_chibi();
    } else if (callable_headers_chibi) {
        test_language_callable_headers_chibi();
    } else if (unit_analysis_chibi || unit_analysis_zero_id || unit_analysis_deep) {
        test_language_unit_analysis_chibi(
            unit_analysis_zero_id ? 1
            : unit_analysis_deep  ? 2
                                  : 0
        );
    } else if (union_members_chibi) {
        test_language_union_members_chibi();
    } else if (effect_analysis_chibi) {
        test_language_effect_analysis_chibi();
    } else if (record_members_chibi) {
        test_language_record_members_chibi();
    } else if (package_execution) {
        test_language_package_execution();
    } else if (package_resolution) {
        test_language_package_resolution();
    } else if (package_imports) {
        test_language_package_imports();
    } else if (package_core) {
        test_language_package_core();
    } else if (package_tests) {
        test_language_package_tests();
    } else if (docs_markdown) {
        test_language_docs_markdown();
    } else if (package_files) {
        test_language_package_files();
    } else if (package_graph_chibi) {
        test_language_package_graph_chibi();
    } else if (syntax_chibi) {
        test_language_syntax_chibi();
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

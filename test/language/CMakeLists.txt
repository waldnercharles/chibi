cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(chibi_language_tests LANGUAGES C)
endif()

set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "")
get_filename_component(CHIBI_LANGUAGE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(CHIBI_INSTALLED_SDK "Test language against an installed SDK" OFF)
if(CHIBI_INSTALLED_SDK)
    find_package(Chibi REQUIRED CONFIG COMPONENTS value_document array_capabilities math_capabilities authoring source_catalog program input_map source_presentation behavior_runtime field_catalog field_capabilities project_document prototype_document scene_document ui_authoring ui_document text_capabilities graph_model graph_authoring graph_compilation mesh_runtime ui_documents)
elseif(NOT TARGET Chibi::language_syntax)
    add_subdirectory("${CHIBI_LANGUAGE_TEST_ROOT}/sdk" sdk)
endif()

configure_file("${CHIBI_LANGUAGE_TEST_ROOT}/third_party/pico_headers/pico_unit.h"
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/third_party/pico_headers/pico_unit.h" COPYONLY)
configure_file("${CHIBI_LANGUAGE_TEST_ROOT}/test/prepared_fixture.h"
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/prepared_fixture.h" COPYONLY)
configure_file("${CHIBI_LANGUAGE_TEST_ROOT}/test/parallel.h"
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/test/parallel.h" COPYONLY)
# Compile the shipped core source in the fixture, including installed consumers.
if(CHIBI_INSTALLED_SDK)
    set(core_strings_source "${Chibi_CORE_SOURCE_DIR}/strings/Strings.chibi")
else()
    set(core_strings_source "${CHIBI_LANGUAGE_TEST_ROOT}/packages/core/strings/Strings.chibi")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${core_strings_source}")
file(READ "${core_strings_source}" CHIBI_CORE_STRINGS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CORE_STRINGS_BYTES "${CHIBI_CORE_STRINGS_HEX}")
configure_file(package_core_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_core_source.h" @ONLY)

# Core string regression sources use the same discovery ordering as Graph.
set(test_root "${CHIBI_LANGUAGE_TEST_ROOT}/test/core_strings")
file(GLOB test_sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${test_root}/*.chibi")
list(FILTER test_sources INCLUDE REGEX "\\.chibi$")
if(NOT test_sources)
    message(FATAL_ERROR "The authored core strings regression package is empty")
endif()
set(CHIBI_CORE_STRINGS_TEST_SOURCES "")
set(CHIBI_CORE_STRINGS_TEST_INPUTS "")
set(test_index 0)
foreach(source IN LISTS test_sources)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," source_bytes "${source_hex}")
    get_filename_component(name "${source}" NAME)
    string(APPEND CHIBI_CORE_STRINGS_TEST_SOURCES
        "static const char CORE_STRINGS_TEST_SOURCE_${test_index}[] = { ${source_bytes} 0 };\n")
    string(APPEND CHIBI_CORE_STRINGS_TEST_INPUTS
        "    PACKAGE_COMPILER_SOURCE(\"core_strings/${name}\", CORE_STRINGS_TEST_SOURCE_${test_index}), \\\n")
    math(EXPR test_index "${test_index} + 1")
endforeach()
configure_file(core_strings_tests_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/core_strings_tests_source.h" @ONLY)

set(compositional_records_source "${CHIBI_LANGUAGE_TEST_ROOT}/test/language/fixtures/compositional_records/Values.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${compositional_records_source}")
file(READ "${compositional_records_source}" compositional_records_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_COMPOSITIONAL_RECORDS_BYTES "${compositional_records_hex}")
configure_file(compositional_records_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/compositional_records_source.h" @ONLY)

set(integer_arithmetic_source "${CHIBI_LANGUAGE_TEST_ROOT}/test/integer_arithmetic/Arithmetic.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${integer_arithmetic_source}")
file(READ "${integer_arithmetic_source}" CHIBI_INTEGER_ARITHMETIC_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_INTEGER_ARITHMETIC_BYTES "${CHIBI_INTEGER_ARITHMETIC_HEX}")
configure_file(integer_arithmetic_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/integer_arithmetic_source.h" @ONLY)

# The documentation site's Markdown renderer is authored package source too.
set(docs_markdown_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/docs/markdown/Markdown.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${docs_markdown_source}")
file(READ "${docs_markdown_source}" CHIBI_DOCS_MARKDOWN_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_DOCS_MARKDOWN_BYTES "${CHIBI_DOCS_MARKDOWN_HEX}")
foreach(part SITE:site/Site.chibi REFERENCE:reference/Reference.chibi GRAMMAR:grammar/Grammar.chibi)
    string(REPLACE ":" ";" part "${part}")
    list(GET part 0 name)
    list(GET part 1 file)
    set(docs_part_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/docs/${file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${docs_part_source}")
    file(READ "${docs_part_source}" docs_part_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_DOCS_${name}_BYTES "${docs_part_hex}")
endforeach()
configure_file(docs_markdown_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/docs_markdown_source.h" @ONLY)

# The first compiler component is ordinary authored package source.
set(package_graph_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_graph/Graph.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${package_graph_source}")
file(READ "${package_graph_source}" CHIBI_PACKAGE_GRAPH_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_GRAPH_BYTES "${CHIBI_PACKAGE_GRAPH_HEX}")
configure_file(package_graph_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_graph_chibi_source.h" @ONLY)

set(syntax_diagnostics_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/syntax_diagnostics/Diagnostics.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${syntax_diagnostics_source}")
file(READ "${syntax_diagnostics_source}" CHIBI_SYNTAX_DIAGNOSTICS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SYNTAX_DIAGNOSTICS_BYTES "${CHIBI_SYNTAX_DIAGNOSTICS_HEX}")
configure_file(syntax_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/syntax_chibi_source.h" @ONLY)

set(source_tokenizer_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/source_tokenizer/Tokenizer.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_tokenizer_source}")
file(READ "${source_tokenizer_source}" CHIBI_SOURCE_TOKENIZER_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SOURCE_TOKENIZER_BYTES "${CHIBI_SOURCE_TOKENIZER_HEX}")
configure_file(source_tokenizer_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/source_tokenizer_chibi_source.h" @ONLY)

set(package_syntax_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_syntax/Extract.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${package_syntax_source}")
file(READ "${package_syntax_source}" CHIBI_PACKAGE_SYNTAX_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_SYNTAX_BYTES "${CHIBI_PACKAGE_SYNTAX_HEX}")
configure_file(package_syntax_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_syntax_chibi_source.h" @ONLY)

set(package_paths_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_paths/Paths.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${package_paths_source}")
file(READ "${package_paths_source}" CHIBI_PACKAGE_PATHS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_PATHS_BYTES "${CHIBI_PACKAGE_PATHS_HEX}")
configure_file(package_paths_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_paths_chibi_source.h" @ONLY)

set(package_routes_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_routes/Routes.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${package_routes_source}")
file(READ "${package_routes_source}" CHIBI_PACKAGE_ROUTES_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_ROUTES_BYTES "${CHIBI_PACKAGE_ROUTES_HEX}")
configure_file(package_routes_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_routes_chibi_source.h" @ONLY)

set(package_snapshot_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/source_snapshot/Snapshot.chibi")
set(compiler_syntax_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/compiler_syntax/Syntax.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${package_snapshot_source}" "${compiler_syntax_source}")
file(READ "${package_snapshot_source}" CHIBI_PACKAGE_SNAPSHOT_HEX HEX)
file(READ "${compiler_syntax_source}" CHIBI_COMPILER_SYNTAX_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_SNAPSHOT_BYTES "${CHIBI_PACKAGE_SNAPSHOT_HEX}")
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_COMPILER_SYNTAX_BYTES "${CHIBI_COMPILER_SYNTAX_HEX}")
configure_file(package_snapshot_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_snapshot_chibi_source.h" @ONLY)

set(constant_ir_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/constant_ir/IR.chibi")
set(constant_proof_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/constant_admissibility/Proof.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${constant_ir_source}" "${constant_proof_source}")
file(READ "${constant_ir_source}" CHIBI_CONSTANT_IR_HEX HEX)
file(READ "${constant_proof_source}" CHIBI_CONSTANT_PROOF_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CONSTANT_IR_BYTES "${CHIBI_CONSTANT_IR_HEX}")
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CONSTANT_PROOF_BYTES "${CHIBI_CONSTANT_PROOF_HEX}")
configure_file(constant_proof_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/constant_proof_chibi_source.h" @ONLY)

set(constant_scalar_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/constant_scalar/Scalar.chibi")
set(constant_folding_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/constant_folding/Fold.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${constant_scalar_source}" "${constant_folding_source}")
file(READ "${constant_scalar_source}" CHIBI_CONSTANT_SCALAR_HEX HEX)
file(READ "${constant_folding_source}" CHIBI_CONSTANT_FOLDING_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CONSTANT_SCALAR_BYTES "${CHIBI_CONSTANT_SCALAR_HEX}")
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CONSTANT_FOLDING_BYTES "${CHIBI_CONSTANT_FOLDING_HEX}")
set(CHIBI_SCALAR_IR_BYTES "${CHIBI_CONSTANT_IR_BYTES}")
set(CHIBI_SCALAR_PROOF_BYTES "${CHIBI_CONSTANT_PROOF_BYTES}")
configure_file(constant_scalar_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/constant_scalar_chibi_source.h" @ONLY)

set(scalar_lowering_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/scalar_lowering/Lower.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${scalar_lowering_source}")
file(READ "${scalar_lowering_source}" CHIBI_SCALAR_LOWERING_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SCALAR_LOWERING_BYTES "${CHIBI_SCALAR_LOWERING_HEX}")
configure_file(scalar_lowering_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/scalar_lowering_chibi_source.h" @ONLY)

set(lexical_binding_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/lexical_binding/Binding.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${lexical_binding_source}")
file(READ "${lexical_binding_source}" CHIBI_LEXICAL_BINDING_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_LEXICAL_BINDING_BYTES "${CHIBI_LEXICAL_BINDING_HEX}")
configure_file(lexical_binding_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/lexical_binding_chibi_source.h" @ONLY)

set(nominal_binding_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/nominal_binding/Nominal.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${nominal_binding_source}")
file(READ "${nominal_binding_source}" CHIBI_NOMINAL_BINDING_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_NOMINAL_BINDING_BYTES "${CHIBI_NOMINAL_BINDING_HEX}")
configure_file(nominal_binding_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/nominal_binding_chibi_source.h" @ONLY)

set(named_binding_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/named_binding/Binding.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${named_binding_source}")
file(READ "${named_binding_source}" CHIBI_NAMED_BINDING_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_NAMED_BINDING_BYTES "${CHIBI_NAMED_BINDING_HEX}")
configure_file(named_binding_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/named_binding_chibi_source.h" @ONLY)

set(type_constructors_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/type_constructors/Constructors.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${type_constructors_source}")
file(READ "${type_constructors_source}" CHIBI_TYPE_CONSTRUCTORS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_TYPE_CONSTRUCTORS_BYTES "${CHIBI_TYPE_CONSTRUCTORS_HEX}")
configure_file(type_constructors_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/type_constructors_chibi_source.h" @ONLY)

set(type_relations_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/type_relations/Relations.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${type_relations_source}")
file(READ "${type_relations_source}" CHIBI_TYPE_RELATIONS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_TYPE_RELATIONS_BYTES "${CHIBI_TYPE_RELATIONS_HEX}")
configure_file(type_relations_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/type_relations_chibi_source.h" @ONLY)

set(unit_algebra_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/unit_algebra/Units.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${unit_algebra_source}")
file(READ "${unit_algebra_source}" CHIBI_UNIT_ALGEBRA_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_UNIT_ALGEBRA_BYTES "${CHIBI_UNIT_ALGEBRA_HEX}")
configure_file(unit_algebra_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/unit_algebra_chibi_source.h" @ONLY)

set(signature_reader_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/signature_reader/Reader.chibi")
set(analysis_state_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/analysis_state/State.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${signature_reader_source}" "${analysis_state_source}")
file(READ "${signature_reader_source}" CHIBI_SIGNATURE_READER_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SIGNATURE_READER_BYTES "${CHIBI_SIGNATURE_READER_HEX}")
file(READ "${analysis_state_source}" CHIBI_ANALYSIS_STATE_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_ANALYSIS_STATE_BYTES "${CHIBI_ANALYSIS_STATE_HEX}")
configure_file(signature_reader_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/signature_reader_chibi_source.h" @ONLY)

set(callable_headers_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/callable_headers/Headers.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${callable_headers_source}")
file(READ "${callable_headers_source}" CHIBI_CALLABLE_HEADERS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_CALLABLE_HEADERS_BYTES "${CHIBI_CALLABLE_HEADERS_HEX}")
configure_file(callable_headers_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/callable_headers_chibi_source.h" @ONLY)

set(unit_analysis_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/unit_analysis/Analysis.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${unit_analysis_source}")
file(READ "${unit_analysis_source}" CHIBI_UNIT_ANALYSIS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_UNIT_ANALYSIS_BYTES "${CHIBI_UNIT_ANALYSIS_HEX}")
configure_file(unit_analysis_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/unit_analysis_chibi_source.h" @ONLY)

set(union_members_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/union_members/Members.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${union_members_source}")
file(READ "${union_members_source}" CHIBI_UNION_MEMBERS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_UNION_MEMBERS_BYTES "${CHIBI_UNION_MEMBERS_HEX}")
configure_file(union_members_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/union_members_chibi_source.h" @ONLY)

set(effect_analysis_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/effect_analysis/Effects.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${effect_analysis_source}")
file(READ "${effect_analysis_source}" CHIBI_EFFECT_ANALYSIS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_EFFECT_ANALYSIS_BYTES "${CHIBI_EFFECT_ANALYSIS_HEX}")
configure_file(effect_analysis_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/effect_analysis_chibi_source.h" @ONLY)

set(record_members_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/record_members/Members.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${record_members_source}")
file(READ "${record_members_source}" CHIBI_RECORD_MEMBERS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_RECORD_MEMBERS_BYTES "${CHIBI_RECORD_MEMBERS_HEX}")
configure_file(record_members_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/record_members_chibi_source.h" @ONLY)

# Embed the single reviewed build-flow fixture; no runtime filesystem input.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildWorkflow.chibi")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/BuildWorkflow.chibi" CHIBI_BUILD_WORKFLOW_SOURCE)
string(REPLACE "\\" "\\\\" CHIBI_BUILD_WORKFLOW_SOURCE "${CHIBI_BUILD_WORKFLOW_SOURCE}")
string(REPLACE "\"" "\\\"" CHIBI_BUILD_WORKFLOW_SOURCE "${CHIBI_BUILD_WORKFLOW_SOURCE}")
string(REPLACE "\n" "\\n\"\n\"" CHIBI_BUILD_WORKFLOW_SOURCE "${CHIBI_BUILD_WORKFLOW_SOURCE}")
configure_file(build_workflow_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/build_workflow_source.h" @ONLY)

add_library(test_behavior_consumer OBJECT headless_consumer.c)
target_include_directories(test_behavior_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_link_libraries(test_behavior_consumer PRIVATE Chibi::behavior_runtime)
target_compile_options(test_behavior_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
# The backend-specific fixtures inspect private activation storage. Other tests
# continue to compile solely against their public SDK dependencies.
set_source_files_properties(binding_benchmark.c inferred_string_arrays.c record_publication.c outliner_projection.c row_array.c record_state.c record_prepared_runtime.c record_runtime.c record_semantic.c record_prepared.c runtime_attribution.c wasm.c execution.c ranges.c scalarize.c component_write.c component_write_source.c component_write_ir.c borrow.c query_boundary.c query_values.c program.c capture.c maps.c PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
add_executable(test_language main.c inferred_string_arrays.c build_workflow.c record_publication.c outliner_projection.c row_array.c record_compatibility.c record_state.c record_document.c record_producer.c record_prepared_runtime.c record_runtime.c record_semantic.c record_values.c record_prepared.c runtime_attribution.c syntax.c semantic.c execution.c program.c retention.c capture.c query_boundary.c query_values.c incremental.c operations.c modules.c native_calls.c ranges.c scalarize.c component_write.c component_write_source.c component_write_ir.c borrow.c reductions.c benchmark.c activation_cache.c activation_benchmark.c binding_benchmark.c borrow_benchmark.c incremental_benchmark.c wasm.c headless.c prepared.c format.c navigation.c publication.c field_runtime.c prototype.c ui.c text_format.c graph_model.c graph_authoring.c ../authoring/model_runtime.c)
# Package tests consume the owning SDK implementation; extraction stays private.
target_sources(test_language PRIVATE packages.c package_syntax.c package_execution.c package_resolution.c package_imports.c package_files.c package_core.c package_math.c package_tests.c package_graph_chibi.c docs_markdown.c)
target_sources(test_language PRIVATE compositional_records.c)
set_source_files_properties(compositional_records.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_sources(test_language PRIVATE argument_effects_chibi.c)
set_source_files_properties(argument_effects_chibi.c package_tests.c PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_link_libraries(test_language PRIVATE Chibi::tasks)
target_sources(test_language PRIVATE program_session.c)
target_link_libraries(test_language PRIVATE Chibi::program)
if(TARGET Chibi::task_pool_runtime)
    target_link_libraries(test_language PRIVATE Chibi::task_pool_runtime Chibi::conversion_capabilities)
    target_compile_definitions(test_language PRIVATE CHIBI_PACKAGE_TEST_POOL=1)
endif()
target_sources(test_language PRIVATE syntax_corpus.c syntax_chibi.c compiler_fixture.c package_syntax_chibi.c package_paths_chibi.c)
target_sources(test_language PRIVATE source_tokenizer_chibi.c source_parser_chibi.c)
# The tokenizer differential reads the checked-in sources at run time.
set_source_files_properties(source_tokenizer_chibi.c source_parser_chibi.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src"
    COMPILE_DEFINITIONS "CHIBI_SOURCE_ROOT=\"${CHIBI_LANGUAGE_TEST_ROOT}\"")
target_sources(test_language PRIVATE package_routes_chibi.c package_snapshot_chibi.c)
set_source_files_properties(package_syntax.c package_syntax_chibi.c package_paths_chibi.c package_routes_chibi.c package_snapshot_chibi.c docs_markdown.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_json.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_syntax.c"
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_link_libraries(test_language PRIVATE Chibi::json Chibi::language_syntax)
target_sources(test_language PRIVATE compiler_artifact_chibi.c)
set_source_files_properties(compiler_artifact_chibi.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_artifact.c"
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_link_libraries(test_language PRIVATE Chibi::asset_json Chibi::native_prepared)
if(TARGET Chibi::compiler_host)
    target_link_libraries(test_language PRIVATE Chibi::compiler_host)
else()
    target_sources(test_language PRIVATE
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_target.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_json.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_syntax.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_artifact.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_bootstrap.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/support/sha256.c")
    set_source_files_properties(
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_target.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_bootstrap.c"
        "${CHIBI_LANGUAGE_TEST_ROOT}/src/support/sha256.c"
        PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
endif()
if(TARGET Chibi::asset_compiler_host)
    set(portable_asset_source "${CHIBI_LANGUAGE_TEST_ROOT}/test/fixtures/portable-byte-asset/Build.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${portable_asset_source}")
    file(READ "${portable_asset_source}" portable_asset_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PORTABLE_ASSET_BYTES "${portable_asset_hex}")
    configure_file(portable_asset_source.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/testing/include/portable_asset_source.h" @ONLY)
    add_library(chibi_asset_compiler_consumer OBJECT asset_compiler_host.c)
    target_include_directories(chibi_asset_compiler_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
    target_link_libraries(chibi_asset_compiler_consumer PRIVATE Chibi::asset_compiler_host Chibi::behavior_runtime
        Chibi::scene_document Chibi::prototype_document)
    target_compile_options(chibi_asset_compiler_consumer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_sources(test_language PRIVATE $<TARGET_OBJECTS:chibi_asset_compiler_consumer>)
    target_link_libraries(test_language PRIVATE Chibi::asset_compiler_host)
    target_compile_definitions(test_language PRIVATE CHIBI_TEST_ASSET_COMPILER_HOST=1)
endif()
include(bounded_compiler_chibi.cmake)
include(prepared_writer_chibi.cmake)
include(shader_source_chibi.cmake)
include(asset_packaging_chibi.cmake)
include(package_compiler_chibi.cmake)
include(package_portable.cmake)
# W21's census of the compiler functions the native path cannot compile yet.
target_sources(test_language PRIVATE native_census.c)
set_source_files_properties(native_census.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_sources(test_language PRIVATE execution_scope.c)
set_source_files_properties(execution_scope.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
# Integer semantics uses the checked authored compiler through the same host
# archive as other compiler fixtures. Older SDKs retain the direct-source
# fallback above; normal and installed compiler-host builds own these once.
target_sources(test_language PRIVATE integer_semantics.c integer_arithmetic.c)
set_source_files_properties(integer_semantics.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src"
    COMPILE_DEFINITIONS "CHIBI_SOURCE_ROOT=\"${CHIBI_LANGUAGE_TEST_ROOT}\"")
target_sources(test_language PRIVATE scalar_lowering_chibi.c)
set_source_files_properties(scalar_lowering_chibi.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_sources(test_language PRIVATE constant_proof_chibi.c constant_scalar_chibi.c lexical_binding_chibi.c nominal_binding_chibi.c named_binding_chibi.c type_constructors_chibi.c type_relations_chibi.c unit_algebra_chibi.c signature_fixture.c signature_reader_chibi.c callable_headers_chibi.c unit_analysis_chibi.c union_members_chibi.c effect_analysis_chibi.c record_members_chibi.c)
set_source_files_properties(constant_proof_chibi.c constant_scalar_chibi.c lexical_binding_chibi.c nominal_binding_chibi.c named_binding_chibi.c type_constructors_chibi.c type_relations_chibi.c unit_algebra_chibi.c signature_fixture.c signature_reader_chibi.c callable_headers_chibi.c unit_analysis_chibi.c union_members_chibi.c effect_analysis_chibi.c record_members_chibi.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
set_source_files_properties(package_math.c PROPERTIES
    COMPILE_DEFINITIONS "CHIBI_SOURCE_ROOT=\"${CHIBI_LANGUAGE_TEST_ROOT}\"")
if(CHIBI_INSTALLED_SDK)
    set(core_math_source_root "${Chibi_CORE_SOURCE_DIR}/math")
else()
    set(core_math_source_root "${CHIBI_LANGUAGE_TEST_ROOT}/packages/core/math")
endif()
set(core_math_fixture_root "${core_math_source_root}")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Only the six shipped math sources enter this fixture filesystem.
    # Installed consumers embed their installed bytes, not this checkout's.
    set(core_math_fixture_root "/chibi-fixtures/core/math")
    foreach(source Math.chibi Spring.chibi MathTests.chibi SpringTests.chibi
                   ease/Ease.chibi ease/EaseTests.chibi)
        set(math_source "${core_math_source_root}/${source}")
        set_property(TARGET test_language APPEND PROPERTY LINK_DEPENDS "${math_source}")
        target_link_options(test_language PRIVATE
            "SHELL:--embed-file \"${math_source}@${core_math_fixture_root}/${source}\"")
    endforeach()
endif()
set_property(SOURCE package_math.c package_compiler_chibi.c APPEND PROPERTY
    COMPILE_DEFINITIONS "CHIBI_CORE_MATH_ROOT=\"${core_math_fixture_root}\"")
target_sources(test_language PRIVATE numeric.c)
set_source_files_properties(numeric.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_include_directories(test_language PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testing/include")
target_sources(test_language PRIVATE "${CHIBI_LANGUAGE_TEST_ROOT}/test/parallel.c" parallel.c)
if(TARGET Chibi::task_scheduler AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # SDK language activation does not activate the parent's link rules.
    enable_language(CXX)
    target_link_libraries(test_language PRIVATE Chibi::task_scheduler)
    target_compile_definitions(test_language PRIVATE CHIBI_TEST_ENKITS=1)
endif()
target_sources(test_language PRIVATE effects.c maps.c generic.c symbol_references.c packed.c iteration.c)
target_link_libraries(test_language PRIVATE Chibi::math_capabilities)
set_source_files_properties(effects.c generic.c PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
if(TARGET Chibi::project_compilation)
    target_sources(test_language PRIVATE source_assets.c)
    target_link_libraries(test_language PRIVATE Chibi::project_compilation Chibi::asset_text)
    target_compile_definitions(test_language PRIVATE CHIBI_TEST_SOURCE_ASSETS)
endif()
target_link_libraries(test_language PRIVATE Chibi::value_document Chibi::array_capabilities Chibi::authoring Chibi::source_catalog Chibi::input_map Chibi::source_presentation Chibi::behavior_runtime Chibi::field_catalog Chibi::field_capabilities Chibi::project_document Chibi::prototype_document Chibi::scene_document Chibi::ui_authoring Chibi::ui_document Chibi::text_capabilities Chibi::state_capabilities Chibi::arithmetic_capabilities Chibi::core_capabilities Chibi::graph_model Chibi::graph_authoring Chibi::graph_compilation Chibi::mesh_runtime Chibi::ui_documents test_behavior_consumer)
target_compile_options(test_language PRIVATE -Wall -Wextra -Werror -UNDEBUG)
if(TARGET Chibi::entity_runtime AND TARGET Chibi::ui_runtime)
    target_sources(test_language PRIVATE record_services.c)
    target_link_libraries(test_language PRIVATE Chibi::entity_runtime Chibi::ui_runtime)
    target_compile_definitions(test_language PRIVATE CHIBI_TEST_RECORD_SERVICES)
endif()
# Source identity belongs to each diagnostic report, not an inferred module ID.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime_attribution.c")
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/runtime_attribution.c" attribution_fixture_sha)
set_source_files_properties(runtime_attribution.c PROPERTIES COMPILE_DEFINITIONS
    "CHIBI_ATTRIBUTION_BASE=\"3c65bb60b6f3443aef6ccf4ab15acbe09b04b191\";CHIBI_ATTRIBUTION_FIXTURE_SHA=\"${attribution_fixture_sha}\";CHIBI_ATTRIBUTION_BUILD=\"${CMAKE_BUILD_TYPE}\"")
if(APPLE)
    target_link_options(test_language PRIVATE -Wl,-dead_strip)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set_target_properties(test_language PROPERTIES SUFFIX ".js")
    target_link_options(test_language PRIVATE -sENVIRONMENT=node,web,worker
        -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
endif()

# Test input discovery is explicit and separate from shipped product resources.
set(corpus_roots test/fixtures)
set(CHIBI_LANGUAGE_CORPUS_PROJECTS "" CACHE STRING
    "Explicit absolute external project roots for the bounded language corpus check")
set(CHIBI_LANGUAGE_CORPUS_MANIFEST "" CACHE FILEPATH
    "Explicit corpus manifest containing one absolute source path per line")
set(corpus)
foreach(root IN LISTS corpus_roots)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CHIBI_LANGUAGE_TEST_ROOT}/${root}/*.chibi")
    list(APPEND corpus ${sources})
endforeach()
foreach(root IN LISTS CHIBI_LANGUAGE_CORPUS_PROJECTS)
    if(NOT IS_ABSOLUTE "${root}" OR NOT IS_DIRECTORY "${root}")
        message(FATAL_ERROR "Language corpus project must be an existing absolute directory: ${root}")
    endif()
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${root}/*.chibi")
    list(APPEND corpus ${sources})
endforeach()
if(CHIBI_LANGUAGE_CORPUS_MANIFEST)
    if(NOT IS_ABSOLUTE "${CHIBI_LANGUAGE_CORPUS_MANIFEST}" OR
       NOT EXISTS "${CHIBI_LANGUAGE_CORPUS_MANIFEST}")
        message(FATAL_ERROR "Language corpus manifest must be an existing absolute file")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CHIBI_LANGUAGE_CORPUS_MANIFEST}")
    file(STRINGS "${CHIBI_LANGUAGE_CORPUS_MANIFEST}" manifest_sources)
    foreach(source IN LISTS manifest_sources)
        if(source STREQUAL "" OR source MATCHES "^#")
            continue()
        endif()
        if(NOT IS_ABSOLUTE "${source}" OR NOT EXISTS "${source}" OR IS_DIRECTORY "${source}" OR
           NOT source MATCHES "\\.chibi$")
            message(FATAL_ERROR "Language corpus manifest requires existing absolute .chibi source files: ${source}")
        endif()
        list(APPEND corpus "${source}")
    endforeach()
endif()
list(SORT corpus)
list(REMOVE_DUPLICATES corpus)
list(JOIN corpus "\n" corpus_text)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/corpus.txt" "${corpus_text}\n")

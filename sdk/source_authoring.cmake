# Source preparation is an authoring dependency, never a player dependency.
add_library(chibi_source_catalog STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/language_catalog.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_catalog.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_asset.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_asset_completion.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_catalog_snapshot.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_collection.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_packages.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_syntax.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_paths.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_routes.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_snapshot.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_files.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_exports.c"
    "${CHIBI_SDK_ROOT}/src/authoring/source_package_prepare.c"
    "${CHIBI_SDK_ROOT}/src/authoring/package_tests.c")
add_library(Chibi::source_catalog ALIAS chibi_source_catalog)
set_target_properties(chibi_source_catalog PROPERTIES EXPORT_NAME source_catalog)
chibi_sdk_headers(chibi_source_catalog PUBLIC authoring/language_catalog.h
    authoring/source_catalog.h authoring/source_collection.h authoring/source_asset.h authoring/source_completion.h
    authoring/source_packages.h authoring/source_package_io.h authoring/source_package_snapshot.h authoring/source_package_files.h
    authoring/source_package_exports.h authoring/source_package_prepare.h authoring/package_tests.h)
foreach(header source_catalog_snapshot source_prepare_internal source_package_syntax source_package_paths source_package_routes)
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/${header}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/source_catalog/authoring/${header}.h" COPYONLY)
endforeach()
configure_file("${CHIBI_SDK_ROOT}/src/capabilities/catalog_index.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/source_catalog/capabilities/catalog_index.h" COPYONLY)
target_include_directories(chibi_source_catalog PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/private/source_catalog"
    "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_source_catalog PUBLIC chibi_authoring chibi_input_map
    chibi_prototype_document
    PRIVATE chibi_asset_text chibi_ui_document chibi_json chibi_tasks)

add_library(chibi_program STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/program.c"
    "${CHIBI_SDK_ROOT}/src/authoring/program_service.c")
add_library(Chibi::program ALIAS chibi_program)
set_target_properties(chibi_program PROPERTIES EXPORT_NAME program)
chibi_sdk_headers(chibi_program PUBLIC authoring/program.h authoring/program_service.h)
target_link_libraries(chibi_program PUBLIC chibi_source_catalog chibi_native_capabilities chibi_behavior_runtime)

add_library(chibi_source_presentation STATIC)
foreach(unit graph_expression presentation_schema source_presentation source_presentation_graph source_presentation_anchors
    source_presentation_values source_presentation_flow source_presentation_layout
    source_presentation_details)
    target_sources(chibi_source_presentation PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/${unit}.c")
endforeach()
add_library(Chibi::source_presentation ALIAS chibi_source_presentation)
set_target_properties(chibi_source_presentation PROPERTIES EXPORT_NAME source_presentation)
chibi_sdk_headers(chibi_source_presentation PUBLIC authoring/graph_expression.h authoring/presentation_schema.h authoring/source_presentation.h)
foreach(header source_presentation_internal source_presentation_anchors source_presentation_details)
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/${header}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/source_presentation/authoring/${header}.h" COPYONLY)
endforeach()
target_include_directories(chibi_source_presentation PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/private/source_presentation" "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_source_presentation PUBLIC chibi_authoring chibi_graph_model chibi_text_source
    PRIVATE chibi_json chibi_graph_authoring)


add_library(chibi_graph_catalog STATIC "${CHIBI_SDK_ROOT}/src/authoring/graph_catalog.c")
add_library(Chibi::graph_catalog ALIAS chibi_graph_catalog)
set_target_properties(chibi_graph_catalog PROPERTIES EXPORT_NAME graph_catalog)
chibi_sdk_headers(chibi_graph_catalog PUBLIC authoring/graph_catalog.h)
target_link_libraries(chibi_graph_catalog PUBLIC chibi_graph_model chibi_native_capabilities chibi_ecs
    PRIVATE chibi_source_presentation chibi_graph_compilation)

list(APPEND CHIBI_SDK_TARGETS chibi_source_catalog chibi_program chibi_source_presentation chibi_graph_catalog)

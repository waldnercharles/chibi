add_library(chibi_graph_model STATIC
    "${CHIBI_SDK_ROOT}/src/graph/model/graph.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/behavior_asset.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/local.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/pin_conv.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/registry.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/registry_validate.c"
    "${CHIBI_SDK_ROOT}/src/graph/model/value_copy.c")
add_library(Chibi::graph_model ALIAS chibi_graph_model)
set_target_properties(chibi_graph_model PROPERTIES EXPORT_NAME graph_model)
chibi_sdk_headers(chibi_graph_model PUBLIC graph/model/graph.h graph/model/behavior_asset.h
    graph/model/local.h graph/model/pin_conv.h graph/model/registry.h graph/model/value_copy.h
    graph/model/node.h graph/model/pin.h)
configure_file("${CHIBI_SDK_ROOT}/src/graph/model/graph_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/graph/graph/model/graph_internal.h" COPYONLY)
target_include_directories(chibi_graph_model PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/graph")
target_link_libraries(chibi_graph_model PUBLIC chibi_types chibi_execution_contract PRIVATE chibi_asset_values)
list(APPEND CHIBI_SDK_TARGETS chibi_graph_model)

# Graph document editing and native KDL authoring; no behavioral executor.
add_library(chibi_graph_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/graph/authoring/constant.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/optimize.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/optimize_cleanup.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/optimize_set.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/align.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/arrange.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/arrange_path.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/arrange_route.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/arrange_score.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/asset_edit.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/describe.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/expand.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/find.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/graph_edit.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/layout.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/linked.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/local_edit.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/pin_edit.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/reactive.c"
    "${CHIBI_SDK_ROOT}/src/graph/authoring/reactive_presentation.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/events.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/extract.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/graph_signature.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/library.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_clipboard.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_implied.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_local.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_refs.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_value.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/serialize_write.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/component_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/conv_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/math_expression.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/math_expression_convert.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/behavior_expression_format.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/state_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/struct_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/flow_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/event_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/reroute_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/math_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/const_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/compare_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/vector_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/fmt_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/array_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/switch_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/set_map_nodes.c"
)
add_library(Chibi::graph_authoring ALIAS chibi_graph_authoring)
set_target_properties(chibi_graph_authoring PROPERTIES EXPORT_NAME graph_authoring)
chibi_sdk_headers(chibi_graph_authoring PUBLIC
    graph/nodes/array_nodes.h graph/nodes/set_map_nodes.h
    graph/authoring/constant.h graph/authoring/optimize.h graph/nodes/core_nodes.h graph/nodes/fmt_nodes.h
    graph/nodes/math_nodes.h graph/nodes/compare_nodes.h graph/nodes/vector_nodes.h graph/nodes/state_nodes.h
    graph/authoring/align.h
    graph/authoring/arrange.h
    graph/authoring/arrange_path.h
    graph/authoring/arrange_score.h
    graph/authoring/asset_edit.h
    graph/authoring/describe.h
    graph/authoring/expand.h
    graph/authoring/find.h
    graph/authoring/graph_edit.h
    graph/authoring/layout.h
    graph/authoring/linked.h
    graph/authoring/local_edit.h
    graph/authoring/pin_edit.h
    graph/document/events.h
    graph/document/extract.h
    graph/document/graph_signature.h
    graph/document/library.h
    graph/document/serialize.h
    graph/model/reactive.h
    graph/model/reactive_presentation.h
    graph/nodes/component_nodes.h
    graph/nodes/conv_nodes.h
    graph/nodes/behavior_expression.h
    graph/nodes/compare_nodes.h
    graph/nodes/math_nodes.h graph/nodes/compare_nodes.h graph/nodes/vector_nodes.h graph/nodes/state_nodes.h
    graph/nodes/vector_nodes.h
    graph/nodes/state_nodes.h)
foreach(header IN ITEMS graph/model/graph_internal.h graph/model/native_definition.h
    graph/authoring/arrange_internal.h graph/authoring/optimize_internal.h graph/document/serialize_internal.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}"
        "${CMAKE_CURRENT_BINARY_DIR}/private/graph/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_graph_authoring PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/graph")
target_link_libraries(chibi_graph_authoring PUBLIC chibi_graph_model chibi_ecs chibi_events chibi_asset_values
    PRIVATE chibi_asset_text chibi_value_document chibi_conversion_capabilities chibi_capabilities
    chibi_core_capabilities chibi_math_capabilities chibi_arithmetic_capabilities
    chibi_comparison_capabilities chibi_vector_capabilities chibi_text_capabilities chibi_state_capabilities
    chibi_array_capabilities chibi_selection_capabilities chibi_set_capabilities)
list(APPEND CHIBI_SDK_TARGETS chibi_graph_authoring)

# Graph shader/model/field preparation never links behavioral graph execution.
include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/models.cmake")
add_library(chibi_graph_compilation STATIC
    "${CHIBI_SDK_ROOT}/src/graph/shader/shader_adapter.c"
    "${CHIBI_SDK_ROOT}/src/graph/shader/shader_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_anchors.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_authoring.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_bounds.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_bounds_prepare.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_compile.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_constructive.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_expand.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_hit.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_materials.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_nodes.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_numeric.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_parameters.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_profile_paths.c"
    "${CHIBI_SDK_ROOT}/src/graph/modeling/model_profiles.c"
    "${CHIBI_SDK_ROOT}/src/graph/compiler/report.c"
    "${CHIBI_SDK_ROOT}/src/graph/compiler/validation.c"
    "${CHIBI_SDK_ROOT}/src/graph/compiler/structure.c"
    "${CHIBI_SDK_ROOT}/src/graph/compiler/field_program.c"
    "${CHIBI_SDK_ROOT}/src/graph/compiler/material_field.c"
    "${CHIBI_SDK_ROOT}/src/graph/document/problems.c"
    "${CHIBI_SDK_ROOT}/src/graph/nodes/field_nodes.c")
add_library(Chibi::graph_compilation ALIAS chibi_graph_compilation)
set_target_properties(chibi_graph_compilation PROPERTIES EXPORT_NAME graph_compilation)
chibi_sdk_headers(chibi_graph_compilation PUBLIC
    graph/compiler/validation.h
    graph/compiler/field_program.h
    graph/compiler/material_field.h
    graph/document/problems.h
    graph/shader/shader_graph.h
    graph/nodes/field_nodes.h
    graph/modeling/model_compile.h
    graph/modeling/model_authoring.h
    graph/modeling/model_profile_paths.h)
foreach(header IN ITEMS
    graph/compiler/structure_internal.h
    graph/modeling/model_internal.h
    graph/modeling/model_expand_internal.h
    graph/modeling/model_expand.h
    graph/modeling/model_numeric.h
    runtime/models/model_program_internal.h
    runtime/models/model_bounds.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}"
        "${CMAKE_CURRENT_BINARY_DIR}/private/graph/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_graph_compilation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/graph")
target_link_libraries(chibi_graph_compilation PUBLIC chibi_graph_authoring
    chibi_shader_runtime chibi_model_runtime chibi_fields chibi_field_catalog
    PRIVATE chibi_field_capabilities)
list(APPEND CHIBI_SDK_TARGETS chibi_graph_compilation)

# Optional built-in component and entity packages. Their include paths are the
# same in Studio and installed consumers; neither can see the product source root.
include("${CMAKE_CURRENT_LIST_DIR}/values.cmake")

# Match WASM's separate multiply/add rounding in these native kernels.
# Emitters are owned by the engine target, which applies the same policy.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        "${CHIBI_SDK_ROOT}/src/runtime/visual_transform.c"
        "${CHIBI_SDK_ROOT}/src/runtime/transform_propagation.c"
        "${CHIBI_SDK_ROOT}/src/runtime/particles.c"
        PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

# Product builds share their existing generation. Standalone SDK builds use an
# explicit native host tool, including when the target compiler is Emscripten.
if(NOT CHIBI_NATIVE_TYPES_SOURCE)
    set(CHIBI_REFLECT_EXECUTABLE "" CACHE FILEPATH "Native Chibi reflection generator")
    if(NOT EXISTS "${CHIBI_REFLECT_EXECUTABLE}")
        message(FATAL_ERROR "Native SDK packages require CHIBI_REFLECT_EXECUTABLE")
    endif()
    set(CHIBI_NATIVE_TYPES_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/native_types.c")
    set(CHIBI_NATIVE_BINDINGS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/native_capabilities.c")
    set(native_manifest "${CHIBI_SDK_ROOT}/src/runtime/component_ids.tsv")
    set(native_args --include "${CHIBI_SDK_ROOT}/src"
        --include "${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/include" --manifest "${native_manifest}")
    set(CHIBI_REFLECT_RESOURCE_INCLUDE_DIR "" CACHE PATH
        "C builtin headers matching the host reflection tool's libclang")
    if(CHIBI_REFLECT_RESOURCE_INCLUDE_DIR)
        list(APPEND native_args --include "${CHIBI_REFLECT_RESOURCE_INCLUDE_DIR}")
    endif()
    set(native_sysroot "${CMAKE_OSX_SYSROOT}")
    if(CMAKE_HOST_APPLE AND NOT native_sysroot)
        execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE native_sysroot
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endif()
    if(native_sysroot)
        list(APPEND native_args --sysroot "${native_sysroot}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_command(OUTPUT "${CHIBI_NATIVE_TYPES_SOURCE}.stamp"
        BYPRODUCTS "${CHIBI_NATIVE_TYPES_SOURCE}" "${CHIBI_NATIVE_BINDINGS_SOURCE}"
        COMMAND "${CHIBI_REFLECT_EXECUTABLE}" "${CHIBI_SDK_ROOT}/src/runtime/native_nodes.c"
            --prefix "" --types-output "${CHIBI_NATIVE_TYPES_SOURCE}"
            --runtime-output "${CHIBI_NATIVE_BINDINGS_SOURCE}" ${native_args}
        COMMAND ${CMAKE_COMMAND} -E touch "${CHIBI_NATIVE_TYPES_SOURCE}.stamp"
        DEPENDS "${CHIBI_REFLECT_EXECUTABLE}" "${native_manifest}"
            "${CHIBI_SDK_ROOT}/src/runtime/native_nodes.c"
            "${CHIBI_SDK_ROOT}/src/runtime/components.h"
            "${CHIBI_SDK_ROOT}/src/runtime/interface.h"
            "${CHIBI_SDK_ROOT}/src/types/annotate.h"
        VERBATIM)
    add_custom_target(chibi_native_reflection DEPENDS "${CHIBI_NATIVE_TYPES_SOURCE}.stamp")
endif()
set_source_files_properties("${CHIBI_NATIVE_TYPES_SOURCE}" "${CHIBI_NATIVE_BINDINGS_SOURCE}" PROPERTIES GENERATED TRUE)
add_library(chibi_native_types STATIC "${CHIBI_NATIVE_TYPES_SOURCE}")
add_library(Chibi::native_types ALIAS chibi_native_types)
set_target_properties(chibi_native_types PROPERTIES EXPORT_NAME native_types)
chibi_sdk_headers(chibi_native_types PUBLIC runtime/components.h runtime/component_catalog.h)
target_link_libraries(chibi_native_types PUBLIC chibi_ecs chibi_math chibi_execution_contract)
if(TARGET chibi_native_reflection)
    add_dependencies(chibi_native_types chibi_native_reflection)
endif()

add_library(chibi_tilemap_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/tilemap.c")
add_library(Chibi::tilemap_runtime ALIAS chibi_tilemap_runtime)
set_target_properties(chibi_tilemap_runtime PROPERTIES EXPORT_NAME tilemap_runtime)
chibi_sdk_headers(chibi_tilemap_runtime PUBLIC runtime/tilemap.h)
target_link_libraries(chibi_tilemap_runtime PUBLIC chibi_math PRIVATE chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_tilemap_runtime)

add_library(chibi_spatial_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/systems/access.c"
    "${CHIBI_SDK_ROOT}/src/runtime/visual_transform.c"
    "${CHIBI_SDK_ROOT}/src/runtime/visual_parts.c"
    "${CHIBI_SDK_ROOT}/src/runtime/transform_propagation.c")
add_library(Chibi::spatial_runtime ALIAS chibi_spatial_runtime)
set_target_properties(chibi_spatial_runtime PROPERTIES EXPORT_NAME spatial_runtime)
chibi_sdk_headers(chibi_spatial_runtime PUBLIC runtime/visual_transform.h runtime/visual_parts.h
    runtime/transform_propagation.h)
target_link_libraries(chibi_spatial_runtime PUBLIC chibi_native_types)
add_library(chibi_native_bindings STATIC "${CHIBI_NATIVE_BINDINGS_SOURCE}")
add_library(Chibi::native_bindings ALIAS chibi_native_bindings)
set_target_properties(chibi_native_bindings PROPERTIES EXPORT_NAME native_bindings)
chibi_sdk_headers(chibi_native_bindings PUBLIC runtime/generated_capabilities.h)
target_link_libraries(chibi_native_bindings PUBLIC chibi_native_types chibi_native_capabilities
    PRIVATE chibi_spatial_runtime)
if(TARGET chibi_native_reflection)
    add_dependencies(chibi_native_bindings chibi_native_reflection)
endif()
list(APPEND CHIBI_SDK_TARGETS chibi_spatial_runtime chibi_native_bindings)

add_library(chibi_entity_runtime STATIC
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/entities.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/entities_source.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/entity_query.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/entities_definition.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/tilemap_definition.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/scene_flow.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/scene_flow_definition.c")
add_library(Chibi::entity_runtime ALIAS chibi_entity_runtime)
set_target_properties(chibi_entity_runtime PROPERTIES EXPORT_NAME entity_runtime)
chibi_sdk_headers(chibi_entity_runtime PUBLIC runtime/behaviors/entities.h
    runtime/behaviors/entities_native.h runtime/behaviors/entities_source.h
    runtime/behaviors/entities_definition.h runtime/behaviors/entity_query.h runtime/behaviors/tilemap_definition.h
    runtime/behaviors/scene_flow.h runtime/behaviors/scene_flow_definition.h)
target_link_libraries(chibi_entity_runtime PUBLIC chibi_native_types chibi_execution
    chibi_scene_runtime chibi_behavior_runtime chibi_native_capabilities PRIVATE chibi_spatial_runtime chibi_tilemap_runtime)
add_library(chibi_audio_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/audio.c"
    "${CHIBI_SDK_ROOT}/src/runtime/audio_analysis.c"
    "${CHIBI_SDK_ROOT}/src/runtime/audio_definition.c")
add_library(Chibi::audio_runtime ALIAS chibi_audio_runtime)
set_target_properties(chibi_audio_runtime PROPERTIES EXPORT_NAME audio_runtime)
chibi_sdk_headers(chibi_audio_runtime PUBLIC runtime/audio.h runtime/audio_definition.h runtime/audio_analysis.h)
target_link_libraries(chibi_audio_runtime PUBLIC chibi_native_capabilities)
add_library(chibi_url_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/url.c"
    "${CHIBI_SDK_ROOT}/src/runtime/url_definition.c")
add_library(Chibi::url_runtime ALIAS chibi_url_runtime)
set_target_properties(chibi_url_runtime PROPERTIES EXPORT_NAME url_runtime)
chibi_sdk_headers(chibi_url_runtime PUBLIC runtime/url.h runtime/url_definition.h)
target_link_libraries(chibi_url_runtime PUBLIC chibi_native_capabilities PRIVATE chibi_support)
add_library(chibi_particle_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/particles.c"
    "${CHIBI_SDK_ROOT}/src/runtime/particles_native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/particles_definition.c")
add_library(Chibi::particle_runtime ALIAS chibi_particle_runtime)
set_target_properties(chibi_particle_runtime PROPERTIES EXPORT_NAME particle_runtime)
chibi_sdk_headers(chibi_particle_runtime PUBLIC runtime/particles.h runtime/particles_native.h
    runtime/particles_definition.h)
target_link_libraries(chibi_particle_runtime PUBLIC chibi_native_types chibi_native_capabilities)
add_library(chibi_save_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/save.c"
    "${CHIBI_SDK_ROOT}/src/runtime/save_native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/save_definition.c")
add_library(Chibi::save_runtime ALIAS chibi_save_runtime)
set_target_properties(chibi_save_runtime PROPERTIES EXPORT_NAME save_runtime)
chibi_sdk_headers(chibi_save_runtime PUBLIC runtime/save.h runtime/save_native.h runtime/save_definition.h)
target_link_libraries(chibi_save_runtime PUBLIC chibi_native_capabilities PRIVATE chibi_json)
add_library(chibi_ui_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/ui.c"
    "${CHIBI_SDK_ROOT}/src/runtime/ui_native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/ui_definition.c")
add_library(Chibi::ui_runtime ALIAS chibi_ui_runtime)
set_target_properties(chibi_ui_runtime PROPERTIES EXPORT_NAME ui_runtime)
chibi_sdk_headers(chibi_ui_runtime PUBLIC runtime/ui.h runtime/ui_native.h runtime/ui_definition.h)
target_link_libraries(chibi_ui_runtime PUBLIC chibi_asset_values chibi_native_capabilities PRIVATE chibi_value_document chibi_math)
add_library(chibi_input_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/engine_input.c"
    "${CHIBI_SDK_ROOT}/src/runtime/input_definition.c" "${CHIBI_SDK_ROOT}/src/runtime/source_input.c")
add_library(Chibi::input_runtime ALIAS chibi_input_runtime)
set_target_properties(chibi_input_runtime PROPERTIES EXPORT_NAME input_runtime)
chibi_sdk_headers(chibi_input_runtime PUBLIC runtime/engine_input.h runtime/input_definition.h
    runtime/input_timeline.h runtime/source_input.h)
target_link_libraries(chibi_input_runtime PUBLIC chibi_input_action chibi_native_types chibi_native_capabilities chibi_execution_contract chibi_behavior_runtime PRIVATE chibi_events
    chibi_spatial_runtime)
target_include_directories(chibi_save_runtime PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
list(APPEND CHIBI_SDK_TARGETS chibi_native_types chibi_entity_runtime chibi_audio_runtime chibi_url_runtime
    chibi_particle_runtime chibi_save_runtime chibi_ui_runtime chibi_input_runtime)

include(materials.cmake)
include(physics.cmake)
include(engine.cmake)

add_library(chibi_host_native_default STATIC "${CHIBI_SDK_ROOT}/src/host/native_default.c")
add_library(Chibi::host_native_default ALIAS chibi_host_native_default)
set_target_properties(chibi_host_native_default PROPERTIES EXPORT_NAME host_native_default)
chibi_sdk_headers(chibi_host_native_default PUBLIC host/native_default.h)
target_link_libraries(chibi_host_native_default PUBLIC chibi_host_native_catalog PRIVATE
    chibi_core_capabilities chibi_array_capabilities chibi_field_capabilities chibi_state_capabilities
    chibi_selection_capabilities chibi_set_capabilities chibi_comparison_capabilities
    chibi_conversion_capabilities chibi_text_capabilities chibi_native_bindings chibi_entity_runtime
    chibi_audio_runtime chibi_url_runtime chibi_vector_capabilities chibi_math_capabilities
    chibi_arithmetic_capabilities chibi_particle_runtime chibi_save_runtime chibi_ui_runtime
    chibi_physics_runtime chibi_physics3d_runtime chibi_material_runtime chibi_visibility_runtime chibi_input_runtime)
list(APPEND CHIBI_SDK_TARGETS chibi_host_native_default)

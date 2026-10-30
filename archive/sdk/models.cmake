# Prepared model evaluation is independent of source graphs and the renderer.
include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
if(NOT TARGET chibi_model_runtime)
    add_library(chibi_model_runtime STATIC
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_lifecycle.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/prepared.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_parameters.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_bounds.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_finite.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_frames.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_hit.c"
        "${CHIBI_SDK_ROOT}/src/runtime/models/model_inspection.c")
    add_library(Chibi::model_runtime ALIAS chibi_model_runtime)
    set_target_properties(chibi_model_runtime PROPERTIES EXPORT_NAME model_runtime)
    chibi_sdk_headers(chibi_model_runtime PUBLIC
        runtime/models/model_program.h runtime/models/model_inspection.h runtime/models/prepared.h)
    set(model_private "${CMAKE_CURRENT_BINARY_DIR}/private/model_runtime")
    foreach(header runtime/models/model_program_internal.h runtime/models/model_bounds.h runtime/shaders/prepared_internal.h)
        configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${model_private}/${header}" COPYONLY)
    endforeach()
    target_include_directories(chibi_model_runtime PRIVATE "${model_private}")
    target_link_libraries(chibi_model_runtime PUBLIC chibi_shader_runtime chibi_types PRIVATE chibi_support)
    if(UNIX)
        target_link_libraries(chibi_model_runtime PRIVATE m)
    endif()
    list(APPEND CHIBI_SDK_TARGETS chibi_model_runtime)
endif()

# Geometry snapshots and CPU baking require only the optional math headers.
# A compiler-only SDK can use model_runtime without the native math package.
if(TARGET chibi_math AND NOT TARGET chibi_mesh_runtime)
    add_library(chibi_mesh_runtime STATIC
        "${CHIBI_SDK_ROOT}/src/runtime/geometry/mesh_asset.c"
        "${CHIBI_SDK_ROOT}/src/runtime/geometry/mesh_pick.c"
        "${CHIBI_SDK_ROOT}/src/runtime/geometry/mesh_prepared.c")
    add_library(Chibi::mesh_runtime ALIAS chibi_mesh_runtime)
    set_target_properties(chibi_mesh_runtime PROPERTIES EXPORT_NAME mesh_runtime)
    chibi_sdk_headers(chibi_mesh_runtime PUBLIC runtime/geometry/mesh_asset.h runtime/geometry/mesh_pick.h
        runtime/geometry/mesh_prepared.h)
    target_link_libraries(chibi_mesh_runtime PUBLIC chibi_math PRIVATE chibi_support chibi_types)
    list(APPEND CHIBI_SDK_TARGETS chibi_mesh_runtime)
endif()

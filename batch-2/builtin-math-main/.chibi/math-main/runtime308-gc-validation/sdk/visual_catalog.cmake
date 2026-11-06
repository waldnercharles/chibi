# CPU visual definitions are shared by offline preparation and display hosts.
add_library(chibi_visual_catalog STATIC
    "${CHIBI_SDK_ROOT}/src/render/shader_catalog.c"
    "${CHIBI_SDK_ROOT}/src/render/shader_values.c"
    "${CHIBI_SDK_ROOT}/src/render/scene3d_parts.c"
    "${CHIBI_SDK_ROOT}/src/render/shader_parameters.c")
add_library(Chibi::visual_catalog ALIAS chibi_visual_catalog)
set_target_properties(chibi_visual_catalog PROPERTIES EXPORT_NAME visual_catalog)
chibi_sdk_headers(chibi_visual_catalog PUBLIC render/shader_catalog.h render/shader_catalog_records.h render/shader_values.h
    render/scene3d_parts.h render/scene3d_parts_table.h render/catalog_publication.h render/shader_parameters.h)
set(visual_private "${CMAKE_CURRENT_BINARY_DIR}/private/visual_catalog")
foreach(header render/shader_catalog_internal.h)
    configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${visual_private}/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_visual_catalog PRIVATE "${visual_private}")
target_link_libraries(chibi_visual_catalog PUBLIC chibi_native_types chibi_shader_runtime
    chibi_model_runtime chibi_mesh_runtime chibi_native_capabilities
    PRIVATE chibi_image_catalog chibi_types chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_visual_catalog)

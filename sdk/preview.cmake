# Studio preview composes source editing with the desktop runtime.
add_library(chibi_project_preparation STATIC)
foreach(unit preview preview_runtime preview_source preview_source_lifecycle preview_source_publish prototype_preview
    behavior_loader graph_loader material_attach prototype_attach render_preview source_apply studio_schema ui_loader ui_check ui_events_adapter engine_preview application_catalog)
    target_sources(chibi_project_preparation PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/${unit}.c")
endforeach()
add_library(Chibi::project_preparation ALIAS chibi_project_preparation)
set_target_properties(chibi_project_preparation PROPERTIES EXPORT_NAME project_preparation)
chibi_sdk_headers(chibi_project_preparation PUBLIC authoring/preview.h authoring/preview_source.h
    authoring/ui_loader.h authoring/ui_check.h authoring/render_preview.h
    authoring/preview_runtime.h authoring/prototype_preview.h authoring/behavior_loader.h
    authoring/source_apply.h authoring/studio_project.h authoring/ui_events_adapter.h
    authoring/engine_preview.h authoring/application_catalog.h)
foreach(header preview_source_internal graph_sources graph_loader_internal)
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/${header}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/project_preparation/authoring/${header}.h" COPYONLY)
endforeach()
target_include_directories(chibi_project_preparation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/project_preparation")
target_link_libraries(chibi_project_preparation PUBLIC chibi_project_compilation chibi_source_presentation
    chibi_engine chibi_ui_host chibi_renderer
    PRIVATE chibi_studio_catalog chibi_host_native_default
    chibi_asset_authoring chibi_asset_text chibi_project_document chibi_ui_authoring chibi_image_preparation
    chibi_declared_components)
list(APPEND CHIBI_SDK_TARGETS chibi_project_preparation)

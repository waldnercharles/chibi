# Studio preview composes source editing with the desktop runtime.
add_library(chibi_project_preparation STATIC)
foreach(unit studio_schema)
    target_sources(chibi_project_preparation PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/${unit}.c")
endforeach()
add_library(Chibi::project_preparation ALIAS chibi_project_preparation)
set_target_properties(chibi_project_preparation PROPERTIES EXPORT_NAME project_preparation)
chibi_sdk_headers(chibi_project_preparation PUBLIC authoring/studio_project.h)
target_link_libraries(chibi_project_preparation PUBLIC chibi_project_compilation
    chibi_engine chibi_ui_host chibi_renderer
    PRIVATE chibi_host_native_default
    chibi_ui_authoring chibi_image_preparation
    chibi_declared_components)
list(APPEND CHIBI_SDK_TARGETS chibi_project_preparation)

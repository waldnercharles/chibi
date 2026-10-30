include("${CMAKE_CURRENT_LIST_DIR}/ui_document.cmake")

add_library(chibi_ui_authoring STATIC "${CHIBI_SDK_ROOT}/src/assets/ui_events.c")
add_library(Chibi::ui_authoring ALIAS chibi_ui_authoring)
set_target_properties(chibi_ui_authoring PROPERTIES EXPORT_NAME ui_authoring)
chibi_sdk_headers(chibi_ui_authoring PUBLIC assets/ui_events.h)
target_link_libraries(chibi_ui_authoring PUBLIC chibi_text_source chibi_ui_document)

list(APPEND CHIBI_SDK_TARGETS chibi_ui_authoring)

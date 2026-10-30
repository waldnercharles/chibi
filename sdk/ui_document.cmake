# Runtime document composition and optional in-memory text overlays do not need
# the language compiler or UI grammar parsers.
if(TARGET chibi_ui_document)
    return()
endif()
add_library(chibi_text_source STATIC
    "${CHIBI_SDK_ROOT}/src/support/file_watch.c"
    "${CHIBI_SDK_ROOT}/src/support/text_file.c"
    "${CHIBI_SDK_ROOT}/src/support/text_overlay.c")
add_library(Chibi::text_source ALIAS chibi_text_source)
set_target_properties(chibi_text_source PROPERTIES EXPORT_NAME text_source)
chibi_sdk_headers(chibi_text_source PUBLIC support/file_watch.h support/text_file.h
    support/text_overlay.h)
target_link_libraries(chibi_text_source PUBLIC chibi_support chibi_text_search)

add_library(chibi_ui_document STATIC
    "${CHIBI_SDK_ROOT}/src/assets/ui_document.c"
    "${CHIBI_SDK_ROOT}/src/support/rcss.c")
add_library(Chibi::ui_document ALIAS chibi_ui_document)
set_target_properties(chibi_ui_document PROPERTIES EXPORT_NAME ui_document)
chibi_sdk_headers(chibi_ui_document PUBLIC assets/ui_document.h support/rcss.h)
target_link_libraries(chibi_ui_document PUBLIC chibi_text_source chibi_ui_document_values)


list(APPEND CHIBI_SDK_TARGETS chibi_text_source chibi_ui_document)

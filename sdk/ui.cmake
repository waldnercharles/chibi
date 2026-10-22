include("${CMAKE_CURRENT_LIST_DIR}/ui_document.cmake")

set(ui_css_root "${CHIBI_THIRD_PARTY_DIR}/source/tree_sitter_css")
set(ui_html_root "${CHIBI_THIRD_PARTY_DIR}/source/tree_sitter_html")
add_library(chibi_ui_syntax STATIC
    "${ui_css_root}/src/parser.c" "${ui_css_root}/src/scanner.c"
    "${ui_html_root}/src/parser.c" "${ui_html_root}/src/scanner.c")
add_library(Chibi::ui_syntax ALIAS chibi_ui_syntax)
set_target_properties(chibi_ui_syntax PROPERTIES EXPORT_NAME ui_syntax)
set_source_files_properties("${ui_css_root}/src/parser.c" "${ui_css_root}/src/scanner.c"
    PROPERTIES INCLUDE_DIRECTORIES "${ui_css_root}/src")
set_source_files_properties("${ui_html_root}/src/parser.c" "${ui_html_root}/src/scanner.c"
    PROPERTIES INCLUDE_DIRECTORIES "${ui_html_root}/src")
target_link_libraries(chibi_ui_syntax PRIVATE chibi_syntax_runtime)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_ui_syntax PRIVATE -w)
endif()
install(FILES "${ui_css_root}/LICENSE"
    DESTINATION share/chibi/licenses/tree-sitter-css)
install(FILES "${ui_html_root}/LICENSE"
    DESTINATION share/chibi/licenses/tree-sitter-html)

configure_file("${CHIBI_SDK_ROOT}/src/assets/ui_source_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/ui_authoring/assets/ui_source_internal.h" COPYONLY)
add_library(chibi_ui_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/assets/ui_events.c"
    "${CHIBI_SDK_ROOT}/src/assets/ui_source.c"
    "${CHIBI_SDK_ROOT}/src/assets/ui_source_complete.c"
    "${CHIBI_SDK_ROOT}/src/assets/ui_source_edit.c"
    "${CHIBI_SDK_ROOT}/src/assets/ui_source_format.c"
    "${CHIBI_SDK_ROOT}/src/assets/ui_source_hover.c")
target_sources(chibi_ui_authoring PRIVATE "${CHIBI_SDK_ROOT}/src/assets/ui_source_resources.c")
add_library(Chibi::ui_authoring ALIAS chibi_ui_authoring)
set_target_properties(chibi_ui_authoring PROPERTIES EXPORT_NAME ui_authoring)
chibi_sdk_headers(chibi_ui_authoring PUBLIC assets/ui_source.h assets/ui_events.h)
target_include_directories(chibi_ui_authoring PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/private/ui_authoring")
target_link_libraries(chibi_ui_authoring PUBLIC chibi_text_source chibi_ui_document
    PRIVATE chibi_ui_syntax chibi_syntax_runtime)

list(APPEND CHIBI_SDK_TARGETS chibi_ui_syntax chibi_ui_authoring)

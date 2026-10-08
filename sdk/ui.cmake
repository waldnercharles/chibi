include("${CMAKE_CURRENT_LIST_DIR}/ui_document.cmake")

FetchContent_Declare(chibi_ui_css
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-css.git
    GIT_TAG v0.25.0 GIT_SHALLOW TRUE SOURCE_SUBDIR chibi-sources-only)
FetchContent_Declare(chibi_ui_html
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-html.git
    GIT_TAG v0.23.2 GIT_SHALLOW TRUE SOURCE_SUBDIR chibi-sources-only)
FetchContent_MakeAvailable(chibi_ui_css chibi_ui_html)
# The pinned scanners use an unspecified-argument C declaration for create(),
# while their parsers and the runtime declare create(void). Make the callback
# type explicit in configured copies so UBSan sees the same function contract.
function(chibi_ui_scanner grammar source output)
    set(original "${source}/src/scanner.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" contents)
    string(REGEX MATCHALL "void \\*tree_sitter_${grammar}_external_scanner_create\\(\\)"
        matches "${contents}")
    list(LENGTH matches count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Pinned ${grammar} scanner create signature changed")
    endif()
    string(REPLACE "void *tree_sitter_${grammar}_external_scanner_create()"
        "void *tree_sitter_${grammar}_external_scanner_create(void)" contents "${contents}")
    set(path "${CMAKE_CURRENT_BINARY_DIR}/private/ui_syntax/${grammar}/scanner.c")
    get_filename_component(directory "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    set(previous "")
    if(EXISTS "${path}")
        file(READ "${path}" previous)
    endif()
    if(NOT contents STREQUAL previous)
        file(WRITE "${path}" "${contents}")
    endif()
    set(${output} "${path}" PARENT_SCOPE)
endfunction()
chibi_ui_scanner(css "${chibi_ui_css_SOURCE_DIR}" CHIBI_UI_CSS_SCANNER)
chibi_ui_scanner(html "${chibi_ui_html_SOURCE_DIR}" CHIBI_UI_HTML_SCANNER)
add_library(chibi_ui_syntax STATIC
    "${chibi_ui_css_SOURCE_DIR}/src/parser.c"
    "${CHIBI_UI_CSS_SCANNER}"
    "${chibi_ui_html_SOURCE_DIR}/src/parser.c"
    "${CHIBI_UI_HTML_SCANNER}")
add_library(Chibi::ui_syntax ALIAS chibi_ui_syntax)
set_target_properties(chibi_ui_syntax PROPERTIES EXPORT_NAME ui_syntax)
set_source_files_properties(
    "${chibi_ui_css_SOURCE_DIR}/src/parser.c"
    "${CHIBI_UI_CSS_SCANNER}"
    PROPERTIES INCLUDE_DIRECTORIES "${chibi_ui_css_SOURCE_DIR}/src")
set_source_files_properties(
    "${chibi_ui_html_SOURCE_DIR}/src/parser.c"
    "${CHIBI_UI_HTML_SCANNER}"
    PROPERTIES INCLUDE_DIRECTORIES "${chibi_ui_html_SOURCE_DIR}/src")
target_link_libraries(chibi_ui_syntax PRIVATE chibi_syntax_runtime)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_ui_syntax PRIVATE -w)
endif()
install(FILES "${chibi_ui_css_SOURCE_DIR}/LICENSE"
    DESTINATION share/chibi/licenses/tree-sitter-css)
install(FILES "${chibi_ui_html_SOURCE_DIR}/LICENSE"
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

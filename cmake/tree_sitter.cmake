# tree-sitter parses ui assets for the language service (src/assets/ui_source.c):
# the HTML grammar reads RML markup and the CSS grammar reads RCSS. Everything
# is plain C, so it builds the same natively and under Emscripten.
#
# SOURCE_SUBDIR names a folder without a CMakeLists.txt, so FetchContent only
# downloads the sources and the grammars' own targets stay out of the build.
macro(chibi_tree_sitter_source name url sha256)
    FetchContent_Declare(${name}
        URL ${url}
        URL_HASH SHA256=${sha256}
        SOURCE_SUBDIR chibi-sources-only
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(${name})
endmacro()

chibi_tree_sitter_source(tree_sitter
    https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.25.10.tar.gz
    ad5040537537012b16ef6e1210a572b927c7cdc2b99d1ee88d44a7dcdc3ff44c)
chibi_tree_sitter_source(tree_sitter_css
    https://github.com/tree-sitter/tree-sitter-css/archive/refs/tags/v0.25.0.tar.gz
    03965344d8c0435dc54fb45b281578420bb7db8b99df4d34e7e74105a274cb79)
chibi_tree_sitter_source(tree_sitter_html
    https://github.com/tree-sitter/tree-sitter-html/archive/refs/tags/v0.23.2.tar.gz
    21fa4f2d4dcb890ef12d09f4979a0007814f67f1c7294a9b17b0108a09e45ef7)

# The runtime is one translation unit, lib.c; each grammar is its generated
# parser and hand-written scanner. Their own warnings are not ours to fix.
add_library(chibi_tree_sitter STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
    ${tree_sitter_css_SOURCE_DIR}/src/parser.c
    ${tree_sitter_css_SOURCE_DIR}/src/scanner.c
    ${tree_sitter_html_SOURCE_DIR}/src/parser.c
    ${tree_sitter_html_SOURCE_DIR}/src/scanner.c)
target_include_directories(chibi_tree_sitter SYSTEM PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)
target_include_directories(chibi_tree_sitter PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src)
set_source_files_properties(
    ${tree_sitter_css_SOURCE_DIR}/src/parser.c ${tree_sitter_css_SOURCE_DIR}/src/scanner.c
    PROPERTIES INCLUDE_DIRECTORIES ${tree_sitter_css_SOURCE_DIR}/src)
set_source_files_properties(
    ${tree_sitter_html_SOURCE_DIR}/src/parser.c ${tree_sitter_html_SOURCE_DIR}/src/scanner.c
    PROPERTIES INCLUDE_DIRECTORIES ${tree_sitter_html_SOURCE_DIR}/src)
target_compile_definitions(chibi_tree_sitter PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_compile_options(chibi_tree_sitter PRIVATE -w)
set_target_properties(chibi_tree_sitter PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)

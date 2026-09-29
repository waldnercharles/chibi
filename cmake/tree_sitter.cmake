# tree-sitter parses ui assets for the language service (src/assets/ui_source.c):
# the HTML grammar reads RML markup and the CSS grammar reads RCSS. Everything
# is plain C, so it builds the same natively and under Emscripten.
#
# SOURCE_SUBDIR names a folder without a CMakeLists.txt, so FetchContent only
# downloads the sources and the grammars' own targets stay out of the build.
macro(chibi_tree_sitter_source name repository tag)
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG ${tag}
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR chibi-sources-only)
    FetchContent_MakeAvailable(${name})
endmacro()

chibi_tree_sitter_source(tree_sitter
    https://github.com/tree-sitter/tree-sitter.git v0.25.10)
chibi_tree_sitter_source(tree_sitter_css
    https://github.com/tree-sitter/tree-sitter-css.git v0.25.0)
chibi_tree_sitter_source(tree_sitter_html
    https://github.com/tree-sitter/tree-sitter-html.git v0.23.2)

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

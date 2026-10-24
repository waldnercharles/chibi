set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/asset_packaging/Packaging.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
file(READ "${source}" source_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_ASSET_PACKAGING_BYTES "${source_hex}")
set(generator "${CHIBI_LANGUAGE_TEST_ROOT}/tools/graph/asset_compiler/Generate.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${generator}")
file(READ "${generator}" generator_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_ASSET_GENERATOR_BYTES "${generator_hex}")
configure_file(asset_packaging_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/asset_packaging_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE asset_packaging_chibi.c)
set_source_files_properties(asset_packaging_chibi.c PROPERTIES
    COMPILE_DEFINITIONS "CHIBI_SOURCE_ROOT=\"${CHIBI_LANGUAGE_TEST_ROOT}\"")

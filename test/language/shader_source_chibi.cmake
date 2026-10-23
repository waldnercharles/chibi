# The preserved shader contracts are compiled by the ordinary package fixture.
# This does not install a service or register a compiler Analyze producer.
foreach(part COMPILER_FACTS:compiler_facts/Facts.chibi SHADER_SOURCE:shader_source/SourceMap.chibi)
    string(REPLACE ":" ";" part "${part}")
    list(GET part 0 name)
    list(GET part 1 file)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/${file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_${name}_BYTES "${source_hex}")
endforeach()
configure_file(shader_source_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/shader_source_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE shader_source_chibi.c)

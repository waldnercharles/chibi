# Source-only standalone package compiler fixture.
foreach(component Compile Body Steps Maps References Tree Number Behavior Scalar)
    string(TOUPPER "${component}" component_upper)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_compiler/${component}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_COMPILER_${component_upper}_BYTES "${source_hex}")
endforeach()
set(scalar_facts_source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/scalar_facts/Facts.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${scalar_facts_source}")
file(READ "${scalar_facts_source}" scalar_facts_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SCALAR_FACTS_BYTES "${scalar_facts_hex}")
configure_file(package_compiler_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_compiler_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE package_compiler_chibi.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c")
set_source_files_properties(package_compiler_chibi.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")

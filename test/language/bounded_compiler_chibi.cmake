# Source-only standalone producer fixture. Registration is not test admission.
# The runtime-only compiler_host target must not acquire these producer sources.
foreach(component Compile Expression Artifact)
    string(TOUPPER "${component}" component_upper)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/bounded_compiler/${component}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_BOUNDED_${component_upper}_BYTES "${source_hex}")
endforeach()
configure_file(bounded_compiler_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/bounded_compiler_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE bounded_compiler_chibi.c compiler_prepared_fixture.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_prepared_scalar.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c")
set_source_files_properties(bounded_compiler_chibi.c compiler_prepared_fixture.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_prepared_scalar.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c"
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
target_link_libraries(test_language PRIVATE Chibi::native_prepared)

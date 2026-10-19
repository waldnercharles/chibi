# Source-only standalone writer fixture. Registration is not test admission.
set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/prepared_writer/Writer.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
file(READ "${source}" source_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PREPARED_WRITER_BYTES "${source_hex}")
configure_file(prepared_writer_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/prepared_writer_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE prepared_writer_chibi.c)
set_source_files_properties(prepared_writer_chibi.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")

# Test the shipped package sources, including an installed SDK consumer.
if(CHIBI_INSTALLED_SDK)
    set(portable_root "${Chibi_CORE_SOURCE_DIR}")
else()
    set(portable_root "${CHIBI_LANGUAGE_TEST_ROOT}/packages/core")
endif()
foreach(package bytes math/bits hash/fnv hash/adler32 hash/crc32)
    string(REPLACE "/" "_" name "${package}")
    string(TOUPPER "${name}" name)
    set(prefix "PORTABLE_${name}")
    file(GLOB sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${portable_root}/${package}/*.chibi")
    list(FILTER sources INCLUDE REGEX "\\.chibi$")
    if(NOT sources)
        message(FATAL_ERROR "The portable core package is empty: ${package}")
    endif()
    set(CHIBI_${prefix}_SOURCES "")
    set(CHIBI_${prefix}_INPUTS "")
    set(index 0)
    foreach(source IN LISTS sources)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" source_hex HEX)
        string(REGEX REPLACE "(..)" "0x\\1," source_bytes "${source_hex}")
        get_filename_component(file "${source}" NAME)
        string(APPEND CHIBI_${prefix}_SOURCES
            "static const char ${prefix}_${index}[] = { ${source_bytes} 0 };\n")
        string(APPEND CHIBI_${prefix}_INPUTS
            "    PACKAGE_COMPILER_SOURCE(\"${package}/${file}\", ${prefix}_${index}), \\\n")
        math(EXPR index "${index} + 1")
    endforeach()
endforeach()
configure_file(package_portable_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_portable_source.h" @ONLY)
target_sources(test_language PRIVATE package_portable.c package_portable_compiler.c)
set_source_files_properties(package_portable.c package_portable_compiler.c
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")

set(portable_consumer "${CHIBI_LANGUAGE_TEST_ROOT}/test/packages/portable_consumer/Consumer.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${portable_consumer}")
file(READ "${portable_consumer}" portable_consumer_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PORTABLE_CONSUMER_BYTES "${portable_consumer_hex}")
configure_file(package_portable_consumer.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_portable_consumer.h" @ONLY)
set_source_files_properties(package_portable_compiler.c
    PROPERTIES COMPILE_DEFINITIONS "CHIBI_SOURCE_ROOT=\"${CHIBI_LANGUAGE_TEST_ROOT}\"")

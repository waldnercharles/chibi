# Source-only standalone package compiler fixture.
foreach(component Compile Body Steps Maps References Pointers Tree Number Optional Sequences Behavior Scalar)
    string(TOUPPER "${component}" component_upper)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_compiler/${component}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_COMPILER_${component_upper}_BYTES "${source_hex}")
endforeach()
# The scalar facts, and the consumer that emits LLVM IR from them.
foreach(pair SCALAR_FACTS=scalar_facts/Facts SCALAR_LLVM=scalar_llvm/Emit LLVM_NATIVE=llvm_native/Emit
             NATIVE_ABI=native_abi/Abi)
    string(REPLACE "=" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 path)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/${path}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_${name}_BYTES "${source_hex}")
endforeach()
# The authored scalar corpus, which the native execution test runs.
set(source "${CHIBI_LANGUAGE_TEST_ROOT}/test/scalar_facts/cases/Cases.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
file(READ "${source}" source_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SCALAR_CASES_BYTES "${source_hex}")
configure_file(package_compiler_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_compiler_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE package_compiler_chibi.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/toolchain/compiler_host.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/process.c")
set_source_files_properties(package_compiler_chibi.c "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/process.c"
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
# The native execution test loads the objects it builds.
target_link_libraries(test_language PRIVATE ${CMAKE_DL_LIBS})

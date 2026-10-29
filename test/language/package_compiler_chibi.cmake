# Source-only standalone package compiler fixture.
foreach(component Compile Body Generics Fallible Steps Maps References Pointers Tasks TaskResults Tree Number Optional Sequences Queries QueryLoops Behavior Scalar Math)
    string(TOUPPER "${component}" component_upper)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/package_compiler/${component}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_PACKAGE_COMPILER_${component_upper}_BYTES "${source_hex}")
endforeach()
# The scalar facts, and the consumer that emits LLVM IR from them.
foreach(pair SCALAR_FACTS=scalar_facts/Facts SCALAR_LLVM=scalar_llvm/Emit LLVM_NATIVE=llvm_native/Emit
             NATIVE_ABI=native_abi/Abi MATH_TYPES=math_types/Types MATH_FUNCTIONS=math_types/Functions)
    string(REPLACE "=" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 path)
    set(source "${CHIBI_LANGUAGE_TEST_ROOT}/tools/compiler/${path}.chibi")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," CHIBI_${name}_BYTES "${source_hex}")
endforeach()
# The accepted authored scalar corpus used by native execution tests.
set(source "${CHIBI_LANGUAGE_TEST_ROOT}/test/scalar_facts/cases/Cases.chibi")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
file(READ "${source}" source_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," CHIBI_SCALAR_CASES_BYTES "${source_hex}")
# New emitter test files and @tests are discovered without C registrations.
# These test-only bytes never enter the bootstrap compiler source manifest.
set(test_root "${CHIBI_LANGUAGE_TEST_ROOT}/test/native_aot/emitter_calls")
file(GLOB test_sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${test_root}/*.chibi")
# GLOB is case-insensitive on macOS/Windows; match Graph's lowercase suffix.
list(FILTER test_sources INCLUDE REGEX "\\.chibi$")
if(NOT test_sources)
    message(FATAL_ERROR "The authored emitter regression package is empty")
endif()
set(CHIBI_EMITTER_TEST_SOURCES "")
set(CHIBI_EMITTER_TEST_INPUTS "")
set(test_index 0)
foreach(source IN LISTS test_sources)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," source_bytes "${source_hex}")
    get_filename_component(name "${source}" NAME)
    string(APPEND CHIBI_EMITTER_TEST_SOURCES
        "static const char EMITTER_TEST_SOURCE_${test_index}[] = { ${source_bytes} 0 };\n")
    string(APPEND CHIBI_EMITTER_TEST_INPUTS
        "    PACKAGE_COMPILER_SOURCE(\"llvm_native_calls/${name}\", EMITTER_TEST_SOURCE_${test_index}), \\\n")
    math(EXPR test_index "${test_index} + 1")
endforeach()
configure_file(package_compiler_chibi_source.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/testing/include/package_compiler_chibi_source.h" @ONLY)
target_sources(test_language PRIVATE package_compiler_chibi.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/native_libraries.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/process.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/stack_overflow.c")
set_source_files_properties(package_compiler_chibi.c
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/native_libraries.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/process.c"
    "${CHIBI_LANGUAGE_TEST_ROOT}/src/host/stack_overflow.c"
    PROPERTIES INCLUDE_DIRECTORIES "${CHIBI_LANGUAGE_TEST_ROOT}/src")
# The native execution test loads the objects it builds.
target_link_libraries(test_language PRIVATE ${CMAKE_DL_LIBS})

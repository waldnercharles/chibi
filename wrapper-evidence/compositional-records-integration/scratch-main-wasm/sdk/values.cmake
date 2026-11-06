include_guard(GLOBAL)

# Pure numeric contracts; no engine, renderer, or component registry. The
# math headers come from the third-party archive's Cute sources, without its renderer.
add_library(chibi_math INTERFACE)
add_library(Chibi::math ALIAS chibi_math)
set_target_properties(chibi_math PROPERTIES EXPORT_NAME math)
foreach(header cute_defines.h cute_user_config.h cute_math.h cute_math3d.h)
    configure_file("${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/include/${header}"
        "${CHIBI_SDK_HEADER_ROOT}/math/${header}" COPYONLY)
    install(FILES "${CHIBI_SDK_HEADER_ROOT}/math/${header}" DESTINATION include/chibi/math)
endforeach()
target_include_directories(chibi_math SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CHIBI_SDK_HEADER_ROOT}/math> $<INSTALL_INTERFACE:include/chibi/math>)
install(FILES "${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/LICENSE"
    DESTINATION share/chibi/licenses/cute-math)

add_library(chibi_vector_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/vectors.c"
    "${CHIBI_SDK_ROOT}/src/runtime/vectors_definition.c")
add_library(Chibi::vector_capabilities ALIAS chibi_vector_capabilities)
set_target_properties(chibi_vector_capabilities PROPERTIES EXPORT_NAME vector_capabilities)
chibi_sdk_headers(chibi_vector_capabilities PUBLIC runtime/vectors.h runtime/vectors_definition.h)
target_link_libraries(chibi_vector_capabilities PUBLIC chibi_native_capabilities chibi_math)
add_library(chibi_math_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/math.c"
    "${CHIBI_SDK_ROOT}/src/runtime/math_definition.c")
add_library(Chibi::math_capabilities ALIAS chibi_math_capabilities)
set_target_properties(chibi_math_capabilities PROPERTIES EXPORT_NAME math_capabilities)
chibi_sdk_headers(chibi_math_capabilities PUBLIC runtime/math_native.h runtime/math_definition.h)
target_link_libraries(chibi_math_capabilities PUBLIC chibi_native_capabilities PRIVATE chibi_execution)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_math_capabilities PRIVATE -ffp-contract=off)
endif()
add_library(chibi_arithmetic_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/arithmetic.c"
    "${CHIBI_SDK_ROOT}/src/runtime/arithmetic_definition.c")
add_library(Chibi::arithmetic_capabilities ALIAS chibi_arithmetic_capabilities)
set_target_properties(chibi_arithmetic_capabilities PROPERTIES EXPORT_NAME arithmetic_capabilities)
chibi_sdk_headers(chibi_arithmetic_capabilities PUBLIC runtime/arithmetic.h runtime/arithmetic_definition.h)
target_link_libraries(chibi_arithmetic_capabilities PUBLIC chibi_native_capabilities PRIVATE chibi_execution)

list(APPEND CHIBI_SDK_TARGETS chibi_math chibi_vector_capabilities chibi_math_capabilities chibi_arithmetic_capabilities)

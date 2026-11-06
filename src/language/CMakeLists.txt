add_library(chibi_language_syntax STATIC
    syntax.c
    src/parser.c
    src/scanner.c)
add_library(Chibi::language_syntax ALIAS chibi_language_syntax)
set_target_properties(chibi_language_syntax PROPERTIES EXPORT_NAME language_syntax)
chibi_sdk_headers(chibi_language_syntax PUBLIC language/syntax.h)
target_include_directories(chibi_language_syntax PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chibi_language_syntax PUBLIC chibi_support chibi_syntax_runtime)
set_source_files_properties(src/parser.c PROPERTIES COMPILE_OPTIONS "-w")
if(CHIBI_SDK_COMPILER_HOST_ONLY)
    return()
endif()

add_library(chibi_language STATIC semantic.c semantic_context.c identity.c component.c
    semantic_expression.c semantic_calls.c semantic_statement.c semantic_declaration.c semantic_effects.c semantic_units.c
    semantic_recovery.c semantic_union.c semantic_record.c semantic_constant.c constant_proof.c scalar_constant.c numeric.c lexical_binding.c nominal_binding.c named_binding.c type_constructors.c unit_algebra.c signature_reader.c callable_headers.c unit_analysis.c union_members.c effect_analysis.c record_members.c semantic_package.c
    semantic_generic.c semantic_comptime.c semantic_math.c math_types.c)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/semantic_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/semantic_internal.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/constant_proof.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/constant_proof.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/scalar_constant.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/scalar_constant.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/numeric.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/numeric.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lexical_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/lexical_binding.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/nominal_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/nominal_binding.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/named_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/named_binding.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/type_constructors.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/type_constructors.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/unit_algebra.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/unit_algebra.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/signature_reader.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/signature_reader.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/callable_headers.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/callable_headers.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/unit_analysis.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/unit_analysis.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/union_members.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/union_members.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/effect_analysis.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/effect_analysis.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/record_members.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/record_members.h" COPYONLY)
add_library(Chibi::language ALIAS chibi_language)
set_target_properties(chibi_language PROPERTIES EXPORT_NAME language)
chibi_sdk_headers(chibi_language PUBLIC language/semantic.h language/identity.h language/component.h language/math_types.h)
target_include_directories(chibi_language PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson"
    "${CMAKE_CURRENT_BINARY_DIR}/private")
target_link_libraries(chibi_language PUBLIC chibi_language_syntax chibi_capabilities PRIVATE chibi_json)
